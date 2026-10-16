#!/usr/bin/env python3
"""Map known SQ natives to embedded-object vtable calls, conservatively.

All bindings get rows, including direct implementations and unresolved calls.
Class candidates carry explicit construction/factory evidence; an offset alone
never establishes a dynamic class. Additional field types can be supplied in
--field-types JSON (keys SQClass:0xFIELD, values classes/evidence).
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from index_mware_classes import PE32, Function, read_tsv, write_tsv, attach_decompilation
ROOT=Path(__file__).resolve().parents[1]

# Audited construction chains, not mappings inferred from spelling of SQ names.
FIELD_TYPES={
 'SQSprite:0x100':{'classes':['Diesel::CSprite','Diesel::CNullSpriteMask'], 'evidence':'004deaa0 -> 0046dab0 -> 00443a60 (CSprite); null fallback 004dc210 (CNullSpriteMask); field +0x100'},
 'SQText:0x100':{'classes':['Diesel::CText','Diesel::CTextEnglish','Diesel::CTextVertical'], 'evidence':'004f2d30 stores result of 0046e270 into [0x40]; factory branches call 004a3170 (CText) and replace primary vptr with CTextEnglish/CTextVertical'},
 'SQSound:0xbc':{'classes':['Diesel::CSound','Diesel::CNullSound'], 'evidence':'004f80a0 -> 0046e3f0 -> 004b5930 (CSound), or CNullSound primary-vptr assignment; stores field +0xbc'},
}
DIRECT=re.compile(r'\(\*\*\(code\s*\*\*\)\(\*\*\(int\s*\*\*\)\((\w+)\s*\+\s*(0x[0-9a-f]+|\d+)\)\s*\+\s*(0x[0-9a-f]+|\d+)\)\)')
ALIAS=re.compile(r'\b(\w+)\s*=\s*\*\(int\s*\*\*\)\((\w+)\s*\+\s*(0x[0-9a-f]+|\d+)\)')
INDIRECT=re.compile(r'\(\*\*\(code\s*\*\*\)\(\*(\w+)\s*\+\s*(0x[0-9a-f]+|\d+)\)\)')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--analysis-root',type=Path,default=ROOT/'ref/analysis/exe');p.add_argument('--field-types',type=Path);a=p.parse_args()
 pe=PE32(ROOT/'ref/totono-pack/binaries/totono_Steam.exe')
 functions=[Function(int(r['address'],16),r['name'],int(r['size']),r['signature']) for r in read_tsv(a.analysis_root/'functions.tsv') if not r['address'].startswith('EXTERNAL')]
 failed={int(r['address'],16) for r in read_tsv(a.analysis_root/'decompile/failures.tsv')}
 attach_decompilation((a.analysis_root/'decompile/totono_Steam.exe.c').read_text(encoding='utf-8-sig'),functions,failed)
 funcs={f.address:f for f in functions}
 tables={r['class_name']:pe.vtable(int(r['vtable_address'],16))[0] for r in read_tsv(a.analysis_root/'rtti_classes.tsv') if r['vtable_address']!='unknown'}
 field_types=dict(FIELD_TYPES)
 if a.field_types:field_types.update(json.loads(a.field_types.read_text(encoding='utf-8')))
 bindings=json.loads((ROOT/'ref/analysis/sqbind/bindings_v2.json').read_text(encoding='utf-8'))
 for cls in sorted({b['class'] for b in bindings if b['class'].startswith('SQ')}):
  field_types.setdefault(cls+':0xb4',{'classes':['Diesel::CTask','Diesel::CTaskMain'], 'evidence':'00481e80/00481fa0 CTaskObject constructors store current CApp task-stack top (+0x90..0x94) in +0xb4; main task constructed by 0046a150; dynamic task subclass is a candidate, not uniquely proven'})
 rows=[];resolved=set();forwarded=set();coverage=Counter()
 for i,b in enumerate(bindings):
  n=b['native_address'];f=funcs.get(int(n[4:],16)) if re.fullmatch('FUN_[0-9a-fA-F]{8}',n) else None
  s=re.sub(r'\(int\)\s*this\b','this',f.body) if f else '';calls=set((base,int(field,0),int(off,0)) for base,field,off in DIRECT.findall(s))
  aliases={v:(base,int(field,0)) for v,base,field in ALIAS.findall(s)}
  for var,off in INDIRECT.findall(s):
   if var in aliases:calls.add((*aliases[var],int(off,0)))
  basecols=(b['class'],b['script_name'],n,b['source'])
  if not calls:
   rows.append((*basecols,'','','','','','', 'no_embedded_vcall' if f else 'missing_body','direct_calls='+','.join(sorted(set(re.findall(r'\bFUN_[0-9a-f]{8}(?=\()',s)))) if f else ''))
  for base,field,off in sorted(calls):
   forwarded.add(i);key=f"{b['class']}:0x{field:x}";mapping=field_types.get(key);candidates=mapping['classes'] if mapping else []
   # Globals and arbitrary parameter objects need separate provenance.
   if base not in ('param_1','this'):candidates=[]
   matches=[]
   for cls in candidates:
    tab=tables.get(cls,[])
    if off%4==0 and off//4<len(tab):matches.append((cls,tab[off//4]))
   if matches:resolved.add(i);coverage[b['class']]+=1
   if not matches:
    rows.append((*basecols,base,f'0x{field:x}',f'0x{off:x}',off//4,'','', 'unresolved_field_type',mapping['evidence'] if mapping else 'construction trace required'))
   for cls,target in matches:
    rows.append((*basecols,base,f'0x{field:x}',f'0x{off:x}',off//4,cls,f'0x{target:08x}', 'candidate' if len(candidates)>1 else 'resolved',mapping['evidence']))
 out=a.analysis_root/'classes';out.mkdir(parents=True,exist_ok=True)
 write_tsv(out/'sqwrappers.tsv',['sq_class','script_name','native','binding_source','receiver','field_byte_offset','vtable_byte_offset','slot','diesel_class','target','status','evidence'],rows)
 (out/'sqwrapper_field_types.json').write_text(json.dumps(field_types,indent=2)+'\n',encoding='utf-8')
 stats={'bindings':len(bindings),'rows':len(rows),'bindings_with_forwarding':len(forwarded),'bindings_with_resolved_candidates':len(resolved),'resolved_call_sites_by_class':dict(coverage)}
 (out/'sqwrappers_stats.json').write_text(json.dumps(stats,indent=2)+'\n',encoding='utf-8');print(json.dumps(stats))
if __name__=='__main__':main()
