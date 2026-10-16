"""Disassemble all .cso under a directory with d3dcompiler_47 (Windows only). Output: <out>/<rel>.asm"""
import ctypes, sys, pathlib
from ctypes import wintypes as w
src, out = map(pathlib.Path, sys.argv[1:3])
d = ctypes.WinDLL("d3dcompiler_47.dll")
class Blob(ctypes.Structure): pass
vt = ctypes.POINTER(ctypes.c_void_p)
Blob._fields_ = [("vtbl", vt)]
d.D3DDisassemble.argtypes = [ctypes.c_char_p, ctypes.c_size_t, w.UINT, ctypes.c_char_p, ctypes.POINTER(ctypes.POINTER(Blob))]
d.D3DDisassemble.restype = ctypes.c_long
GetPtr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(0)
n = bad = 0
for f in sorted(src.rglob("*.cso")):
    b = f.read_bytes(); blob = ctypes.POINTER(Blob)()
    hr = d.D3DDisassemble(b, len(b), 0, None, ctypes.byref(blob))
    if hr: bad += 1; print("FAIL", f, hex(hr & 0xffffffff)); continue
    this = ctypes.cast(blob, ctypes.c_void_p)
    vtbl = blob.contents.vtbl
    getp = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtbl[3])
    gets = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtbl[4])
    txt = ctypes.string_at(getp(this), gets(this)).decode("latin1")
    dst = out / f.relative_to(src).with_suffix(".asm"); dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(txt, encoding="utf-8"); n += 1
print("ok", n, "bad", bad)
