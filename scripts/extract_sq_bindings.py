#!/usr/bin/env python3
"""Extract binding names and references from Ghidra's C export and SCRP files.

The generated artifacts contain metadata only (names, addresses, line numbers,
counts and inferred type categories); proprietary source/binary contents are not
copied into the output tree.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve()
ROOT = SCRIPT_PATH.parents[2] if SCRIPT_PATH.parents[1].name == "libtotono" else SCRIPT_PATH.parents[1]
DEFAULT_DECOMPILE = ROOT / "ref/totono-pack/decompile/totono_Steam.exe.c"
DEFAULT_SCRIPTS = Path(r"D:\Games\君与彼女与彼女之恋\script.npk~\media\script")
DEFAULT_OUT = ROOT / "ref/analysis/sqbind"

FUNCTION_START = re.compile(r"(?m)^(?P<header>[^\s\r\n][^\r\n]*)\r?\n\r?\n\{")
FUN_NAME = re.compile(r"\b(FUN_[0-9a-fA-F]+|Initialize[A-Za-z0-9_]+)\s*\(")
CLASS_LITERAL = re.compile(r'param_1\[1\]\s*=\s*"([^"]+)"\s*;')
CALL_START = re.compile(r"\b(FUN_[0-9a-fA-F]+)\s*\(")
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
ASCII_RUN = re.compile(rb"[\x20-\x7e]{2,}")
TARGET_CLASSES = [
    "SQApp", "SQSprite", "SQSprite3D", "SQText", "SQSound", "SQSound3D", "SQSoundDevice",
    "SQModel", "SQModelCharacter", "SQBone", "SQJoint", "SQCamera", "SQParticle", "SQPhysics",
    "SQRigidBody", "SQPointLight", "SQNull3D", "SQShadeMask", "SQSpriteMask",
    "SQSpriteRenderTarget", "SQDisplacementMap", "SQProcess", "SQAction1D", "SQScene", "SQFile",
    "SQDes", "SQLine", "SQMATH", "Mware::Vector2D", "Mware::Vector3D", "Mware::Rect2D", "Mware::ColorF",
]


def parse_functions(text: str) -> list[dict]:
    starts = list(FUNCTION_START.finditer(text))
    functions = []
    for i, match in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(text)
        header = match.group("header").strip().replace("\r", " ")
        name_match = FUN_NAME.search(header)
        if not name_match:
            continue
        functions.append({
            "name": name_match.group(1),
            "header": header,
            "body": text[match.end():end],
            "line": text.count("\n", 0, match.start()) + 1,
        })
    return functions


def split_args(raw: str) -> list[str]:
    args, start, depth, quoted, escaped = [], 0, 0, False, False
    for i, char in enumerate(raw):
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            args.append(raw[start:i].strip())
            start = i + 1
    args.append(raw[start:].strip())
    return args


def calls(body: str):
    """Yield function calls using balanced parentheses, preserving line offsets."""
    for match in CALL_START.finditer(body):
        pos = match.end()
        depth, quoted, escaped = 1, False, False
        while pos < len(body) and depth:
            char = body[pos]
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    quoted = False
            elif char == '"':
                quoted = True
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            pos += 1
        if depth == 0:
            yield match.group(1), split_args(body[match.end():pos - 1]), match.start()


def unquote(expr: str) -> str | None:
    expr = expr.strip()
    if len(expr) >= 2 and expr[0] == expr[-1] == '"':
        return bytes(expr[1:-1], "utf-8").decode("unicode_escape")
    return None


def assignments_before(body: str, end: int) -> dict[str, str]:
    assigns = {}
    for match in re.finditer(r'\b([A-Za-z_]\w*)\s*=\s*("(?:[^"\\]|\\.)*"|&?(?:FUN|LAB)_[0-9a-fA-F]+|[A-Za-z_]\w*)\s*;', body[:end]):
        assigns[match.group(1)] = match.group(2).lstrip("&")
    return assigns


def resolve(expr: str, variables: dict[str, str]) -> str:
    expr = expr.strip().lstrip("&")
    seen = set()
    while expr in variables and expr not in seen:
        seen.add(expr)
        expr = variables[expr].strip().lstrip("&")
    return expr


def resolve_name(expr: str, variables: dict[str, str]) -> str | None:
    value = resolve(expr, variables)
    return unquote(value)


def read_strings(path: Path) -> set[str]:
    data = path.read_bytes()
    found = set()
    # Squirrel 2 SCRP string entries use tag 0x08000010 followed by a
    # little-endian byte length and UTF-8 bytes. This avoids treating arbitrary
    # printable bytecode fragments as script references.
    tag = b"\x10\x00\x00\x08"
    for match in re.finditer(re.escape(tag), data):
        length_offset = match.end()
        if length_offset + 4 > len(data):
            continue
        length = struct.unpack_from("<I", data, length_offset)[0]
        start = length_offset + 4
        if length == 0 or length > 1_000_000 or start + length > len(data):
            continue
        value = data[start:start + length].decode("utf-8", errors="ignore")
        if value and not any(ord(c) < 0x20 for c in value):
            found.add(value)
    # Keep UTF-16LE runs for possible toolchain variants.
    for match in re.finditer(rb"(?:[\x20-\x7e]\x00){2,}", data):
        found.add(match.group().decode("utf-16le", errors="ignore"))
    return found


def type_category(type_name: str) -> str:
    t = type_name.lower()
    if "void" in t:
        return "void"
    if "bool" in t:
        return "bool"
    if "float" in t or "double" in t:
        return "float"
    if "char" in t or "string" in t:
        return "string"
    if "int" in t or "uint" in t or "long" in t or "short" in t:
        return "int"
    if "*" in type_name or "&" in type_name or "Vector" in type_name or "Color" in type_name:
        return "object"
    return "unknown"


def native_shape(native_expr: str, function_by_name: dict[str, dict]) -> dict:
    fn = function_by_name.get(native_expr)
    if not fn:
        return {"return": "unknown", "parameters": [], "binding": "unknown", "inferred": True}
    header = fn["header"]
    proto = re.search(r"(?:__cdecl|__thiscall|__fastcall|__stdcall)?\s*([^\s]+)\s+" + re.escape(native_expr) + r"\((.*)\)", header)
    if not proto:
        return {"return": "unknown", "parameters": [], "binding": "unknown", "inferred": True}
    ret = type_category(proto.group(1))
    raw_params = proto.group(2).strip()
    params = [] if not raw_params or raw_params == "void" else [type_category(p.strip().split(" param_")[0]) for p in split_args(raw_params)]
    convention = "instance" if "__thiscall" in header else ("static" if "__cdecl" in header or "__stdcall" in header else "unknown")
    return {"return": ret, "parameters": params, "binding": convention, "inferred": True}


def extract(decompile_path: Path, scripts_root: Path) -> tuple[list[dict], dict]:
    text = decompile_path.read_text(encoding="utf-8", errors="replace")
    functions = parse_functions(text)
    function_by_name = {fn["name"]: fn for fn in functions}
    constructors = {}
    valid_class_literals = set(TARGET_CLASSES) | {name.split("::")[-1] for name in TARGET_CLASSES}
    for fn in functions:
        for class_name in CLASS_LITERAL.findall(fn["body"]):
            if class_name in {"Vector2D", "Vector3D", "ColorF", "Rect2DF"}:
                class_name = "Mware::" + {"Rect2DF": "Rect2D"}.get(class_name, class_name)
            if class_name not in valid_class_literals:
                continue
            constructors[fn["name"]] = class_name

    script_files = sorted(scripts_root.rglob("*.nut"))
    script_refs: dict[str, Counter] = defaultdict(Counter)
    all_names = set()
    for path in script_files:
        strings = read_strings(path)
        is_framework = "framework" in path.parts
        for name in strings:
            script_refs[name]["framework" if is_framework else "other"] += 1
            all_names.add(name)

    bindings = []
    seen = set()
    for fn in functions:
        fn_calls = list(calls(fn["body"]))
        first_ctor_call = next(((callee, offset) for callee, _, offset in fn_calls if callee in constructors), None)
        # Class names assigned in constructor-like functions are registrations
        # too. Preserve their creator and the initializer that publishes them.
        class_registrar = (fn["name"] == "InitializeSystemBindings" or
                           (fn["header"].startswith("void ") and first_ctor_call is not None))
        if class_registrar and first_ctor_call is not None:
            ctor, ctor_offset = first_ctor_call
            class_name = constructors[ctor]
            script_name = class_name.split("::")[-1]
            key = (class_name, script_name, ctor, "class_constructor")
            if key not in seen:
                seen.add(key)
                refs = script_refs.get(script_name, Counter())
                bindings.append({
                    "class": class_name, "script_name": script_name,
                    "native_address": ctor, "registrar_address": fn["name"],
                    "registrar_shape": {"return": "object", "parameters": [], "binding": "class_constructor", "inferred": True},
                    "references": {"framework_files": refs["framework"], "other_files": refs["other"]},
                    "source_line": fn["line"] + 2 + fn["body"][:ctor_offset].count("\n"),
                })

        if fn["name"] == "InitializeSquirrelRuntime":
            for ctor, offset in ((callee, off) for callee, _, off in fn_calls if callee in constructors):
                class_name = constructors[ctor]
                script_name = class_name.split("::")[-1]
                key = (class_name, script_name, ctor, "class_constructor")
                if key in seen:
                    continue
                seen.add(key)
                refs = script_refs.get(script_name, Counter())
                bindings.append({
                    "class": class_name, "script_name": script_name,
                    "native_address": ctor, "registrar_address": fn["name"],
                    "registrar_shape": {"return": "object", "parameters": [], "binding": "class_constructor", "inferred": True},
                    "references": {"framework_files": refs["framework"], "other_files": refs["other"]},
                    "source_line": fn["line"] + 2 + fn["body"][:offset].count("\n"),
                })
        # A single registration initializer invokes its class constructor near
        # the beginning. Global runtime initialization invokes several type
        # constructors and must not be mistaken for class-owned registrations.
        first_ctor = next((constructors[callee] for callee, _, _ in fn_calls if callee in constructors), None)
        classes = {first_ctor} if first_ctor and fn["name"] != "InitializeSquirrelRuntime" else set()
        if fn["name"] == "FUN_00525770":
            classes.add("SQMATH")
        for registrar, args, offset in fn_calls:
            if not classes:
                break
            if len(args) < 2:
                continue
            vars_at = assignments_before(fn["body"], offset)
            name = resolve_name(args[1], vars_at)
            if not name or not re.fullmatch(r"[A-Za-z_$][A-Za-z0-9_$]*", name):
                continue
            native = resolve(args[0], vars_at)
            for class_name in sorted(classes):
                key = (class_name, name, native, registrar)
                if key in seen:
                    continue
                seen.add(key)
                shape = native_shape(native, function_by_name)
                refs = script_refs.get(name, Counter())
                bindings.append({
                    "class": class_name,
                    "script_name": name,
                    "native_address": native,
                    "registrar_address": registrar,
                    "registrar_shape": shape,
                    "references": {"framework_files": refs["framework"], "other_files": refs["other"]},
                    "source_line": fn["line"] + 2 + fn["body"][:offset].count("\n"),
                })

        # Global math function registrations pass the script-visible name as
        # argument 1 (there is no native function pointer in this initializer).
        if fn["name"] == "FUN_00525770":
            for registrar, args, offset in fn_calls:
                if not args:
                    continue
                name = unquote(args[0])
                if not name or not name.startswith("SQMATH_"):
                    continue
                key = ("SQMATH", name, "unresolved", registrar)
                if key in seen:
                    continue
                seen.add(key)
                refs = script_refs.get(name, Counter())
                bindings.append({
                    "class": "SQMATH", "script_name": name,
                    "native_address": "unresolved", "registrar_address": registrar,
                    "registrar_shape": {"return": "unknown", "parameters": [], "binding": "static", "inferred": True},
                    "references": {"framework_files": refs["framework"], "other_files": refs["other"]},
                    "source_line": fn["line"] + 2 + fn["body"][:offset].count("\n"),
                })

        # SQMATH_* constants are installed through a value registrar.
        if fn["name"] == "FUN_00437720":
            for registrar, args, offset in fn_calls:
                if not args:
                    continue
                name = unquote(args[0])
                if not name or not name.startswith("SQMATH_"):
                    continue
                key = ("SQMATH", name, "constant", registrar)
                if key in seen:
                    continue
                seen.add(key)
                refs = script_refs.get(name, Counter())
                bindings.append({
                    "class": "SQMATH", "script_name": name,
                    "native_address": "constant", "registrar_address": registrar,
                    "registrar_shape": {"return": "value", "parameters": [], "binding": "static", "inferred": True},
                    "references": {"framework_files": refs["framework"], "other_files": refs["other"]},
                    "source_line": fn["line"] + 2 + fn["body"][:offset].count("\n"),
                })

    bindings.sort(key=lambda b: (b["class"], b["script_name"], b["native_address"], b["registrar_address"]))
    stats = {
        "decompile": str(decompile_path),
        "scripts_root": str(scripts_root),
        "script_file_count": len(script_files),
        "binding_count": len(bindings),
        "distinct_name_count": len({b["script_name"] for b in bindings}),
        "distinct_referenced_name_count": len({b["script_name"] for b in bindings if b["references"]["framework_files"] or b["references"]["other_files"]}),
        "distinct_unreferenced_name_count": len({b["script_name"] for b in bindings if not b["references"]["framework_files"] and not b["references"]["other_files"]}),
        "classes": {k: Counter(b["class"] for b in bindings)[k] for k in TARGET_CLASSES},
        "distinct_names_by_class": {k: len({b["script_name"] for b in bindings if b["class"] == k}) for k in TARGET_CLASSES},
        "unmatched_names_seen_in_scripts": len(all_names),
    }
    return bindings, stats


def render_markdown(bindings: list[dict], stats: dict, decompile_path: Path) -> str:
    classes = defaultdict(list)
    for binding in bindings:
        classes[binding["class"]].append(binding)
    for class_name in TARGET_CLASSES:
        classes.setdefault(class_name, [])
    out = [
        "# Squirrel native bindings",
        "",
        "## Schema",
        "",
        "`bindings.json` is an array. Each row has `class`, `script_name`, `native_address`, `registrar_address`, `registrar_shape`, `references`, and `source_line`. Shape categories and binding kind are inferred from the decompiler's prototype/calling convention; unknown values are left explicit.",
        "",
        "All name/address/count data below was extracted from the local decompile and compiled scripts. No decompile text or script bodies are copied here.",
        "",
        "## Summary",
        "",
        f"- `.nut` files scanned: {stats['script_file_count']}",
        f"- Binding rows: {stats['binding_count']}",
        f"- Distinct registered names: {stats['distinct_name_count']}",
        f"- Distinct referenced names: {stats['distinct_referenced_name_count']}",
        f"- Distinct registered names with no matching script string: {stats['distinct_unreferenced_name_count']}",
        "",
        "## Registrar families",
        "",
        "Rows are grouped by class and registrar address. Return/parameter categories and instance/static status are decompiler-based inferences, not verified runtime behavior.",
        "",
    ]
    for class_name in sorted(classes):
        out += [f"## {class_name}", "", "| Script name | Native address | Registrar | Return / parameters | Kind | Framework files | Other files | Line |", "|---|---|---|---|---|---:|---:|---:|"]
        for b in classes[class_name]:
            s = b["registrar_shape"]
            shape = f"{s['return']} / {', '.join(s['parameters']) or 'none'}"
            out.append(f"| `{b['script_name']}` | `{b['native_address']}` | `{b['registrar_address']}` | {shape} | {s['binding']} (inferred) | {b['references']['framework_files']} | {b['references']['other_files']} | {b['source_line']} |")
        if not classes[class_name]:
            out.append("| — | — | — | no direct registrations recovered | — | 0 | 0 | — |")
        out.append("")
    out += ["## Ten source spot checks", "", "For each sample, the registrar call was checked at the first line; the script-visible name literal or its local assignment was checked at the second line. Both are line numbers in `totono_Steam.exe.c`.", ""]
    sample = []
    source_lines = decompile_path.read_text(encoding="utf-8", errors="replace").splitlines()
    priority = ["SQApp", "SQSprite", "SQSprite3D", "SQText", "SQSound", "SQModel", "SQModelCharacter", "SQCamera", "SQScene", "SQMATH"]
    for class_name in priority:
        candidates = [b for b in classes.get(class_name, []) if b["registrar_shape"]["binding"] != "class_constructor"]
        verified = []
        for b in candidates:
            idx = b["source_line"] - 1
            if idx < 0 or idx >= len(source_lines) or b["registrar_address"] not in source_lines[idx]:
                continue
            name_line = next((j + 1 for j in range(max(0, idx - 10), min(len(source_lines), idx + 11)) if b["script_name"] in source_lines[j]), None)
            if name_line is not None:
                verified.append((b, name_line))
        if verified:
            sample.append(verified[len(verified) // 2])
    for b, name_line in sample:
        out.append(f"- `{b['class']}.{b['script_name']}`: registrar call line {b['source_line']}, name source line {name_line}, registrar `{b['registrar_address']}`, native `{b['native_address']}`.")
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--decompile", type=Path, default=DEFAULT_DECOMPILE)
    parser.add_argument("--scripts", type=Path, default=DEFAULT_SCRIPTS)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    if not args.decompile.is_file():
        parser.error(f"decompile not found: {args.decompile}")
    if not args.scripts.is_dir():
        parser.error(f"scripts directory not found: {args.scripts}")
    args.output.mkdir(parents=True, exist_ok=True)
    bindings, stats = extract(args.decompile, args.scripts)
    (args.output / "bindings.json").write_text(json.dumps(bindings, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "bindings.md").write_text(render_markdown(bindings, stats, args.decompile), encoding="utf-8")
    (args.output / "summary.json").write_text(json.dumps(stats, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(stats, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
