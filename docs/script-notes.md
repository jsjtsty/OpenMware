# Script runtime notes

## Bytecode format (verified by executing the game's scripts)
- `.nut` = 16-byte header (`SCRP`...) + Squirrel 2.2.4 closure stream, 32-bit ABI (`sq_open_ex(..., SQ_ABI_32)`).
- **Instruction records are the raw 32-bit struct: `int32 arg1, u8 op, u8 arg0, u8 arg2, u8 arg3`.** The original libtotono fork
  read `op, arg0, arg1, arg2, arg3` (wrong order): loading succeeded but execution crashed. `third_party/squirrel2/squirrel/sqobject.cpp`
  now reads/writes the real order.
- After the closure stream's `TAIL` tag (`LIAT` on disk) + 4 header bytes, files may carry constant records (see render-notes.md).
  Scripts read these constants as free identifiers; inside class methods the VM does a raw root-table lookup, so
  `ScriptHost` stores them **in the root table** (`media/script/framework/ConstantValue.nut` provides 518 of them).
- Script paths inside scripts use mixed case; `script.npk` stores lower case -> `NpkArchive::find_ci`.

## Boot protocol (`media/script/main.nut`, disassembled with `nutdis`)
```
Import("media/script/framework/N2Framework.nut"); InitMacro();   // InitMacro Include()s ~40 system/function scripts
N2Framework.init();
g_StartupScript = "media/script/system/boot.nut";
loop { s = N2Framework.run(g_StartupScript);     // SYSTEM_STATES_ABORT -> g_App.exit(); RESET -> N2Framework.reset(); RELOAD -> .reload()
       collectgarbage(); suspend(); }            // the engine resumes main once per frame
```
The entry point in the exe is `Diesel::CTaskMain` (FUN_0046a150) which loads `main.nut`.

## Native API
- Globals registered in `InitializeSquirrelRuntime` (exe, ~0x47a1f0): Import, PreCompile, IncludeFile, Suspend, GetScreenWidth/Height,
  SystemCall, CountTextLength, TokenizeText, ConvertToUTF16, GetSavePathRoot, GetTempPathRoot, FileDelete, ClearReferenceTable,
  SerializeRootTable, DeserializeRootTable, ResetNGCode, AddNGCode_HeadOfLine/EndOfLine, plus helper-registered ones whose names the
  decompile lost.
- `ref/analysis/sqbind/bindings.json` is **incomplete** (the extractor needs a string literal in the decompile; e.g. `SQApp.loadSystemFont`,
  `SQAction1D.setAction` are registered but missing). The runtime therefore resolves unknown members of the stub classes to logging stubs
  (`ScriptHost::missing`), and `scriptrun --discover` prints the real API actually used.

## Tools
- `scriptrun SCRIPT.npk [--keys F] [--discover] [--verbose] [entry.nut]`: boots the scripts against stub natives, prints natives called/missing.
- `nutdis FILE.nut`: disassembler (opcodes, literals, source lines, nested functions).
- `scripts/gen_sq_stubs.py`: regenerates `src/script/generated/sq_binding_names.inc` from bindings.json.

## Findings while booting (2026-10-16)
- `scriptrun --frames N` resumes the suspended `main` once per frame (`ScriptHost::resume_frame`). With the natives below the game gets
  through `boot.nut`, the title logo and into the title screen scripts (`title2`, `z1111.nut`) without a script error for 600 frames.
- Script threads (`newthread`) do not inherit the VM foreign pointer: natives must find the host via `ScriptHost::current()`.
- Vector results are returned through root variables: natives write `SQMATH_RET_1..16`, script-side helpers
  (`framework/command/MathFunction.nut`: `SQMATH_GetResult_Vec2/3/...`) build tables from them. Input/result of `SQMATH_*` natives uses the
  same registers (`SQMATH_V3_1_X`, ... are root variables, not functions).
- `framework/command/BindFunction.nut` only holds empty script declarations of some natives (documentation of signatures).
- `SQAction1D` is fully implemented (src/script/natives_action.cpp) from FUN_00520df0..00521250: fields duration/timer/src/dst/value/power/
  tempo/active/timescale; the 61 tempo curves (table at 0x5b37f8) are a placeholder linear curve until Codex task 06 lands.
- `SQApp` (src/script/natives_app.cpp) has real config/time/window-state methods; resource loaders accept requests and return true.
- Still stubs: SQSprite, SQScene, SQText, SQSound, SQFile, SQProcess, SQSpriteMask/RenderTarget/..., the SQMATH_* functions, TokenizeText,
  CountTextLength, ConvertToUTF16, Serialize/DeserializeRootTable and friends.
