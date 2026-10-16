# Task 04: make the HLSL rewrites numerically match the originals

## Why
Task 03 compared opcode sequences, which proves little. I wrote a numeric oracle, `tools/shadercmp.cpp`: it runs the original
`.cso` and the new HLSL on D3D11 WARP with identical random inputs (flat interpolants, random textures t0..t15,
random cbuffers b0..b3) and reports the max relative error. Current result: **117 match, 212 differ**.
Several differences are real semantic mistakes, not rounding. Examples found so far:
- `pssprite_*_transition`: the original samples TransitionMask at (uv.xy / uv.w), then samples TransitionRule at
  `(mask.x, 0)` and multiplies the **whole colour** (rgba) of the source by `(1, 1, 1, rule.w)` ... check the asm, do not guess.
- Some modes do NOT read `Texture_Destination`; they output premultiplied values for the fixed-function blend unit, e.g.
  `pssprite_mul_normal` writes `rgb = tex.rgb*a + (1 - tex.a*vcol.a)`, `pssprite_screen_normal` writes `rgb = a * rgb`.
  Others (`darken`, `difference`, `writealpha`, ...) read t7 and compute in-shader. So the textbook blend formulas in
  `common.hlsli` are wrong for some modes. Derive every mode from its `.asm`. Add a comment per mode saying
  whether it needs a fixed-function blend state (and which: the engine will need that list).

## Build and run the oracle
```bat
scripts\devenv.cmd cl /nologo /EHsc /std:c++17 /O2 tools\shadercmp.cpp /Fobuild\tools\ /Febuild\tools\shadercmp.exe d3d11.lib d3dcompiler.lib
build\tools\shadercmp.exe shaders\hlsl\manifest.tsv shaders\hlsl ref\work\system_x\shader [name-filter]
```
It prints `DIFF <name> max-rel-err <x>` per mismatch and a summary line. Pass a substring to test a subset.
Pixel shaders only (`ps*`); vertex/geometry shaders are not covered by the oracle.

## Goal
Get to **0 differ** for all `ps*` rows (tolerance 1e-4 is built in). Work family by family:
sprite -> primitive -> particle -> posteffect -> scene. Use the `.asm` as the source of truth, rewrite the HLSL in your own words.

## Rules
- If a shader cannot match because the oracle itself is unfair to it (e.g. it depends on SV_Position/undefined derivatives, produces
  NaN/inf for random input), say so per shader in `shaders/hlsl/README.md` with the reason; improving the oracle (e.g. restricting
  the random range for a particular shader) is allowed, but do not weaken the 1e-4 tolerance globally.
- Keep cbuffer layout and register numbers as they are (they already match the originals).
- For vertex shaders (`vssprite`, `vsprimitive`, `vsparticle`, `vsposteffect`, `vsmodelstreamout_*`, `vsupdategbuffer*`, geometry): extend the
  oracle if you can (compare VS output for random vertex input, e.g. through stream-out or by drawing to a float target), otherwise
  review by hand and list them in the README as "not numerically checked".
- Update `scripts/check_shaders.py` output only if useful; the numeric oracle is now the acceptance test.

## Done when
`shadercmp.exe` reports `differ 0` for all pixel shaders (or each remaining one is explained in the README), with a short
table in the README: family, count, status, and the list of modes that need a fixed-function blend state.
