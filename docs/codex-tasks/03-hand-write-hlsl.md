# Task 03: rewrite the 341 game shaders as HLSL source

## Goal
The original game ships 341 compiled D3D shaders (DXBC, Shader Model 4.0). Rewrite all of them as readable HLSL
source that behaves the same, so the new engine can compile them to SPIR-V/DXIL/MSL later.
This is a clean-room rewrite from the disassembly: do not paste raw disassembly into the output.

## Inputs
- Disassembly: `ref/analysis/shaders/{sprite,particle,posteffect,scene}/*.asm` (one per shader, 341 total).
  Headers list cbuffer fields (names, offsets), texture/sampler bindings (t#, s#), and input/output signatures.
- Original binaries: `ref/work/system_x/shader/**/*.cso` (the reference to compare against).
- Disassembler script: `scripts/disasm_shaders.py <dir-with-cso> <out-dir>` (uses d3dcompiler_47 via ctypes; run with `.venv\Scripts\python.exe`).

## Output (committed to git, not under ref/)
`shaders/hlsl/`
- `sprite.hlsl`, `particle.hlsl`, `primitive.hlsl`, `posteffect/*.hlsl`, `scene/*.hlsl` plus shared `common.hlsli`.
- Do NOT write 341 files. Sprite/particle/primitive shaders are permutations: about 19 blend modes
  (blend, mul, add, sub, screen, overlay, softlight, hardlight, colordodge, colorburn, ... see file names) times
  feature variants (`normal`, `mask`, `transition`, `masktransition`, and `displacementmap*` forms).
  Use one template per family with `#define` switches (`BLEND_MODE_*`, `USE_MASK`, `USE_TRANSITION`, `USE_DISPLACEMENT`).
- `shaders/hlsl/manifest.tsv`: one row per original shader: `original_name  file  defines`
  (for example `pssprite_overlay_masktransition  sprite.hlsl  BLEND_OVERLAY;USE_MASK;USE_TRANSITION`).
- Keep the original cbuffer field names/order/offsets and texture/sampler register numbers exactly (the engine will
  fill them by the same layout), and keep the same vertex input/output semantics.

## Steps
1. Read a handful of `.asm` per family; derive the shared logic (blend function, mask, transition rule, displacement).
2. Write the HLSL templates and manifest.
3. Write `scripts/check_shaders.py`: for each manifest row, compile with `D3DCompile` (d3dcompiler_47 via ctypes,
   target `ps_4_0`/`vs_4_0`/`gs_4_0`, entry `main`, flags: no optimization-skipping, i.e. default O1) and compare with
   the original `.cso`:
   - same cbuffer layout and resource bindings (from reflection or from the disassembly header), same signatures;
   - instruction count within a small tolerance, and ideally the same opcode sequence. Report which shaders differ.
4. Behavioral check where opcode sequences differ: reason about the arithmetic from the disassembly and note any
   deliberate difference in a comment. Blend formulas must match the original exactly (they read the
   `Texture_Destination` at t7 and compute the blend in the shader; the alpha discard threshold is 1/255, `l(0.003922)`).

## Done when
- `scripts/check_shaders.py` compiles all 341 manifest rows without error.
- Report in the script output: N identical opcode sequences, M different (list the different ones with a one-line reason).
- Scene shaders (15) are lowest priority; do them last, and say so if skipped.

## Notes
- Windows only for the checker; the HLSL itself must be plain SM4-compatible and avoid D3D11-specific features.
- Do not touch `src/`, `third_party/`, or `libtotono/`.
