"""Extract the CBlend/CSampler state table built in the CGraphicsDX11 constructor of Mware.dll (from the Ghidra decompile)."""
import re, sys, json
src = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
start, end = int(sys.argv[2]), int(sys.argv[3])
BLEND = ['ZERO','ONE','SRC_COLOR','INV_SRC_COLOR','SRC_ALPHA','INV_SRC_ALPHA','DEST_ALPHA','INV_DEST_ALPHA','DEST_COLOR','INV_DEST_COLOR']
OP = ['ADD','SUBTRACT','REV_SUBTRACT','MIN','MAX']
mem = {}
def ev(tok):
    tok = tok.strip()
    tok = re.sub(r'\(\s*(int|undefined4)\s*\*\s*\)', '', tok).strip()
    tok = tok.lstrip('&').strip('() ')
    m = re.fullmatch(r'(\w+)\s*\+\s*(\d+)', tok)
    if m: return mem.get(f'{m.group(1)}[{m.group(2)}]')
    if re.fullmatch(r'local_\w+', tok): return mem.get(tok, mem.get(tok + '[0]'))
    return None
slot = None; this_slot = {}
out = []
text = ' '.join(l.strip() for l in src[start-1:end])
for stmt in re.split(r';', text):
    s = stmt.strip()
    m = re.match(r'(\w+(?:\[\w+\])?) = (?:\(undefined4 \*\))?(0x[0-9a-f]+|\d+)$', s)
    if m:
        k, v = m.group(1), int(m.group(2), 0)
        mem[k] = v
        mm = re.fullmatch(r'(local_\w+)\[(\d+)\]', k)
        if mm: pass
        if re.fullmatch(r'\w+', k): mem[k + '[0]'] = v
        continue
    m = re.match(r'param_1\[(0x[0-9a-f]+)\] = (\w+)$', s)
    if m: this_slot[m.group(2)] = int(m.group(1), 16); continue
    m = re.match(r'FUN_(100865c0|100866c0)\((\w+),(.*)\)$', s)
    if m:
        a = [ev(x) for x in re.split(r',\s*(?=\(int|\w)', m.group(3))]
        sl = this_slot.get(m.group(2))
        if any(x is None for x in a):
            continue
        if m.group(1) == '100865c0':
            en, op, sf, df, am, r, g, b = a
            out.append(dict(slot=hex(sl) if sl is not None else "?", enable=en, op=OP[op], src=BLEND[sf], dst=BLEND[df], srcA='ONE', dstA='ONE', opA='ADD', mask=''.join(c for c, v in zip('ARGB', (am, r, g, b)) if v)))
        else:
            en, op, sf, df, opa, sfa, dfa, am, r, g, b = a
            out.append(dict(slot=hex(sl) if sl is not None else "?", enable=en, op=OP[op], src=BLEND[sf], dst=BLEND[df], srcA=BLEND[sfa], dstA=BLEND[dfa], opA=OP[opa], mask=''.join(c for c, v in zip('ARGB', (am, r, g, b)) if v)))
print(f'{"slot":7}{"en":3}{"rgb":38}{"alpha":30}mask')
for o in out:
    print(f'{o["slot"]:7}{o["enable"]:<3}{o["op"]+" "+o["src"]+"/"+o["dst"]:38}{o["opA"]+" "+o["srcA"]+"/"+o["dstA"]:30}{o["mask"]}')
json.dump(out, open('ref/analysis/render/blend_table.json', 'w'), indent=1)
