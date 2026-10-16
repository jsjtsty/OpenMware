#!/usr/bin/env python3
"""Recover specialized SQ registration helpers from dumpbin's x86 disassembly.

Retains the original JSON. Constants inside a helper take precedence over
decompiler call arguments (which can be stale registers in optimized code).
Run dumpbin /DISASM on the original executable to produce --disassembly.
"""
from __future__ import annotations
import argparse
import bisect
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from index_mware_classes import PE32, read_tsv, write_tsv
from extract_sq_bindings import read_strings

ROOT = Path(__file__).resolve().parents[1]
INS = re.compile(r'^\s+([0-9A-F]{8}):\s+(?:[0-9A-F]{2} )+\s+([a-z][a-z0-9]*)\s*(.*?)\s*$')

def imm(s):
    s = s.strip()
    if re.fullmatch(r'[0-9A-Fa-f]+h', s): return int(s[:-1], 16)
    if re.fullmatch(r'[0-9A-Fa-f]{8}', s): return int(s, 16)
    if s.isdigit(): return int(s)
    return None

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disassembly', type=Path, default=ROOT/'build/tools/tempo-exe-disasm.txt')
    p.add_argument('--analysis-root', type=Path, default=ROOT/'ref/analysis/exe')
    p.add_argument('--exe', type=Path, default=ROOT/'ref/totono-pack/binaries/totono_Steam.exe')
    p.add_argument('--old', type=Path, default=ROOT/'ref/analysis/sqbind/bindings.json')
    p.add_argument('--out', type=Path, default=ROOT/'ref/analysis/sqbind/bindings_v2.json')
    a = p.parse_args()
    pe = PE32(a.exe)
    meta = read_tsv(a.analysis_root/'functions.tsv')
    addresses = sorted(int(r['address'],16) for r in meta if not r['address'].startswith('EXTERNAL'))
    names = {r['name']:int(r['address'],16) for r in meta if not r['address'].startswith('EXTERNAL')}
    code = defaultdict(list)
    for line in a.disassembly.read_text(errors='replace').splitlines():
        m = INS.match(line)
        if m:
            addr = int(m[1],16); idx = bisect.bisect_right(addresses,addr)-1
            if idx >= 0: code[addresses[idx]].append((addr,m[2],m[3]))
    def string(v):
        if v is None: return None
        try: s = pe.c_string(v)
        except (ValueError,UnicodeError): return None
        return s if re.fullmatch(r'[A-Za-z_$][\w$]*',s) else None
    # Specialized helpers pass EDX to sq_pushstring and write the native
    # member pointer to newly allocated userdata. No speculative symbol names.
    specialized = {}
    for addr, ins in code.items():
        if not any(op=='call' and imm(arg)==0x40ae50 for _,op,arg in ins): continue
        edx = None; name = None; native = None
        for _,op,arg in ins:
            if op=='mov' and arg.startswith('edx,'): edx=imm(arg.split(',',1)[1])
            if op=='call' and imm(arg)==0x40a510: name = string(edx) or name
            if op=='mov' and re.match(r'dword ptr \[eax\],',arg):
                v=imm(arg.split(',',1)[1])
                if v is not None and pe.is_code(v): native=v
            if op=='call': edx=None
        if name and native: specialized[addr]=(name,native)
    old = json.loads(a.old.read_text(encoding='utf-8'))
    initializers = defaultdict(set)
    for r in old:
        if r['registrar_shape']['binding']=='class_constructor':
            addr = names.get(r['registrar_address'])
            if addr is None and r['registrar_address'].startswith('FUN_'): addr=int(r['registrar_address'][4:],16)
            if addr is not None: initializers[addr].add(r['class'])
    initializers[0x525770].add('SQMATH')
    refs=defaultdict(Counter)
    for path in sorted((ROOT/'ref/work/script_x/media/script').rglob('*.nut')):
        for name in read_strings(path): refs[name]['framework_files' if 'framework' in path.parts else 'other_files']+=1
    rows = [dict(r,source='decompile') for r in old]
    recovered=[]; evidence=[]; replacements=set()
    for init,classes in sorted(initializers.items()):
        # Multi-class global initializers need explicit class attribution.
        if len(classes)!=1: continue
        cls=next(iter(classes)); pushed=[]
        for pc,op,arg in code[init]:
            if op=='push':
                v=imm(arg)
                if v is not None: pushed.append(v)
            if op!='call': continue
            target=imm(arg)
            if target in specialized:
                name,native=specialized[target]
                registrar=f'FUN_{target:08x}'
                replacements.add((cls,registrar))
                prior=next((r for r in old if r['class']==cls and r['registrar_address']==registrar),None)
                shape=prior['registrar_shape'] if prior else {'return':'unknown','parameters':[],'binding':'instance','inferred':True}
                recovered.append({'class':cls,'script_name':name,'native_address':f'FUN_{native:08x}',
                                  'registrar_address':registrar,'registrar_shape':shape,
                                  'references':{'framework_files':refs[name]['framework_files'],'other_files':refs[name]['other_files']},
                                  'source_line':0,'source':'disassembly'})
                evidence.append((cls,name,f'0x{init:08x}',f'0x{pc:08x}',registrar,f'FUN_{native:08x}','constant name and native inside registrar'))
            pushed=[]
    rows=[r for r in rows if (r['class'],r['registrar_address']) not in replacements or r['registrar_shape']['binding']=='class_constructor']+recovered
    unique={}
    for r in rows: unique[(r['class'],r['script_name'],r['native_address'],r['registrar_address'])]=r
    rows=sorted(unique.values(),key=lambda r:(r['class'],r['script_name'],r['native_address'],r['registrar_address']))
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    write_tsv(a.out.with_name('bindings_v2_evidence.tsv'),['class','name','initializer','call_site','registrar','native','evidence'],evidence)
    old_names={r['script_name'] for r in old}; new_names={r['script_name'] for r in rows}
    stats={'old_rows':len(old),'rows':len(rows),'old_names':len(old_names),'names':len(new_names),
           'assembly_rows':len(recovered),'new_names':sorted(new_names-old_names),'specialized_helpers':len(specialized)}
    a.out.with_name('bindings_v2_stats.json').write_text(json.dumps(stats,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(stats,ensure_ascii=False))

if __name__=='__main__': main()
