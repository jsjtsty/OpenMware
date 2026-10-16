#!/usr/bin/env python3
"""Index an existing Ghidra Mware export without changing the inputs.

Only this script and its output directory are written. No Ghidra installation or
third-party Python packages are needed. All addresses are original image VAs.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import re
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMENTS = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
C_STRING = r'(?:u8|[LuU])?"(?:\\.|[^"\\])*"'
TOKENS = re.compile(r"/\*.*?\*/|//[^\n]*|" + C_STRING + r"|'(?:\\.|[^'\\])*'|[{}]", re.S)
STRINGS = re.compile(C_STRING, re.S)
CALL = re.compile(r"\bFUN_([0-9a-fA-F]{8})\s*\(")
DATA_SYMBOL = re.compile(r"\b((?:_?DAT|s_[\w$]*|u_[\w$]*|PTR_[\w$]*)_([0-9a-fA-F]{8}))\b")
HEX_ADDRESS = re.compile(r"\b0x([0-9a-fA-F]{8})\b")


def va(value: int) -> str:
    return f"0x{value:08x}"


def read_tsv(path: Path) -> list[dict[str, str]]:
    # The Ghidra exporter writes plain tab-separated text, not CSV-quoted fields.
    with path.open(encoding="utf-8-sig", newline="") as stream:
        header = stream.readline().rstrip("\r\n").split("\t")
        rows = []
        for line in stream:
            cells = line.rstrip("\r\n").split("\t", len(header) - 1)
            if len(cells) != len(header):
                raise ValueError(f"Malformed TSV row in {path}: {line[:80]!r}")
            rows.append(dict(zip(header, cells)))
        return rows


def write_tsv(path: Path, columns: list[str], rows) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(columns)
        writer.writerows(rows)


@dataclass
class Section:
    name: str
    rva: int
    virtual_size: int
    raw: int
    raw_size: int


class PE32:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError("Input DLL is not a PE image")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        machine, count = struct.unpack_from("<HH", self.data, pe + 4)
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        optional = pe + 24
        if machine != 0x14C or struct.unpack_from("<H", self.data, optional)[0] != 0x10B:
            raise ValueError("Expected an x86 PE32 image with 32-bit vtable entries")
        self.image_base = struct.unpack_from("<I", self.data, optional + 28)[0]
        self.sections = []
        for i in range(count):
            start = optional + optional_size + i * 40
            name = self.data[start:start + 8].split(b"\0", 1)[0].decode("ascii")
            virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", self.data, start + 8)
            self.sections.append(Section(name, rva, virtual_size, raw, raw_size))
        self.text = next(section for section in self.sections if section.name == ".text")

    def offset(self, address: int, size: int = 1) -> int:
        rva = address - self.image_base
        for section in self.sections:
            delta = rva - section.rva
            if 0 <= delta and delta + size <= section.raw_size:
                offset = section.raw + delta
                if offset + size <= len(self.data):
                    return offset
        raise ValueError(f"Address {va(address)} is not backed by DLL bytes")

    def u32(self, address: int) -> int:
        return struct.unpack_from("<I", self.data, self.offset(address, 4))[0]

    def i32(self, address: int) -> int:
        return struct.unpack_from("<i", self.data, self.offset(address, 4))[0]

    def c_string(self, address: int) -> str:
        start = self.offset(address)
        end = self.data.find(b"\0", start, start + 4096)
        if end < 0:
            raise ValueError(f"Unterminated string at {va(address)}")
        return self.data[start:end].decode("ascii")

    def is_code(self, address: int) -> bool:
        begin = self.image_base + self.text.rva
        return begin <= address < begin + self.text.virtual_size

    def vtable(self, address: int) -> tuple[list[int], int | None]:
        entries = []
        while True:
            try:
                target = self.u32(address + len(entries) * 4)
            except ValueError:
                return entries, None
            if not self.is_code(target):
                return entries, target
            entries.append(target)


@dataclass
class Function:
    address: int
    name: str
    size: int
    signature: str
    source: str = ""
    body: str = ""
    line: int = 0
    calls: set[int] = field(default_factory=set)
    literals: set[str] = field(default_factory=set)


def attach_decompilation(text: str, functions: list[Function], failed: set[int]) -> int:
    """ExportAll.java emits internal functions in functions.tsv order, excluding failures.

    Match that order, but fail on count/FUN-address disagreement rather than
    silently assigning a body to the wrong overloaded/demangled function.
    The lexical scan ignores braces in comments, string and character literals.
    """
    blocks = []
    depth, boundary, opening = 0, 0, 0
    for match in TOKENS.finditer(text):
        token = match.group()
        if token == "{":
            if depth == 0:
                opening = match.start()
            depth += 1
        elif token == "}":
            depth -= 1
            if depth < 0:
                raise ValueError("Unbalanced top-level closing brace in C export")
            if depth == 0:
                blocks.append((boundary, opening, match.end()))
                boundary = match.end()
    if depth or COMMENTS.sub("", text[boundary:]).strip():
        raise ValueError("Incomplete/unrecognized trailing content in C export")
    expected = [f for f in functions if f.size > 0 and f.address not in failed]
    if len(expected) != len(blocks):
        raise ValueError(f"C export has {len(blocks)} bodies, expected {len(expected)}; order mapping is unsafe")
    newlines = [m.start() for m in re.finditer("\n", text)]
    for function, (start, opening, end) in zip(expected, blocks):
        header = COMMENTS.sub(" ", text[start:opening])
        if function.name.startswith("FUN_") and not re.search(r"\b" + re.escape(function.name) + r"\s*\(", header):
            raise ValueError(f"Function/body order disagreement at {va(function.address)}: {header[-160:]}")
        start += len(text[start:opening]) - len(text[start:opening].lstrip())
        function.source = text[start:end]
        function.body = text[opening + 1:end - 1]
        function.line = bisect.bisect_left(newlines, start) + 1
    return len(blocks)


def decode_literal(token: str) -> str:
    value = token[token.index('"') + 1:-1]
    escapes = {"n": "\n", "r": "\r", "t": "\t", "a": "\a", "b": "\b", "f": "\f", "v": "\v", "\\": "\\", '"': '"', "'": "'", "?": "?"}

    def replace(match):
        code = match.group(1)
        if code.startswith("x"):
            return chr(int(code[1:], 16) & 0xFF)
        if code.startswith(("u", "U")):
            return chr(int(code[1:], 16))
        if code[0] in "01234567":
            return chr(int(code, 8))
        if code == "\n":
            return ""
        return escapes.get(code, "\\" + code)

    return re.sub(r"\\(x[0-9a-fA-F]+|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|[0-7]{1,3}|\n|.)", replace, value)


def clean_string(value: str) -> str:
    # Match ExportAll.clean(): preserve backslashes but flatten literal controls for TSV.
    return value.replace("\t", " ").replace("\r", " ").replace("\n", " ").replace("\0", r"\0")


def index_references(functions: list[Function], strings: dict[int, str], image: PE32):
    by_value = set(strings.values())
    callers = defaultdict(set)
    literal_only = set()
    for function in functions:
        if not function.source:
            continue
        # Remove comments before collecting literals; remove both literals and comments for calls/addresses.
        literals = []
        code_parts, previous = [], 0
        for token in TOKENS.finditer(function.body):
            word = token.group()
            if word.startswith(("/*", "//")) or word.startswith("'") or STRINGS.fullmatch(word):
                code_parts.append(function.body[previous:token.start()])
                code_parts.append(" " * len(word))
                previous = token.end()
                if STRINGS.fullmatch(word):
                    if literals and not COMMENTS.sub("", function.body[literals[-1][1]:token.start()]).strip():
                        value, _ = literals.pop()
                        literals.append((value + decode_literal(word), token.end()))
                    else:
                        literals.append((decode_literal(word), token.end()))
        code_parts.append(function.body[previous:])
        code = "".join(code_parts)
        function.calls = {int(match.group(1), 16) for match in CALL.finditer(code)}
        for target in function.calls:
            callers[target].add(function.address)
        for value, _ in literals:
            value = clean_string(value)
            if value not in by_value:
                # Some narrow literals are escaped UTF-8 bytes in the C export.
                try:
                    utf8 = value.encode("latin1").decode("utf-8")
                    if utf8 in by_value:
                        value = utf8
                except (UnicodeError, ValueError):
                    pass
            function.literals.add(value)
            if value not in by_value:
                literal_only.add((function.address, value))
        addresses = {int(match.group(2), 16) for match in DATA_SYMBOL.finditer(code)}
        addresses.update(int(match.group(1), 16) for match in HEX_ADDRESS.finditer(code))
        for address in addresses:
            if address in strings:
                function.literals.add(strings[address])
        # PTR_s_* labels are pointer objects, not string addresses themselves.
        for match in DATA_SYMBOL.finditer(code):
            if match.group(1).startswith("PTR_s_"):
                try:
                    pointee = image.u32(int(match.group(2), 16))
                    if pointee in strings:
                        function.literals.add(strings[pointee])
                except ValueError:
                    pass
    return callers, literal_only


@dataclass
class ClassInfo:
    name: str
    address: int | None
    bases_text: str
    slots: list[int] = field(default_factory=list)
    stop: int | None = None
    # (name, depth, subobject offset); MSVC RTTI gives a preorder tree.
    hierarchy: list[tuple[str, int, int]] = field(default_factory=list)
    rtti_error: str = ""


def type_name(image: PE32, address: int, names: dict[int, str]) -> str:
    if address in names:
        return names[address]
    decorated = image.c_string(address + 8)
    if not decorated.startswith((".?AV", ".?AU")) or "@@" not in decorated:
        raise ValueError(f"Unrecognized RTTI descriptor {va(address)}")
    name = "::".join(reversed([part for part in decorated[4:decorated.index("@@")].split("@") if part]))
    names[address] = name
    return name


def attach_hierarchy(info: ClassInfo, image: PE32, names: dict[int, str]) -> None:
    if info.address is None:
        return
    try:
        locator = image.u32(info.address - 4)
        root_type = image.u32(locator + 12)
        if type_name(image, root_type, names) != info.name:
            raise ValueError("vtable locator type does not match class row")
        descriptor = image.u32(locator + 16)
        count, array = image.u32(descriptor + 8), image.u32(descriptor + 12)
        if not 1 <= count <= 4096:
            raise ValueError("invalid RTTI hierarchy size")
        ends = []
        hierarchy = []
        for i in range(count):
            base = image.u32(array + 4 * i)
            contained = image.u32(base + 4)
            if i + contained >= count:
                raise ValueError("invalid RTTI subtree span")
            while ends and i > ends[-1]:
                ends.pop()
            name = type_name(image, image.u32(base), names)
            hierarchy.append((name, len(ends), image.i32(base + 8)))
            if contained:
                ends.append(i + contained)
        info.hierarchy = hierarchy
    except (ValueError, UnicodeError) as error:
        info.rtti_error = str(error)


def split_bases(value: str) -> list[str]:
    # Do not split commas inside template arguments.
    result, depth, start = [], 0, 0
    for i, char in enumerate(value):
        depth += (char == "<") - (char == ">")
        if char == "," and depth == 0:
            result.append(value[start:i]); start = i + 1
    if value[start:]:
        result.append(value[start:])
    return result


def skipped(name: str) -> bool:
    return (name.startswith(("std::", "Concurrency::", "V__ExceptionPtr::", "X$0A::"))
            or "lambda_" in name or name == "type_info")


def filename(name: str) -> str:
    stem = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name.replace("::", "__")).rstrip(". ")
    if len(stem) > 180:
        stem = stem[:165] + "_" + hashlib.sha256(name.encode()).hexdigest()[:12]
    return stem + ".md"


def cell(text: str) -> str:
    return " ".join(text.split()).replace("|", r"\|").replace("`", "'")


def class_markdown(info: ClassInfo, classes: dict[str, ClassInfo], functions: dict[int, Function], filenames: dict[str, str]) -> tuple[str, set[int]]:
    ancestors = [name for name, _, _ in info.hierarchy if name != info.name]
    if not ancestors:
        ancestors = [name for name in split_bases(info.bases_text) if name != info.name]
    ancestors = list(dict.fromkeys(ancestors))
    base_slots = {name: classes[name].slots for name in ancestors if name in classes}
    direct = [name for name, depth, offset in info.hierarchy if depth == 1 and offset == 0]
    primary = direct[0] if direct else None
    lines = [f"# {info.name}", "", f"Vtable: `{va(info.address) if info.address is not None else 'unknown'}`; slots: **{len(info.slots)}**.", ""]
    if info.hierarchy:
        lines.extend(["## Base-class hierarchy", "", "MSVC RTTI preorder reconstructed using each base descriptor's subtree span; offsets are relative to the complete object.", ""])
        for name, depth, offset in info.hierarchy:
            label = f"[{name}]({filenames[name]})" if name in filenames and name != info.name else f"`{name}`"
            lines.append("  " * depth + f"- {label} (offset {offset:+#x})")
    else:
        lines.extend(["## Base-class information", "", "RTTI export ancestor list (includes self; no linear/direct-base chain is inferred):", "", f"`{info.bases_text or 'unresolved'}`"])
        if info.rtti_error:
            lines.append(f"\nHierarchy warning: {info.rtti_error}.")
    if info.address is None:
        lines.extend(["", "No vtable address was resolved in rtti_classes.tsv. No slots or method bodies are inferred.", ""])
        return "\n".join(lines), set()
    lines.extend(["", f"Scan stops at `{va(info.address + 4 * len(info.slots))}`: next value `{va(info.stop) if info.stop is not None else 'unmapped'}` is outside `.text`.", ""])
    if primary:
        lines.append(f"Primary-base slot comparison: `{primary}` (first zero-offset direct base).")
    lines.extend(["", "## Virtual slots", "", "Slot equality is literal function-address equality, not a claim about thunk equivalence. Overrides are compared at the same slot; a different-slot reuse is labeled explicitly.", "", "| Slot / byte offset | Function address | Shared with base? | Primary-base comparison | Size | One-line signature |", "| --- | --- | --- | --- | ---: | --- |"])
    ancestor_functions = {address for entries in base_slots.values() for address in entries}
    copied = {address for address in info.slots if address not in ancestor_functions and address in functions and functions[address].size > 16 and functions[address].source}
    for slot, address in enumerate(info.slots):
        function = functions.get(address)
        shared = [name for name, slots in base_slots.items() if len(slots) > slot and slots[slot] == address]
        if shared:
            reuse = "yes: " + "; ".join(shared)
        else:
            shifted = [f"{name} slot {slots.index(address)}" for name, slots in base_slots.items() if address in slots]
            reuse = "yes (different slot): " + "; ".join(shifted) if shifted else "no resolved base match"
        comparison = "unresolved"
        if primary and primary in classes and classes[primary].address is not None:
            parent = classes[primary].slots
            comparison = "new slot" if slot >= len(parent) else ("inherited" if parent[slot] == address else f"override vs {primary}")
        elif not ancestors:
            comparison = "root / no listed base"
        address_label = f"[{va(address)}](#function-{address:08x})" if address in copied else f"`{va(address)}`"
        lines.append(f"| {slot} / +0x{slot * 4:03x} | {address_label} | {cell(reuse)} | {cell(comparison)} | {function.size if function else '?'} | `{cell(function.signature) if function else 'not present in functions.tsv'}` |")
    lines.extend(["", "## Class-specific nontrivial bodies", "", "Only functions larger than 16 bytes whose address does not occur in a resolved ancestor vtable are copied. Each address appears once here, even if several slots use it. Source is copied verbatim from the existing Ghidra export, with no inferred method names.", ""])
    for address in dict.fromkeys(info.slots):
        function = functions.get(address)
        if address in copied:
            slots = [str(i) for i, target in enumerate(info.slots) if target == address]
            lines.extend([f'<a id="function-{address:08x}"></a>', f"### {va(address)} — {function.name}", "", f"Slots: {', '.join(slots)}. Size: {function.size} bytes. Source: `../decompile/Mware.dll.c`, line {function.line}.", ""])
            if function.calls:
                lines.extend(["Direct `FUN_` callees (text scan): " + ", ".join(f"`FUN_{target:08x}`" for target in sorted(function.calls)) + ".", ""])
            longest = max([len(m.group()) for m in re.finditer(r"`+", function.source)] or [0])
            fence = "`" * max(3, longest + 1)
            lines.extend([fence + "c", function.source, fence, ""])
        elif address not in ancestor_functions and (function is None or function.size > 16 and not function.source):
            lines.extend([f"Body unavailable for `{va(address)}`: no matching successful decompilation.", ""])
    if not copied:
        lines.extend(["No class-specific nontrivial bodies were available to copy.", ""])
    return "\n".join(lines), copied


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analysis-root", type=Path, default=ROOT / "ref/analysis/mware")
    parser.add_argument("--dll", type=Path, default=ROOT / "ref/totono-pack/binaries/Mware.dll")
    parser.add_argument("--out", type=Path, help="defaults to ANALYSIS_ROOT/classes")
    args = parser.parse_args()
    analysis = args.analysis_root.resolve()
    output = (args.out or analysis / "classes").resolve()
    inputs = [analysis / "rtti_classes.tsv", analysis / "functions.tsv", analysis / "strings.tsv", analysis / "decompile/Mware.dll.c", analysis / "decompile/failures.tsv", args.dll.resolve()]
    # An output directory must not alias the source export directory.
    if output == analysis or any(path == output or output in path.parents for path in inputs):
        parser.error("output directory would overwrite existing analysis inputs")
    image = PE32(args.dll)
    functions = [Function(int(row["address"], 16), row["name"], int(row["size"]), row["signature"])
                 for row in read_tsv(inputs[1]) if not row["address"].startswith("EXTERNAL:")]
    by_address = {f.address: f for f in functions}
    if len(by_address) != len(functions):
        raise ValueError("Duplicate function entry addresses")
    failures = {int(row["address"], 16) for row in read_tsv(inputs[4])}
    parsed = attach_decompilation(inputs[3].read_text(encoding="utf-8-sig"), functions, failures)
    strings = {int(row["address"], 16): clean_string(row["string"]) for row in read_tsv(inputs[2])}
    callers, literal_only = index_references(functions, strings, image)
    classes = {}
    for row in read_tsv(inputs[0]):
        address = None if row["vtable_address"] == "unknown" else int(row["vtable_address"], 16)
        info = ClassInfo(row["class_name"], address, row["base_classes"])
        if address is not None:
            info.slots, info.stop = image.vtable(address)
        classes[info.name] = info
    type_names = {}
    for info in classes.values():
        if info.address is not None:
            try:
                type_names[image.u32(image.u32(info.address - 4) + 12)] = info.name
            except ValueError:
                pass
    for info in classes.values():
        attach_hierarchy(info, image, type_names)
    filenames = {name: filename(name) for name in classes if not skipped(name)}
    if len({name.casefold() for name in filenames.values()}) != len(filenames):
        raise ValueError("Class names collide after Windows filename sanitization")
    output.mkdir(parents=True, exist_ok=True)
    write_tsv(output / "index.tsv", ["class", "vtable_addr", "slot_count", "base_classes"],
              ((c.name, va(c.address), len(c.slots), c.bases_text) for c in classes.values() if c.address is not None))
    body_addresses = set()
    for name, filename_ in filenames.items():
        markdown, copied = class_markdown(classes[name], classes, by_address, filenames)
        (output / filename_).write_text(markdown, encoding="utf-8")
        body_addresses.update(copied)
    fun_targets = {f.address for f in functions if f.name.startswith("FUN_")} | set(callers)
    write_tsv(output / "callers.tsv", ["function_address", "function_name", "caller_count", "caller_addresses"],
              ((va(address), by_address[address].name if address in by_address else f"FUN_{address:08x}",
                len(callers[address]), ";".join(va(caller) for caller in sorted(callers[address]))) for address in sorted(fun_targets)))
    pairs = sorted((f.address, literal) for f in functions for literal in f.literals)
    write_tsv(output / "strings_by_function.tsv", ["function_address", "string_literal"], ((va(address), literal) for address, literal in pairs))
    stats = {
        "classes": len(classes), "classes_with_vtables": sum(c.address is not None for c in classes.values()),
        "class_documents": len(filenames), "class_documents_with_vtables": sum(classes[n].address is not None for n in filenames),
        "vtable_slots": sum(len(c.slots) for c in classes.values()),
        "unique_vtable_functions": len({a for c in classes.values() for a in c.slots}),
        "unique_class_body_functions": len(body_addresses), "internal_functions": len(functions),
        "decompiled_functions": parsed, "failed_decompilations": len(failures), "caller_index_functions": len(fun_targets),
        "direct_call_edges": sum(len(s) for s in callers.values()), "string_catalog_addresses": len(strings),
        "functions_with_strings": sum(bool(f.literals) for f in functions), "function_string_pairs": len(pairs),
        "unique_referenced_strings": len({literal for _, literal in pairs}), "inline_pairs_not_in_string_catalog": len(literal_only),
        "shader_string_pairs": sum(bool(re.search(r"shader[/\\].*\.cso$", literal, re.I)) for _, literal in pairs),
        "unique_shader_paths": len({literal for _, literal in pairs if re.search(r"shader[/\\].*\.cso$", literal, re.I)}),
        "rtti_hierarchy_warnings": {c.name: c.rtti_error for c in classes.values() if c.rtti_error},
        "unknown_vtable_targets": sorted({va(a) for c in classes.values() for a in c.slots if a not in by_address}),
        "input_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
    }
    (output / "stats.json").write_text(json.dumps(stats, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (output / "README.md").write_text(
        "# Mware class and function index\n\n"
        f"Generated by `scripts/index_mware_classes.py` from {stats['classes']} RTTI classes: "
        f"{stats['classes_with_vtables']} resolved vtables, {len(filenames)} class documents "
        f"({stats['class_documents_with_vtables']} with vtables). Compiler/CRT/lambda classes remain in index.tsv but their Markdown is omitted.\n\n"
        f"Mapped {parsed} successful decompilations in validated exporter order; {len(failures)} known failures have no body. "
        f"The caller index covers {len(fun_targets)} FUN_ targets and {stats['direct_call_edges']} direct call edges. "
        f"Strings: {len(pairs)} function/string pairs, {stats['unique_referenced_strings']} unique values, "
        f"{stats['functions_with_strings']} functions; {stats['unique_shader_paths']} unique shader paths.\n\n"
        "## Method and limitations\n\n"
        "- PE section headers map original VAs to file bytes. Entries are 32-bit absolute addresses. Scan each provided vtable until the first pointer outside .text; do not stop just because an address lacks function metadata.\n"
        "- Only the single vtable address supplied per class is indexed. Secondary vtables are not discovered. Unknown vtable addresses are not guessed; applicable class documents say so.\n"
        "- MSVC RTTI base-descriptor subtree spans reconstruct branching hierarchies. The raw ancestor list in index.tsv includes self. Shared code uses exact addresses, not speculative thunk equivalence. Primary-base overrides compare the same slot of the first zero-offset direct base; unresolved base tables are labeled.\n"
        "- ExportAll.java emits functions in functions.tsv order, omitting failures. The parser balances braces while excluding comments/literals, checks the exact body count, and validates every FUN_ header against its mapped address. Duplicate named methods are therefore not matched by name alone.\n"
        "- callers.tsv records syntactic FUN_xxxxxxxx(...) calls in bodies, ignoring comments and strings. Indirect calls, callback references and calls using demangled names are not inferred. Targets with no callers are retained.\n"
        "- strings_by_function.tsv unions actual C string literals with exact strings.tsv address references (data symbols/numeric VAs); PTR_s_* pointer objects are dereferenced once. Adjacent C literals and escapes are decoded; control whitespace is flattened as in the source TSV. Inline literals absent from the catalogue are retained. This is text-reference indexing, not data-flow or indirect-reference recovery.\n"
        "- Copied bodies and signatures are original Ghidra output, including its errors/warnings. No method names are invented. Functions <=16 bytes and functions reused by resolved base vtables are not duplicated in body sections.\n\n"
        "Rerun: `.venv\\Scripts\\python.exe scripts\\index_mware_classes.py` from the project root. "
        "Only this output directory is rewritten. Detailed counts, warnings and input SHA-256 hashes are in stats.json.\n",
        encoding="utf-8")
    print(f"Classes: {stats['classes']} RTTI, {stats['classes_with_vtables']} vtables, {stats['class_documents']} documents")
    print(f"Functions: {parsed} decompiled, {len(fun_targets)} FUN_ caller rows, {stats['direct_call_edges']} direct call edges")
    print(f"Strings: {len(pairs)} function/value pairs, {stats['unique_referenced_strings']} unique, {stats['functions_with_strings']} functions")
    print(f"Shaders: {stats['unique_shader_paths']} unique paths, {stats['shader_string_pairs']} function/path pairs")
    print(f"Output: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
