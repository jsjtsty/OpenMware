# SQAction1D tempo table

The 61 pointers are read from VA `0x005b37f8` in the original 32-bit executable. Entry 0 is `0x00430bb0`;
entry 60 is **0x004333d0**. The task sheet's proposed last address, `0x004326d0`, is actually entry 39.
All entries are identified. Powers are 2, 4 and 8, not the usual quadratic/cubic/quartic sequence.

Write `u=timer/duration`, `mix(f)=src+(dst-src)*f`, and `out(F)=dst+(src-dst)*F((duration-timer)/duration)`.
`in(F)=mix(F(u))`. `in_out` and `out_in` split at `duration*0.5`, with midpoint `(src+dst)*0.5` and the
indicated pair of curves. Each half performs its own original float operations, not an algebraically simplified blend.

Shapes:

- `square(u)=u*u`, `fourth(u)=(u*u)*(u*u)`, `eighth(u)=fourth(u)*fourth(u)`.
- `back2(u)=(1.8633248805999756*u-0.8633249402046204)*u`.
- `back3(u)=u*u*(2.701540231704712*u-1.701540231704712)`.
- `back4(u)=u*u*u*(3.554462194442749*u-2.554462194442749)`.
- `elasticN(u)=-2^(10*(u^N-1))*sin(((u^N-1)-0.07500000298023224)*20.943950653076172)`.
- `bounce(u)` uses coefficient 7.5625 and intervals at float(4/11), float(8/11), float(10/11),
  with centers 0, float(6/11), float(9/11), float(21/22) and levels 0, 3/4, 15/16, 63/64.
- `bounce2(u)=bounce(u^2)`, `bounce4(u)=bounce(u^4)`.
- Trigonometric `x=(timer*float(pi/2))/duration`, evaluated in that order.

| Index | Original address | Curve name | Formula | Notes |
| ---: | --- | --- | --- | --- |
| 0 | 0x00430bb0 | linear | `mix(u)` | SSE float arithmetic |
| 1 | 0x00430d10 | power2_in | `in(square)` | SSE float arithmetic |
| 2 | 0x00430d50 | power2_out | `out(square)` | SSE float arithmetic |
| 3 | 0x00430d90 | power4_in | `in(fourth)` | SSE float arithmetic |
| 4 | 0x00430dd0 | power4_out | `out(fourth)` | SSE float arithmetic |
| 5 | 0x00430e20 | power8_in | `in(eighth)` | SSE float arithmetic |
| 6 | 0x00430e70 | power8_out | `out(eighth)` | SSE float arithmetic |
| 7 | 0x00430ec0 | power2_in_out | `split(power2_in / out)` | SSE float arithmetic |
| 8 | 0x00430f50 | power2_out_in | `split(power2_out / in)` | SSE float arithmetic |
| 9 | 0x00430fe0 | power4_in_out | `split(power4_in / out)` | SSE float arithmetic |
| 10 | 0x00431080 | power4_out_in | `split(power4_out / in)` | SSE float arithmetic |
| 11 | 0x00431120 | power8_in_out | `split(power8_in / out)` | SSE float arithmetic |
| 12 | 0x004311c0 | power8_out_in | `split(power8_out / in)` | SSE float arithmetic |
| 13 | 0x00431260 | back2_in | `in(back2)` | SSE float arithmetic |
| 14 | 0x004312b0 | back2_out | `out(back2)` | SSE float arithmetic |
| 15 | 0x00431310 | back3_in | `in(back3)` | SSE float arithmetic |
| 16 | 0x00431360 | back3_out | `out(back3)` | SSE float arithmetic |
| 17 | 0x004313c0 | back4_in | `in(back4)` | SSE float arithmetic |
| 18 | 0x00431420 | back4_out | `out(back4)` | SSE float arithmetic |
| 19 | 0x00431480 | back2_in_out | `split(back2_in / out)` | SSE float arithmetic |
| 20 | 0x00431530 | back2_out_in | `split(back2_out / in)` | SSE float arithmetic |
| 21 | 0x004315e0 | back3_in_out | `split(back3_in / out)` | SSE float arithmetic |
| 22 | 0x004316a0 | back3_out_in | `split(back3_out / in)` | SSE float arithmetic |
| 23 | 0x00431760 | back4_in_out | `split(back4_in / out)` | SSE float arithmetic |
| 24 | 0x00431820 | back4_out_in | `split(back4_out / in)` | SSE float arithmetic |
| 25 | 0x004318e0 | elastic2_in | `in(elastic2)` | Elastic endpoint offset retained; powf/sinf library calls |
| 26 | 0x00431980 | elastic2_out | `out(elastic2)` | Elastic endpoint offset retained; powf/sinf library calls |
| 27 | 0x00431a20 | elastic4_in | `in(elastic4)` | Elastic endpoint offset retained; powf/sinf library calls |
| 28 | 0x00431ac0 | elastic4_out | `out(elastic4)` | Elastic endpoint offset retained; powf/sinf library calls |
| 29 | 0x00431b70 | elastic8_in | `in(elastic8)` | Elastic endpoint offset retained; powf/sinf library calls |
| 30 | 0x00431c10 | elastic8_out | `out(elastic8)` | Elastic endpoint offset retained; powf/sinf library calls |
| 31 | 0x00431cc0 | elastic2_in_out | `split(elastic2_in / out)` | Elastic endpoint offset retained; powf/sinf library calls |
| 32 | 0x00431e10 | elastic2_out_in | `split(elastic2_out / in)` | Elastic endpoint offset retained; powf/sinf library calls |
| 33 | 0x00431f80 | elastic4_in_out | `split(elastic4_in / out)` | Elastic endpoint offset retained; powf/sinf library calls |
| 34 | 0x004320e0 | elastic4_out_in | `split(elastic4_out / in)` | Elastic endpoint offset retained; powf/sinf library calls |
| 35 | 0x00432240 | elastic8_in_out | `split(elastic8_in / out)` | Elastic endpoint offset retained; powf/sinf library calls |
| 36 | 0x004323b0 | elastic8_out_in | `split(elastic8_out / in)` | Elastic endpoint offset retained; powf/sinf library calls |
| 37 | 0x00432520 | bounce_in | `out(bounce)` | Bounce interval constants rounded to float |
| 38 | 0x00432600 | bounce_out | `in(bounce)` | Bounce interval constants rounded to float |
| 39 | 0x004326d0 | bounce_power2_in | `out(bounce2)` | Bounce interval constants rounded to float |
| 40 | 0x004327b0 | bounce_power2_out | `in(bounce2)` | Bounce interval constants rounded to float |
| 41 | 0x00432890 | bounce_power4_in | `out(bounce4)` | Bounce interval constants rounded to float |
| 42 | 0x00432970 | bounce_power4_out | `in(bounce4)` | Bounce interval constants rounded to float |
| 43 | 0x00432a50 | bounce_in_out | `split(bounce_in / out)` | Bounce interval constants rounded to float |
| 44 | 0x00432ad0 | bounce_out_in | `split(bounce_out / in)` | Bounce interval constants rounded to float |
| 45 | 0x00432b50 | bounce_power2_in_out | `split(bounce_power2_in / out)` | Bounce interval constants rounded to float |
| 46 | 0x00432bd0 | bounce_power2_out_in | `split(bounce_power2_out / in)` | Bounce interval constants rounded to float |
| 47 | 0x00432c50 | bounce_power4_in_out | `split(bounce_power4_in / out)` | Bounce interval constants rounded to float |
| 48 | 0x00432cd0 | bounce_power4_out_in | `split(bounce_power4_out / in)` | Bounce interval constants rounded to float |
| 49 | 0x00432d50 | sine_in | `mix(1-cos(x))` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 50 | 0x00432db0 | sine_out | `mix(sin(x))` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 51 | 0x00432e00 | sine4_in | `mix(sin(x)^4)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 52 | 0x00432e60 | cosine4_out | `mix(1-cos(x)^4)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 53 | 0x00432ec0 | sine8_in | `mix(sin(x)^8)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 54 | 0x00432f20 | cosine8_out | `mix(1-cos(x)^8)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 55 | 0x00432f90 | sine_in_out | `split(sine_in,sine_out)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 56 | 0x00433060 | sine_out_in | `split(sine_out,sine_in)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 57 | 0x00433130 | sine4_in_out | `split(sine4_in,cosine4_out)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 58 | 0x00433210 | sine4_out_in | `split(cosine4_out,sine4_in)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 59 | 0x004332f0 | sine8_in_out | `split(sine8_in,cosine8_out)` | sinf/cosf; float pi/2 global at 0x005be8a8 |
| 60 | 0x004333d0 | sine8_out_in | `split(cosine8_out,sine8_in)` | sinf/cosf; float pi/2 global at 0x005be8a8 |

