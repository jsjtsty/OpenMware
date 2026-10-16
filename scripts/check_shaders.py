"""Compile every HLSL manifest row with the Windows D3DCompiler and compare DXBC."""
from __future__ import annotations

import argparse
import ctypes
import re
import sys
from pathlib import Path
from ctypes import wintypes

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "shaders/hlsl/manifest.tsv"
CSO_ROOT = ROOT / "ref/work/system_x/shader"
ASM_ROOT = ROOT / "ref/analysis/shaders"


class Blob(ctypes.Structure):
    pass


BlobPtr = ctypes.POINTER(Blob)
VTable = ctypes.POINTER(ctypes.c_void_p)
Blob._fields_ = [("vtable", VTable)]


def _blob_bytes(blob: BlobPtr) -> bytes:
    this = ctypes.cast(blob, ctypes.c_void_p)
    vtable = blob.contents.vtable
    get_data = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtable[3])
    get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtable[4])
    return ctypes.string_at(get_data(this), get_size(this))


def _release(blob: BlobPtr) -> None:
    if blob:
        this = ctypes.cast(blob, ctypes.c_void_p)
        release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(blob.contents.vtable[2])
        release(this)


def _compiler():
    try:
        dll = ctypes.WinDLL("d3dcompiler_47.dll")
    except AttributeError as error:
        raise RuntimeError("This checker requires Windows and d3dcompiler_47.dll") from error
    dll.D3DCompile.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_void_p,
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, wintypes.UINT,
        wintypes.UINT, ctypes.POINTER(BlobPtr), ctypes.POINTER(BlobPtr),
    ]
    dll.D3DCompile.restype = ctypes.c_long
    dll.D3DDisassemble.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.UINT,
                                  ctypes.c_char_p, ctypes.POINTER(BlobPtr)]
    dll.D3DDisassemble.restype = ctypes.c_long
    return dll


def _disassemble(dll, bytecode: bytes) -> str:
    code = ctypes.create_string_buffer(bytecode)
    blob = BlobPtr()
    hr = dll.D3DDisassemble(code, len(bytecode), 0, None, ctypes.byref(blob))
    if hr < 0:
        raise RuntimeError(f"D3DDisassemble failed: 0x{hr & 0xffffffff:08x}")
    try:
        return _blob_bytes(blob).decode("latin1")
    finally:
        _release(blob)


def _opcodes(disassembly: str) -> list[str]:
    result = []
    in_code = False
    for line in disassembly.splitlines():
        if re.match(r"^(ps|vs|gs)_\d_\d$", line.strip()):
            in_code = True
            continue
        if not in_code:
            continue
        match = re.match(r"\s*([a-z][a-z0-9_]*)\s", line)
        if match:
            result.append(match.group(1))
    return result


def _metadata(disassembly: str) -> tuple[str, ...]:
    # Keep compiler reflection-like tables: cbuffer members, resource registers,
    # and input/output signatures. Ignore whitespace and generated comments.
    kept = []
    section = None
    for line in disassembly.splitlines():
        if "Buffer Definitions:" in line:
            section = "buffer"
        elif "Resource Bindings:" in line:
            section = "resource"
        elif "Input signature:" in line:
            section = "input"
        elif "Output signature:" in line:
            section = "output"
        elif re.match(r"^(ps|vs|gs)_\d_\d$", line.strip()):
            section = None
        if section and line.strip().startswith("//"):
            value = re.sub(r"\s+", " ", line.strip("/ "))
            value = re.sub(r"\bSV_Position\b", "SV_POSITION", value, flags=re.IGNORECASE)
            if value and not set(value) <= {"-", " "}:
                kept.append(f"{section}:{value}")
    return tuple(kept)


