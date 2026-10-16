# Task 07: run the Mware.dll export + class index pipeline on totono_Steam.exe (the "Diesel" game layer)

## Why
The script-visible engine lives in the exe, not in Mware.dll: `Diesel::CSprite`, `CSpriteList`, `CText`, `CSound`, `CEffectShake`,
`CBackSurfaceTarget`, `CSpriteMask`, `CSpriteRenderTarget`, `CApp`, `CTaskMain`, `CFont`, `CTextTokenizer`, the particle classes, ... sit on
top of Mware (`Mware::CSprite2D` etc. are only low-level primitives). The SQ wrappers (`SQSprite` ...) forward through vtable slots of these
Diesel objects (e.g. SQSprite::move -> `(*(obj+0x100))->vtbl[0x33c/4]`). The exe has RTTI (`Diesel::*::vftable` symbols are in
`ref/totono-pack/decompile/totono_Steam.exe.c`) but no per-class index like task 05 produced for Mware.dll.

## Inputs
- `ref/totono-pack/binaries/totono_Steam.exe` (32-bit PE, image base 0x400000) and the Ghidra project
  `ref/totono-pack/ghidra/Totono.gpr` (check that the exe is analysed in it; if not, import + analyse it headlessly).
- Your earlier work: `scripts/ghidra/ExportAll.java`, `ConfigureAnalysis.java`, `scripts/index_mware_classes.py`, the outputs under
  `ref/analysis/mware/` and `ref/analysis/mware/classes/` (reuse the code, parameterise by binary).

## Output (git-ignored, under `ref/analysis/exe/`)
1. Same set as task 01 for the exe: `functions.tsv`, `strings.tsv`, `rtti_classes.tsv`, `exports.tsv` (may be empty), decompile
   (`decompile/totono_Steam.exe.c` already exists in the pack, but regenerate it so function names/addresses match your TSVs).
2. Same set as task 05: `classes/index.tsv`, one `classes/<Class__Name>.md` per class with a vtable (focus: every `Diesel::*` and
   `Diesel::Squirrel::*`; skip CRT/STL/Concurrency), `callers.tsv`, `strings_by_function.tsv`.
   Slot tables must show, for each Diesel class, the slot index AND byte offset (the SQ wrappers use byte offsets like 0x33c) and which
   slots override a base class.
3. `classes/sqwrappers.tsv`: for every `SQ*` wrapper native we know (`ref/analysis/sqbind/bindings.json`, plus the ones found by
   `FUN_00521d40`-style registrars) the forwarded vtable byte offset(s) of the wrapped Diesel object, i.e. "SQSprite.setBlendMode ->
   Diesel::CSprite slot 0xdc/4 -> function address". Derive it by scanning the wrapper bodies for `(**(code **)(**(int **)(this+0x100) + OFF))`
   patterns and resolving OFF in the vtable of the class stored at that field (the field's class is the Diesel class constructed in the
   wrapper's create function; if ambiguous, list all candidates).
4. Fix the name gap in `ref/analysis/sqbind/bindings.json`: the extractor missed names whose registrar call has no string literal
   (e.g. `SQApp.loadSystemFont`, `SQAction1D.setAction`, many `SQSprite.*`). Recover them by reading the registration functions' disassembly
   (name strings are pushed as immediate pointers) instead of the C text, and write `ref/analysis/sqbind/bindings_v2.json` with the same schema
   plus a `source: "decompile"|"disassembly"` field. Update `scripts/extract_sq_bindings.py` or add `scripts/extract_sq_bindings_asm.py`.

## Done when
- `Diesel__CSprite.md` lists all slots with offsets and sizes, `sqwrappers.tsv` covers at least the SQSprite, SQScene, SQText, SQSound,
  SQApp classes, and `bindings_v2.json` has more names than the 661 of v1 (report the new count and a few examples such as `setAction`).
- Report counts (classes, wrappers resolved, new names recovered).

## Notes
- Read-only analysis; do not touch `src/`, `third_party/`, `libtotono/`, or earlier outputs.
