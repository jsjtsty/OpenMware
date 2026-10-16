// Save/load of the whole script world: SerializeRootTable / DeserializeRootTable and the reference-table natives.
//
// The game's save files are snapshots of the Squirrel VM: every table, array, class instance, closure and suspended
// coroutine reachable from the root table. Native objects (sprites, sounds, text ...) are not stored; the script-side
// callbacks (CCommand.InstanceRecreate*/InstanceReset*) rebuild them. See docs/save-format.md.
#pragma once

namespace omw {

class ScriptHost;
void register_snapshot_natives(ScriptHost& host);

} // namespace omw