def _compile(dll, source: Path, defines: list[str], profile: str) -> tuple[str, bytes]:
    data = source.read_bytes()
    source_buffer = ctypes.create_string_buffer(data)
    macros = []
    for define in defines:
        name, _, value = define.partition("=")
        macros.append((name, value or "1"))
    class Macro(ctypes.Structure):
        _fields_ = [("Name", ctypes.c_char_p), ("Definition", ctypes.c_char_p)]
    macro_array = (Macro * (len(macros) + 1))()
    for index, (name, value) in enumerate(macros):
        macro_array[index] = Macro(name.encode("ascii"), value.encode("ascii"))
    out, errors = BlobPtr(), BlobPtr()
    # D3D_COMPILE_STANDARD_FILE_INCLUDE is the sentinel value 1.
    hr = dll.D3DCompile(source_buffer, len(data), str(source).encode(), macro_array,
                        ctypes.c_void_p(1), b"main", profile.encode(), 0,
                        0, ctypes.byref(out), ctypes.byref(errors))
    diagnostic = _blob_bytes(errors).decode("utf-8", "replace") if errors else ""
    _release(errors)
    if hr < 0:
        _release(out)
        raise RuntimeError(diagnostic.strip() or f"D3DCompile failed: 0x{hr & 0xffffffff:08x}")
    try:
        bytecode = _blob_bytes(out)
    finally:
        _release(out)
    return diagnostic.strip(), bytecode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--limit", type=int, help="compile only the first N rows")
    args = parser.parse_args()
    try:
        dll = _compiler()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 2
    identical, different, failed = [], [], []
    rows = args.manifest.read_text(encoding="utf-8").splitlines()[1:]
    shader_names = [row.split("\t", 1)[0] for row in rows if row.strip()]
    duplicates = sorted({name for name in shader_names if shader_names.count(name) > 1})
    if duplicates:
        print("Duplicate manifest shader names: " + ", ".join(duplicates), file=sys.stderr)
        return 2
    for row in rows[:args.limit]:
        name, source_name, define_text = row.split("\t")
        original_stem = Path(name)
        asm_path = (ASM_ROOT / original_stem.with_suffix(".asm") if "/" in name or "\\" in name
                    else next(ASM_ROOT.rglob(name + ".asm")))
        cso_path = (CSO_ROOT / original_stem.with_suffix(".cso") if "/" in name or "\\" in name
                    else next(CSO_ROOT.rglob(name + ".cso")))
        source_path = args.manifest.parent / source_name
        defines = [part for part in define_text.split(";") if part]
        original_profile = next((line.strip() for line in asm_path.read_text("latin1").splitlines()
                                 if re.fullmatch(r"(?:ps|vs|gs)_\d_\d", line.strip())), "ps_4_0")
        try:
            warnings, bytecode = _compile(dll, source_path, defines, original_profile)
            compiled = _disassemble(dll, bytecode)
            original = asm_path.read_text("latin1")
            same_metadata = _metadata(compiled) == _metadata(original)
            same_ops = _opcodes(compiled) == _opcodes(original)
            if same_metadata and same_ops:
                identical.append(name)
            else:
                notes = []
                if not same_metadata:
                    notes.append("layout/resource/signature metadata differs")
                if not same_ops:
                    if "reconstruction scaffold" in source_path.read_text(encoding="utf-8"):
                        notes.append("source preserves the interface but its shader-specific logic still needs reconstruction")
                    elif source_path.parent.name == "scene" and original_profile == "ps_4_0":
                        notes.append("scene lighting arithmetic was reconstructed; generated opcode order differs")
                    elif name.startswith(("pssprite_", "psprimitive_")):
                        notes.append("blend and transition arithmetic was rewritten; generated opcode order differs")
                    elif name.startswith("psparticle_"):
                        notes.append("particle modulation and soft-depth fade were rewritten; generated opcode order differs")
                    elif name.startswith("ps") and source_path.parent.name == "posteffect":
                        notes.append("post-effect arithmetic was rewritten; generated opcode order differs")
                    else:
                        notes.append("vertex or geometry arithmetic was rewritten; generated opcode order differs")
                different.append((name, "; ".join(notes)))
                print("DIFF", name, "; ".join(notes))
            if warnings:
                print("WARN", name, warnings.replace("\n", " | "))
        except Exception as error:
            failed.append((name, str(error).replace("\n", " | ")))
            print("FAIL", name, failed[-1][1])
    print(f"Identical opcode sequences: {len(identical)}")
    print(f"Different: {len(different)}")
    print(f"Failed to compile: {len(failed)}")
    print(f"Manifest rows processed: {len(rows[:args.limit])}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
