# Task 05: per-class vtable index of Mware.dll

## Why
Ghidra could not name Mware.dll's methods (no PDB), so the decompile is ~8000 `FUN_xxxxxxxx` functions. We do have RTTI:
`ref/analysis/mware/rtti_classes.tsv` lists 357 classes with their vtable address and base classes. Reading the engine
class by class (CSprite2D, CSpriteMask, CGraphicsPrimitive2D, CSceneManager..., CSound*, CMovie*, CFile*) needs a cheap way to
go from a class to its virtual functions and the functions they call.

## Inputs
- `ref/analysis/mware/rtti_classes.tsv` (class_name, vtable_address, base_classes)
- `ref/analysis/mware/decompile/Mware.dll.c` (all decompiled functions), `functions.tsv` (address, name, size, signature)
- `ref/totono-pack/binaries/Mware.dll` (raw bytes, image base 0x10000000; vtable entries are 32-bit absolute addresses)
- Ghidra project/script setup from task 01 (`scripts/ghidra/`) if you prefer to do it in Ghidra.

## Output (all under `ref/analysis/mware/classes/`, git-ignored)
1. `index.tsv`: `class  vtable_addr  slot_count  base_classes` for every class that has a vtable.
2. For every class (skip `Concurrency::*`, `std::*`, lambdas, CRT): `<ClassName with :: replaced by __>.md` containing
   - the base-class chain,
   - a table: `slot (index and byte offset)  function address  shared with base? (which)  size  one-line signature`,
   - for each function NOT shared with a base class and not trivial (size > 16 bytes): its decompiled body (copy from Mware.dll.c).
   Slot count: read entries from the vtable until an entry is not a code address inside .text.
3. `callers.tsv`: for each `FUN_` function: the addresses of functions that call it directly (`FUN_xxxx` mentioned in another body).
   A plain text scan of `Mware.dll.c` is enough.
4. `strings_by_function.tsv`: `function_address  string_literal` for every string literal referenced in a function body
   (from `strings.tsv` addresses, e.g. shader paths like `shader/sprite/psSprite_Blend_Normal.cso`).

## Done when
- `index.tsv` covers all classes with vtables and, for a spot check, `Mware__CSprite2D.md` lists its 40 or so slots with
  the slots overridden relative to `Mware::CPrimitive2D` marked.
- `strings_by_function.tsv` maps the `shader/*.cso` paths to the functions that load them.
- Report the counts (classes, functions, strings) in your reply.

## Notes
- Do not modify `src/`, `third_party/`, `libtotono/`, or existing task outputs. This is read-only analysis.
- Keep the script in `scripts/` (e.g. `scripts/index_mware_classes.py`) so it can be rerun.
