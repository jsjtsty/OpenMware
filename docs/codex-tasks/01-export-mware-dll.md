# Task 01 — Export Mware.dll from the Ghidra project (headless)

## Goal
Produce text exports of `Mware.dll` (the engine library; 746 exports, D3D11/XAudio2/MediaFoundation/CArchiveNPK)
so the OpenMware engine can be reimplemented from them. Today only `totono_Steam.exe` has been exported.

## Inputs
- Ghidra 12.1.4: `D:\CodeSpace\ghidra_12.1.4_PUBLIC` (use `support\analyzeHeadless.bat`; Java is on PATH).
- Project: `D:\Projects\C++\OpenMware\ref\totono-pack\ghidra\Totono.gpr` (contains `Mware.dll`, `totono_Steam.exe`, `rar`).
- Binary also at `ref\totono-pack\binaries\Mware.dll` (32-bit PE).

## Do
1. **Work on a copy** of the project dir (`ref\analysis\ghidra-copy\`); never modify the original.
2. Check whether `Mware.dll` in the project is already auto-analysed (function count). If not, run auto-analysis
   headless (enable "Decompiler Parameter ID", "Demangler Microsoft", "RTTI/ClassInformer-like" analysers if present).
3. Write a Ghidra script (`scripts/ghidra/ExportAll.java` in the repo) that exports, for `Mware.dll`:
   - `functions.tsv`: address, name (demangled if available), size, calling convention, signature string.
   - `exports.tsv`: ordinal, address, mangled name, demangled name (746 expected).
   - `strings.tsv`: address, string (ASCII + UTF-16).
   - `rtti_classes.tsv`: every class with RTTI (name, vtable address, base classes) — keep namespace (`Mware::...`).
   - `decompile/Mware.dll.c`: C decompile of all functions, in the same style as `ref\totono-pack\decompile\totono_Steam.exe.c`.
   - Also split the decompile into one file per class/namespace when the name is known (`decompile/by_class/<Class>.c`).
4. Output to `D:\Projects\C++\OpenMware\ref\analysis\mware\` (this tree is git-ignored; do **not** copy proprietary
   output anywhere else).

## Done when
- All files above exist; `exports.tsv` has 746 rows; `rtti_classes.tsv` has > 100 rows.
- `ref\analysis\mware\README.md` states: function count, how many decompiled OK / failed, analysis options used, runtime.
- The script is committed under `scripts/ghidra/` and runs end-to-end with one command documented in that README.

## Notes
- Expect this to take a long time (decompiling ~tens of thousands of functions); use a timeout per function and log failures.
- Do not run or modify the game. Do not upload anything.
