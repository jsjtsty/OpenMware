# Task 02 — Extract the Squirrel native-binding table from the exe decompile

## Goal
Generate a machine-readable inventory of everything the engine exposes to Squirrel scripts, so we can write the
OpenMware bindings from it. Output: `ref\analysis\sqbind\bindings.json` plus a human-readable `bindings.md`.

## Inputs
- `ref\totono-pack\decompile\totono_Steam.exe.c` (Ghidra export, ~400k lines). Class registration pattern: `FUN_004c4060`
  (SQApp) at line ~170815 — a long run of calls like
  `FUN_004c4720(FUN_004c2190,"getMouseButtonStateL",local_40,local_3c);`
  (registrar function, native function pointer, script-visible name). The registrar function address tells the
  signature shape (it is a template instantiation: number/types of arguments).
- Script side: compiled scripts in `D:\Games\君与彼女与彼女之恋\script.npk~\media\script\` (2129 `.nut`, header `SCRP`,
  Squirrel 2.2 bytecode). A string-constant extractor is enough; do not try to decompile bytecode.
- Class names to cover: SQApp SQSprite SQSprite3D SQText SQSound SQSoundDevice SQModel SQModelCharacter SQBone SQJoint
  SQCamera SQParticle SQPhysics SQRigidBody SQPointLight SQNull3D SQShadeMask SQSpriteMask SQSpriteRenderTarget
  SQDisplacementMap SQProcess SQAction1D SQScene SQFile SQDes SQLine, plus the `SQMATH_*` free functions and
  Mware::Vector2D/Vector3D/Rect2D/ColorF.

## Do
1. Find every class-registration function (start from `FUN_004c4060`; find the others by the same shape and by the
   class-name strings). For each registered name record: class, script name, native function address, registrar address,
   and the **registrar's template shape** (group registrar addresses and describe what each seems to take/return:
   int/float/string/bool/object/void, instance vs static). State clearly which parts are inferred.
2. For each name, compute from the scripts: number of `.nut` files whose string constants contain it, split into
   `framework/` vs everything else. (Expected: 670 distinct names, ~585 referenced by scripts.)
3. Flag names registered but never referenced (≈85) — these can be stubbed.
4. Emit `bindings.json` (schema documented at top of `bindings.md`) and `bindings.md` (one table per class).
5. Put the extraction script in the repo as `scripts/extract_sq_bindings.py`, runnable with
   `.venv\Scripts\python.exe -I scripts/extract_sq_bindings.py` (uv venv at repo root; add deps with `uv pip install`).

## Done when
- All 28ish classes found; per-class counts printed and reconciled with the 670 distinct-name total.
- A spot check of 10 random functions (pick across classes) against the decompile is written into `bindings.md`
  with the line numbers you checked.
- No proprietary decompile text is copied into the repo — only names/addresses/counts in `ref\analysis\` (ignored).
