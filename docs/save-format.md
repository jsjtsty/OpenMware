# Save format

The game saves by snapshotting the Squirrel VM, so OpenMware does the same (`src/script/vm_snapshot.cpp`).

Natives implemented: `CreateReferenceTable`, `ClearReferenceTable`, `SerializeRootTable(nut, recreateCb, resetCb)`,
`DeserializeRootTable(nut, recreateCb, resetCb, errorCb)` (0 ok, -1 bad data -> error script, -2 fatal -> reboot).

- Everything reachable from the root table is walked (tables, arrays, closures with their prototypes, classes,
  instances, generators and suspended threads with call stacks and exception traps). Entries in the ignore array
  (processes, backlog, read-file state ...) are skipped.
- Instances of script classes are written with the script's own `InstanceRecreateSaveCallback` payload (a type name
  plus data, written into the same stream, length-prefixed so the reader can resynchronise). On load the recreate
  callback builds the object; `InstanceResetLoadCallback` is run afterwards to restore sprites, sounds and text from
  `m_RegenerateInfo` / `m_LastActionInfo`.
- Native objects keep no state: their `serialize`/`deserialize` just return true. Instances whose payload is the
  "Unknown" marker are re-instantiated as plain instances.
- Natives that the reset callbacks read back (`get*Timer`, `get*Phase`) must return numbers; unimplemented ones
  return 0.0 (see `native_stub`).
