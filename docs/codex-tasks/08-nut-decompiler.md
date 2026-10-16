# Task 08: a readable decompiler for the game's compiled Squirrel scripts

## Why
The whole game logic (title, menus, save/load, scenario scripts, the N2Framework) is 2129 compiled `.nut` files. We read them with `nutdis`
(register-level disassembly, see `docs/script-notes.md`), which is slow and error-prone for the larger functions. A pseudo-Squirrel
decompiler would make reverse engineering the engine contract (which natives are called, with which arguments and what results they
must return) several times faster.

## Inputs
- `build/vcpkg/nutdis.exe FILE.nut` (source `tools/nutdis.cpp`) prints every function with `index L<line> OPNAME a0=.. a1=.. a2=.. a3=..`
  plus literals as comments. Opcode semantics are those of Squirrel 2.2.4 (`third_party/squirrel2/squirrel/sqvm.cpp`, `sqopcodes.h`,
  `sqfuncstate.cpp` for how the compiler emits each construct; `sqcompiler.cpp` is the best reference for patterns such as `for`/`foreach`/
  `switch`/`&&`/`||`/ternary/`try`).
- The extracted scripts: `ref/work/script_x/media/script/**.nut` (git-ignored), disassembly cache `ref/work/dis/*.dis` (one per file,
  produced by running nutdis over everything; regenerate with a loop if missing).
- Constants: `ref/analysis/render/script_constants.json` (names for integer values such as BLEND_MODE_*, KEY_CODE_*).

## Output
`scripts/nutdec.py` (run with `.venv\Scripts\python.exe -I scripts/nutdec.py FILE.nut > out.nut.txt`, calls nutdis itself or reads
a `.dis`), writing pseudo-Squirrel source:
- classes, functions with parameter names (`"this" "x" ...` are in the disassembly header), locals named from the local-variable info if
  available, otherwise `v<reg>`;
- expressions rebuilt from register traffic (GETK/GET chains -> `a.b.c`, PREPCALLK/PREPCALL+CALL -> calls, ARITH/CMP/EQ/NE/BITW/NOT/NEG...,
  NEWTABLE/NEWSLOTA -> table literals, NEWARRAY/APPENDARRAY -> array literals, CLOSURE -> nested function, DLOAD pairs);
- structured control flow from JMP/JZ/JNZ/FOREACH/POSTFOREACH/PUSHTRAP: `if/else`, `while`, `for`, `foreach (k, v in x)`, `switch` where
  detectable, `break/continue`, `try/catch`, `&&`/`||`/`?:`;
- every statement annotated with its source line from LINE info as a trailing `// L123` comment (only when it changes);
- anything the lifter cannot structure falls back to a labelled region with the raw `OP a0 a1 a2 a3` lines so no information is lost.
Also a batch mode: `scripts/nutdec.py --all OUTDIR` decompiles every script into `ref/work/dec/<path>.nut.txt` (same dir layout) and prints
a summary: files ok, functions with fallback regions (count and percentage).

## Quality bar
- Must handle the four big framework files without crashing: `framework/command/ccommand.nut`, `system/sys_function.nut`,
  `framework/nut/cnutsprite.nut`, `system/sys_titlelogo.nut`.
- Spot-check against the disassembly for correctness on these functions: `TitleLogoBase`, `StartTitleSelect` (sys_titlelogo.nut),
  `CreateClipSprite` (ccommand.nut), `CheckEnd` and `CallFile` (system/boot.nut). Include the results of the spot checks (short list)
  in your reply.
- Target: >= 95% of functions with no fallback region.

## Rules
- Python, standard library only. Do not modify `src/`, `third_party/`, `tools/nutdis.cpp` (you may add flags to nutdis if really needed:
  prefer a separate output format like `--json` in a new file `tools/nutdis_json.cpp` and a CMake target `nutdis_json`).
- Keep the decompiler deterministic and fast (all 2129 files in a couple of minutes).

## Done when
`--all` runs over every script, the summary shows >= 95% clean functions, and the spot-check functions read like sensible code.
