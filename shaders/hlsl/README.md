# HLSL numeric reconstruction report

Task 04 acceptance uses `tools/shadercmp.cpp`, which runs original DXBC and reconstructed HLSL on the same D3D11 WARP
device. The tolerance remains `1e-4`. Latest result: **match 329, differ 0, compile-failed 0**. No pixel shader is excluded.
All 341 manifest entries also compile through `scripts/check_shaders.py`; opcode equality is diagnostic only.

| Pixel family | Count | Numeric status |
| --- | ---: | --- |
| Sprite | 152 | Match |
| Primitive | 76 | Match |
| Particle | 58 | Match |
| Posteffect | 37 | Match |
| Scene | 8 | Match, including all G-buffer render targets |
| Total | 329 | Match; 0 differences, 0 compile failures |

## Reproduce

From the project root on Windows:

```bat
scripts\devenv.cmd cl /nologo /EHsc /std:c++17 /O2 tools\shadercmp.cpp /Fobuild\tools\ /Febuild\tools\shadercmp.exe d3d11.lib d3dcompiler.lib
build\tools\shadercmp.exe shaders\hlsl\manifest.tsv shaders\hlsl ref\work\system_x\shader
.venv\Scripts\python.exe scripts\check_shaders.py
```

The optional final oracle argument filters names by substring, such as `psparticle` or `psgaussianblur`.
`manifest.tsv` qualifies the duplicated vertex names as `sprite/vssprite`, `scene/vssprite`, etc.

The numeric comparison uses 24 deterministic seeds, random 8x8 RGBA float textures at t0..t15, flat random interpolants,
and four 1 KiB constant buffers (covering the largest pixel buffer, 512 bytes). Cbuffer values cover [0,1] and [-1,2].
It compares every component of eight float render targets. Samplers s1/s3 use point filtering; the other slots use linear
filtering, all with clamp addressing. Even seeds force opaque source/vertex alpha so G-buffer writes are actually
executed; particle seeds alternate the soft-depth switch, and selected seeds use zero/full blur masks. Both sides receive
identical inputs. Equal NaNs and same-sign infinities compare equal; finite/nonfinite differences fail. This is sampled
numeric evidence, not a proof for every input or sampler state. Constant interpolants do not test varying derivatives.

Notable corrections include blue-channel transition rules, the early-discard exception for
`pssprite_blend_displacementmaptransition`, fixed-function mul/screen preprocessing, the original power-curve softlight,
non-quantized hardmix, pre-fade particle coverage, Gaussian matrix row/column conventions and symmetric tap weights,
DOF tap-dependent offsets, HSV/value ordering, tone-map channel statistics, and the original scene lighting/SSS equations.
G-buffer basis normalization includes w where the reference uses dp4; all MRT packing is compared.

## Blend states required by the outputs

The states below are **equivalent RGB compositing states derived from the shader equations**. Shader DXBC does not encode
the engine's actual D3D blend objects, separate alpha factors, or write masks; those are not claimed as recovered runtime
state. `SRC_ALPHA / INV_SRC_ALPHA` below means ADD with those source/destination RGB factors. For ordinary coverage alpha,
use ONE / INV_SRC_ALPHA separately for alpha. The numeric oracle disables blending to compare raw shader outputs.

| Mode | Reads t7 | Required equivalent RGB state | Output detail |
| --- | --- | --- | --- |
| blend | No | SRC_ALPHA / INV_SRC_ALPHA | Straight source colour |
| mul | No | DEST_COLOR / ZERO (or ZERO / SRC_COLOR) | Emits `1-a+a*s` |
| screen | No | ONE / INV_SRC_COLOR | Emits `a*s` |
| add (particle only) | No | SRC_ALPHA / ONE | Straight source RGB, additive coverage |
| colorburn | Yes | SRC_ALPHA / INV_SRC_ALPHA | Unclamped burn result |
| colordodge | Yes | SRC_ALPHA / INV_SRC_ALPHA | Unclamped dodge result |
| darken | Yes | SRC_ALPHA / INV_SRC_ALPHA | Component minimum |
| difference | Yes | SRC_ALPHA / INV_SRC_ALPHA | Absolute difference |
| exclusion | Yes | SRC_ALPHA / INV_SRC_ALPHA | Sum minus twice the product |
| hardlight | Yes | SRC_ALPHA / INV_SRC_ALPHA | Source selects multiply/screen branch |
| hardmix | Yes | SRC_ALPHA / INV_SRC_ALPHA | Keeps the sum when >=1, otherwise zero |
| lighten | Yes | SRC_ALPHA / INV_SRC_ALPHA | Component maximum |
| linearburn | Yes | SRC_ALPHA / INV_SRC_ALPHA | Unclamped `d+s-1` |
| lineardodge | Yes | SRC_ALPHA / INV_SRC_ALPHA | `d+s` |
| linearlight | Yes | SRC_ALPHA / INV_SRC_ALPHA | Clamped `d+2*s-1` |
| overlay | Yes | SRC_ALPHA / INV_SRC_ALPHA | Destination selects multiply/screen branch |
| pinlight | Yes | SRC_ALPHA / INV_SRC_ALPHA | Source selects min/max branch |
| softlight | Yes | SRC_ALPHA / INV_SRC_ALPHA | Original power-curve equation |
| vividlight | Yes | SRC_ALPHA / INV_SRC_ALPHA | Piecewise bounded burn/dodge |
| writealpha (sprite/primitive only) | Yes | Replacement if RGB is enabled | Emits vertex RGB and `a+destination.a`; alpha must replace (ONE / ZERO), with no second alpha accumulation. Caller chooses RGB write mask. |

Sprite/primitive mul and screen use the post-transition alpha. Particle mul/screen premultiply RGB with coverage before
the optional soft-particle fade; the fade changes only the emitted alpha. Keep this behavior when integrating blend states.
Per-mode comments are also in `common.hlsli` and the family output paths.

## Vertex and geometry shaders: not numerically checked

The pixel oracle does not exercise VS/GS. The following 12 entries were reviewed against their disassembly by hand and
are explicitly **not numerically checked**; stream-out comparison remains future work.

| Entries | Count | Assembly review |
| --- | ---: | --- |
| sprite/vsprimitive, sprite/vssprite | 2 | Colour overrides, offsets, primitive input z vs sprite zero z, projective matrix and signatures |
| vsparticle | 1 | WVP, linear-depth and projective transforms |
| vsposteffect | 1 | Column-major matrix multiplication, stencil rectangle and alpha |
| scene/vsprimitive, scene/vssprite | 2 | World/view/projection and projected texture coordinates |
| vsbasic, vsupdategbuffer_mesh | 2 | View-space basis normalization and linear-depth packing |
| vsmodelstreamout_mesh | 1 | World transform and basis/UV output |
| vsmodelstreamout_skinmesh | 1 | Palette matrix blend, final residual influence, normalized transformed basis |
| vsmodelstreamout_skinmeshmorph | 1 | Morph IDs select POSITION0/POSITION1/zero before skinning; NORMAL1 is unused |
| gsmodelstreamout_mesh | 1 | One point emitted with all six attributes copied unchanged |

Skinning assumes the original input contract: `g_BoneNum` is 1..4, bone indices are within the 256-matrix palette, and
morph IDs index the original four-row selector table. Hand review fixed several earlier VS/GS reconstruction errors,
including an accidental all-zero geometry output. No numerical claim is made for these stages.