## Precision and domain

Disassembly resolves a misleading Ghidra `float10` return: the curves calculate with scalar SSE `addss/subss/mulss/divss`,
store a 32-bit float, then `fld` that float for the x86 ABI. They do not calculate their polynomial arithmetic in extended
precision. The port therefore uses float operands and float sin/cos/pow overloads, rather than double intermediates.
The original optimized CRT math helpers at 0x0058fa10 / 0x005900c3 / 0x0058f76c implement powf / sinf / cosf.
Host C++ libraries can differ by a few final bits from these old helpers; this is not claimed bit-for-bit identical across
platform libm implementations. Compile without FMA contraction or fast-math; the test target requests strict FP.

The elastic shapes are not snapped at 0 or 1: `F(0)` is approximately -0.00048828 and `F(1)` approximately 1.
In/out variants therefore retain the corresponding small source/destination overshoot; split variants retain their
half-size offsets and midpoint behavior. Back and bounce curves are deliberately nonmonotonic. No time clamping is
performed in this standalone evaluator; the original action wrapper clamps its timer before calling the table.

Invalid tempo values clamp to [0,60], as permitted by the task. Nonpositive durations return dst as an explicit safe
instant-completion policy; direct division by zero in a raw original curve is not reproduced. NaN inputs otherwise
propagate through the selected arithmetic. The root test target is independent of the script runtime and does not
change SQAction1D integration or any existing runtime target.

## Validation

`test_easing` built through the root CMake target in `build/easing-check` and passed with zero failures. Tests exercise
all 61 original endpoint behaviors, 1,024 monotonicity samples for each polynomial/trigonometric curve, affine endpoint
ranges, tempo guards, zero duration, and independent hand-calculated interior values including a bounce midpoint.
The existing Debug compiler setup hit MSVC C1902 (PDB manager mismatch); a separate build configuration used embedded
debug information (`-D CMAKE_CXX_FLAGS_DEBUG=/Z7`) to avoid that environment issue. No existing runtime target was edited.
