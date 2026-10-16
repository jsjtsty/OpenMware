# Squirrel pseudo-source decompiler

## Usage

No packages, compiler or VM process are required. Python's standard library is
enough; `-I` is supported.

```powershell
.venv\Scripts\python.exe -I scripts/nutdec.py ref/work/script_x/media/script/system/boot.nut > boot.nut.txt
.venv\Scripts\python.exe -I scripts/nutdec.py --all ref/work/dec
.venv\Scripts\python.exe -I scripts/nutdec.py --all
.venv\Scripts\python.exe -I scripts/nutdec.py SOME_FILE.dis
```

Batch output preserves paths beneath `ref/work/script_x/media/script`, with
`.nut.txt` appended. `--scripts-root` selects another input tree. The batch
summary is printed and saved as `OUTDIR/summary.json`. A failed input gives a
nonzero exit code and an explicit filename/error in that summary.

## Reading and lifting

- The reader decodes the ABI32 closure stream inside the 16-byte SCRP wrapper,
  using `sqobject.cpp` as the format reference. It validates section tags,
  bounds, counts and the closure trailer, and retains every literal, local
  lifetime, parameter, outer capture, default-parameter register and child
  prototype. The SCRP trailer exports constants; these become `const` entries.
- Reading binary directly avoids the incomplete literal pool and unescaped
  strings in nutdis text. The optional `.dis` reader preserves the original
  text for fallback, including multiline literal continuations. If the text
  lacks a needed optimized literal or prints an unknown opcode as `?`, that
  function falls back rather than guessing.
- VM register uses/definitions and backwards liveness determine which values
  cross control-flow boundaries. Those values get named temporaries. Other
  register traffic becomes expressions, member chains and calls. Debug locals
  use their names/lifetimes, and unnamed registers use `vN`. Locals are declared
  at function scope to make merge temporaries visible on both paths.
- The structured walker recognizes forward branches, common joins, natural
  backedges, foreach/postforeach pairs, exception traps, and the compiler's
  switch skip-test pattern. Counted loops with a proven final local increment
  become `for`; other loops may use `while (true)` plus a conditional `break`.
  Pure short-circuit/ternary regions become `&&`, `||` and `?:`; regions with
  statements retain conditional blocks so evaluation remains conditional.
- Adjacent table/array/class construction stores are folded into literals or
  class blocks when their order permits it. Closures are emitted at their
  creation sites, with default values and capture snapshots described in the
  function header. Unreferenced child prototypes are retained separately.
- Statements carry `// Lnnn` when the source line changes. If an opcode or
  control-flow shape cannot be lifted, the whole affected function has a
  `FALLBACK` block with indexed/labelled raw instructions, all four operands,
  source lines, literal pool and local/capture/default metadata. Child
  prototypes are still emitted. An unknown-opcode check confirmed this path.

### Game-specific opcode 0x3d

The game's VM has one more opcode than the public Squirrel 2.2.4 table.
`0x3d` is `TOSTRING a0,a1`: convert register a1 to a string in a0. This was
confirmed in the regenerated EXE decompilation:
`switchD_004108b3_caseD_3d` calls `FUN_0040eb90(source, destination)`, which
handles strings, `%d`, `%g`, booleans and the `_tostring` metamethod. The lifter
prints `(value).tostring()`. Existing nutdis caches display this opcode as `?`.

## Verification

The complete input set contains **2,129 files and 8,528 function prototypes**.
All files were processed successfully, with **0 structural fallback functions**
(100% clean by the task's fallback metric). A full pass takes about 26 seconds
on this workspace. The final run's timings/counts are in `ref/work/dec/summary.json`.

The four requested large files plus `system/boot.nut` were independently
compared with the existing nutdis caches: all **809 prototypes**, every
instruction's four operands, source lines, parameters and nesting agree. The
only opcode-name difference is the now-identified `0x3d`. Repeated renders of
these files produced identical source. File hashes and per-file counts are
saved in `ref/analysis/nutdec/verification.json`.

### Requested spot checks

| Function | Checked against bytecode |
| --- | --- |
| TitleLogoBase | Creates the 1024x576 color surface and two logo sprites; preserves fade/wait/delete arguments. The true Logo2_in branch is empty in the bytecode; the false branch performs the remaining fades/deletes. |
| StartTitleSelect | Preserves dialog lock/unlock order, BeginSelect/WaitSelect loop, menu/key alternatives, values assigned to TitleSelect, break paths, EndSelect/HideCursorRequest and final return TitleSelect. |
| CreateClipSprite | Preserves ten explicit parameters and owner=null in its closure definition; SCREEN/SCENE/file alternatives call the corresponding create method; failures erase and return false; success sets priority, calls init and returns name. |
| CheckEnd | Compares GameName with z9000.nut, returns false on mismatch, builds the boot closure, passes it to CallStartFile and returns true. |
| CallFile | Preserves volume/status setup, dropped-file handling, CheckEnd/CheckExtraMenu early returns, recording loop and the Route.fl_startfile alternatives. Unreachable statements already present after a RETURN remain visible. |

Standalone excerpts of these five lifted functions are saved alongside the
verification JSON. The complete outputs contain their closure defaults and
the surrounding class/registration context.

## Scope

This is readable reverse-engineering pseudo-source. The clean percentage
measures structural coverage, not proof of runtime equivalence or successful
recompilation. Some register assignments, empty branches, unreachable returns,
explicit `.call` receivers and expanded short-circuit blocks remain visible.
Metamethod-dependent property access and complex aliasing still merit checking
against the original instructions when implementing behavior. Constants are
only named when the binary supplies a name; ambiguous numeric values are not
globally replaced with guesses from the shared constants catalogue.

No runtime sources, third-party sources or nutdis implementation were changed.
