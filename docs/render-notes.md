# Render notes (recovered from Mware.dll)

Source: `ref/analysis/mware/decompile/Mware.dll.c` (CGraphicsDX11 constructor, around line 137560).
Extractor: `scripts/extract_blend_table.py <decompile.c> 137560 137950`; full output `ref/analysis/render/blend_table.json`.
The helper `FUN_100865c0` fills a `D3D11_BLEND_DESC` (RT0): the enum arguments index D3D11 values +1; the alpha channel is
always `ADD ONE/ONE`. Engine blend states live in a device-wide table of 23 `CBlend` objects (`param_1[0x219..0x22f]`).

## Blend table (index = slot - 0x219)

| idx | blend enable | RGB equation | write mask | inferred script constant | shader family |
| --: | :-: | --- | --- | --- | --- |
| 0 | off | replace | RGB | `BLEND_MODE_DISABLE` | Blend |
| 1 | on | ADD SRC_ALPHA / INV_SRC_ALPHA | RGB | `BLEND_MODE_NORMAL` | Blend |
| 2 | on | ADD SRC_ALPHA / ONE | RGB | `BLEND_MODE_ADD` | Blend |
| 3 | on | REV_SUBTRACT SRC_ALPHA / ONE | RGB | `BLEND_MODE_SUB` | Blend |
| 4 | on | ADD DEST_COLOR / ZERO | RGB | `BLEND_MODE_MULTIPLE` | Mul |
| 5 | on | ADD INV_DEST_COLOR / ONE | RGB | `BLEND_MODE_SCREEN` | Screen |
| 6..20 | on | ADD SRC_ALPHA / INV_SRC_ALPHA | RGB | Overlay, ColorDodge, LinearDodge, ColorBurn, LinearBurn, SoftLight, HardLight, VividLight, LinearLight, PinLight, HardMix, Darken, Lighten, Difference, Exclusion (in this order) | same-named shader; reads `Texture_Destination` |
| 21 | off | replace | ARGB | (no-blend, writes alpha too) | WriteAlpha? |
| 22 | off | replace | none | (colour writes off) | - |

Notes
- The index -> constant mapping is **confirmed** by the script constants (`BLEND_MODE_DISABLE=0 ... BLEND_MODE_EXCLUSION=20`,
  see below); it also matches the order of shader names in the DLL string table.
- Sprites have no `Add`/`Sub` shader: ADD and SUB reuse `psSprite_Blend_*` with blend states 2 and 3. Particles have `Add`.
- Codex's equivalent-state table (shaders/hlsl/README.md) agrees for normal/mul/screen/in-shader modes; the real screen state
  is `INV_DEST_COLOR / ONE`, which gives the same result as `ONE / INV_SRC_COLOR` for the shader's premultiplied output.
- Other classes (scene manager, post effect, ...) build their own blend objects inline (callers of FUN_100865c0 / FUN_100866c0
  at lines ~13034, 14732, 35388, 36271); the latter takes separate alpha factors. Not yet decoded.

## Script constants live in a file tail

`framework/constantvalue.nut` is a closure followed by a constants block: after the closure payload comes the 4 bytes
`00 00 00 00`, the magic `LIAT` ('TAIL' reversed), a 4-byte header (`06 02 00 00`), then repeated records
`u16 len, name[len] (NUL-terminated)`, `u32 tag`, value: tag `0x05000002` = int32, tag `0x08000010` = `u32 len` + NUL-terminated string.
518 constants (`BLEND_MODE_*`, `KEY_CODE_*`, `NUT_TYPE_*`, ...) are dumped to `ref/analysis/render/script_constants.json`.
Every .nut ends with `LIAT` + 4 header bytes; six files carry records after it (constantvalue, sys_function, sys_val, sys_val_product,
loadnutfunction, savenutfunction). The loader must register these constants in the VM's const table when loading such a file.

## Shader table layout (FUN_100872e0, shader manager init)

Shaders are loaded as (vertex, pixel) pairs per family; the sprite families hold an array of 12 pointers
(`vsPrimitive` rows are the even entries, `vsSprite` the odd ones):

| index (u32 slot) | 0/1 | 2/3 | 4/5 | 6/7 | 8 | 9 | 10 | 11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| variant | Normal (prim/sprite) | Mask | Transition | MaskTransition | DisplacementMap | DispMapMask | DispMapTransition | DispMapMaskTransition |

(sprites only for the four displacement variants). So the shader choice is a function of three flags: mask, transition, displacement.
Post-effect shaders all pair with `vsPostEffect`; arrays hold X/Y/XY and `_WithMask` gaussian variants, DOF variants, etc.

## Sprite sampler/state selection (FUN_100235c0, CSprite2D virtual slot 52)

Sampler objects are built by `FUN_100862d0(filter, address, border)`:
- filter 0/1 -> D3D11 `MIN_MAG_MIP_POINT`; 2 -> `MIN_MAG_LINEAR_MIP_POINT` (0x14).
- address 0 -> WRAP, 1 -> CLAMP, 2 -> MIRROR, 3 -> BORDER, 4 -> MIRROR_ONCE.
CGraphics device holds four sprite samplers: `point+clamp`, `point+wrap`, `linear+clamp`, `linear+wrap`.
A sprite binds **wrap** when any of its UV offset/scroll fields (+0xf0, +0xf4, +0x208, +0x20c) is non-zero, otherwise **clamp**;
the point/linear choice comes from a per-sprite flag at +0x200. The same sampler is bound to slots 0 and 1; textures are
bound to slots 0 and 1 (`+0x1f4`, `+0x1fc`: main texture and a second one). The draw (slot 64, FUN_100234b0) binds those,
then calls the generic primitive draw `FUN_100211a0(this, topology=2, ...)` and unbinds textures 0/1.
