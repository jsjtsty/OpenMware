#!/bin/sh
# Starts every story script directly (the game's own "dropped file" debug entry, g_DroppedFileName), forces Skip, and records
# script errors per script. Usage: scripts/sweep_scripts.sh WORKER_INDEX WORKER_COUNT [FRAMES]
# Output: ref/sweep_dropped/NAME.txt (only for scripts that error) and ref/sweep_dropped/_done.WORKER (finished names).
# Needs build/src/OpenMware.exe, keys.local.txt and the decompiled scripts in ref/work/dec/nut (for the name list).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDX="${1:-0}"
CNT="${2:-1}"
FRAMES="${3:-5000}"
GAME="D:/Games/君与彼女与彼女之恋"
OUT="$ROOT/ref/sweep_dropped"
WORK="$ROOT/ref/sweep_work/w$IDX"
mkdir -p "$OUT" "$WORK"
cd "$WORK" || exit 1
export OMW_NOAUDIO=1
n=0
for f in "$ROOT"/ref/work/dec/nut/*.nut.txt; do
	name="$(basename "$f" .nut.txt)"
	n=$((n + 1))
	[ $((n % CNT)) -eq "$IDX" ] || continue
	rm -rf save
	timeout 180 "$ROOT/build/src/OpenMware.exe" "$GAME" --keys "$ROOT/keys.local.txt" --shaders "$ROOT/build/src/shaders" \
		--frames "$FRAMES" --shot "$WORK/shot.png" \
		--eval "200:g_DroppedFileName=\"media/script/nut/$name.nut\"" \
		--eval "250:System.skip_absolute=1" \
		--eval-every "400:60:if (::Status.skip != 1 && ::Status.lock_skip != 1) ::OMW_TapKey(29);" \
		--auto 400:90:145:535 --auto 445:90:120:422 2>&1 | grep -a "script error\|^ *at " | head -14 > "$OUT/$name.txt"
	if [ -s "$OUT/$name.txt" ]; then :; else rm -f "$OUT/$name.txt"; fi
	echo "$name" >> "$OUT/_done.$IDX"
done
