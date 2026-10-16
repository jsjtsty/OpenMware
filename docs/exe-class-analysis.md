# EXE / Diesel class and binding analysis

## Reproduce

Use Ghidra 12.1.4 and Java 21. Copy the original Totono project before running
`ConfigureAnalysis.java`, then `ExportBinary.java OUTPUT ORIGINAL_EXE` as a
post script. Ghidra project directory names cannot contain `+`, so the working
copy is under `D:/CodeSpace/ghidra_12.1.4_PUBLIC/analysis-exe-task07/`.
The original project and pack are preserved.

```powershell
.venv\Scripts\python.exe scripts/index_mware_classes.py --analysis-root ref/analysis/exe --dll ref/totono-pack/binaries/totono_Steam.exe
# Generate this disassembly with a VS developer shell:
dumpbin /DISASM ref/totono-pack/binaries/totono_Steam.exe > build/tools/tempo-exe-disasm.txt
.venv\Scripts\python.exe scripts/extract_sq_bindings_asm.py
.venv\Scripts\python.exe scripts/index_sqwrappers.py
```

The generic exporter adds an original-address marker before each C result.
The indexer validates these against functions.tsv. The older Mware export
continues to use its validated export-order parser.

## Binding recovery

The v1 extractor interprets arguments at decompiled calls. Optimized helpers
can ignore those apparent arguments and embed their own name and native
member pointer. The assembly pass finds `mov edx,NAME` before `sq_pushstring`
at 0040a510, and the code pointer stored into the userdata returned by
0040a850 before `sq_newclosure` at 0040ae50. Calls to those helpers inside
known class initializers establish class ownership. Specialized-helper facts
replace stale v1 rows for that class/helper; other v1 rows are retained.

`bindings_v2.json` retains the original schema with an added `source` field.
`source_line=0` on assembly rows means there is no matching C source line.
Assembly instruction addresses and initializer addresses are recorded in
`bindings_v2_evidence.tsv`. Signature categories remain inferred.

## Wrapper resolution

`classes/sqwrappers.tsv` includes every known binding, including implementations
without embedded-object virtual calls and unresolved cases. It records the
receiver, object-field byte offset, virtual-slot byte offset, slot index,
candidate class and target address. Multiple candidates remain separate rows.
One row does not mean that candidate is the sole runtime type.

Construction traces are recorded in `sqwrapper_field_types.json`. In particular:

- SQSprite uses field +0x100; its normal factory constructs CSprite, while its
  failed-create path constructs CNullSpriteMask.
- SQText's +0x100 is populated in its object constructor, before `create` is
  called. Its factory selects CText, CTextEnglish or CTextVertical.
- SQSound uses +0xbc, with CSound and CNullSound paths.
- CTaskObject construction populates +0xb4 from CApp's current task stack.
  CTask and CTaskMain are candidates; their slots are not labeled uniquely
  resolved solely from the wrapper text.
- SQScene's +0x100 is a flag, not the wrapped CSprite field. Scene post effects
  use +0x4b8, copied from the app's +0x30 service. That service is obtained via
  Mware interfaces; its dynamic vtable must not be guessed as Diesel::CSprite.

Calls through arbitrary parameters, globals and untraced fields remain
`unresolved_field_type`; absence of a recognized embedded call is labeled
`no_embedded_vcall`. Secondary vtables, indirect factory paths and register
alias chains beyond direct field loads need further analysis. All slots in
the class documents are read from PE bytes, with sizes/signatures from the
regenerated Ghidra inventory and primary-base override comparisons from RTTI.

## Completed export counts

- 370 RTTI classes, 215 primary vtables, 243 class documents.
- 11,676 decompiled functions, zero exporter failures; 16,602 direct call edges.
- CSprite has 237 virtual slots, all with byte offsets and function sizes.
- bindings_v2: 1,691 rows, 785 distinct names (v1: 1,674 / 661).
- 251 assembly-backed rows; examples: setAction, loadSystemFont, move, setTimeline, setBezier.
- sqwrappers: 2,179 rows covering all 1,691 bindings; 574 bindings have recognized embedded virtual calls, 235 have construction-backed target candidates.
- SQSprite, SQScene, SQText, SQSound and SQApp are all represented. SQScene post-effect service calls remain explicitly unresolved.

Detailed counts and source hashes are in classes/stats.json; candidate coverage is in classes/sqwrappers_stats.json.
