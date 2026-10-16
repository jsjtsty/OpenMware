# Task 06: port the 61 tempo/easing functions of SQAction1D

## Why
`SQAction1D` (script class used all over the framework for timed value animations) evaluates its value through a table of
61 small functions in `totono_Steam.exe`: table `PTR_FUN_005b37f8` (61 x 32-bit pointers, first `0x430bb0`, last `0x4326d0`).
The call is `value = table[tempo](const float* src, const float* dst, const float* duration, const float* timer)`
(see FUN_00520e50 / FUN_00520df0 in `ref/totono-pack/decompile/totono_Steam.exe.c`, args `(obj+0xc4, obj+0xc8, obj+0xb8, obj+0xbc)`).
The functions return an x87 float10 that the caller truncates to float. We need an exact C++ port.

## Inputs
- `ref/totono-pack/decompile/totono_Steam.exe.c` (Ghidra C export; find each function by its `FUN_00xxxxxx` name, addresses are the
  table entries; read the table from `ref/totono-pack/binaries/totono_Steam.exe`, image base 0x400000, .rdata/.data sections).
- Ghidra project `ref/totono-pack/ghidra/Totono.gpr` and the headless setup from task 01 if the decompile is unclear.

## Output
1. `src/script/easing.h` / `src/script/easing.cpp` (namespace `omw`): 
   - `constexpr int kTempoCount = 61;`
   - `float eval_tempo(int tempo, float src, float dst, float duration, float timer);`  (guard `tempo` range like the original does, or clamp)
   - one static function per table entry named after its curve if you can identify it (linear, quad in/out/in-out, cubic, quart, quint,
     sine, expo, circ, back, elastic, bounce, ... ) otherwise `tempo_NN`; keep the table order so the index is the script value.
2. `docs/tempo-table.md`: table `index | original address | curve name | formula | notes` (note any use of globals such as
   a PI constant, or float vs double intermediates).
3. `tests/test_easing.cpp` (doctest is not set up yet: a plain `main` returning non-zero on failure is fine) + CMake target `test_easing`:
   check `f(timer=0)` and `f(timer=duration)` endpoints, monotonicity where expected, and a few hand-computed values.
   Add the target to the root `CMakeLists.txt` at the end of the file (do not touch other targets).

## Rules
- Clean-room rewrite from the decompile: no pasted decompiler output, no `FUN_`/`param_` names in the final code.
- Float semantics matter: the original computes in x87 (extended) and truncates at the end. Use `double` internally unless the
  decompile clearly shows float-only math, then round to float on return. Mention anything that differs in the doc.
- Do not edit anything under `src/` other than the two new files, nor `third_party/`, `libtotono/`.

## Done when
`test_easing` builds and passes, `docs/tempo-table.md` lists all 61 entries, and the reply states any entries you could not identify.
