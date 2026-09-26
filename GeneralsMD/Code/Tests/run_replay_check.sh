#!/usr/bin/env bash
#
# E1's POSIX harness (Tools/replay-check.sh) checked on itself, with the game's data.
#   1. One seed, 1200 frames: the recording, its playback and a second run from the same seed stop on
#      the same HEADLESS CRC at the same frame.
#   2. The armed control: the same with the kept replay's game seed edited before the playback.  The
#      harness must report that seed DIVERGED, on the playback and not on the second run, and exit 1 -
#      so a pass in 1 is a comparison that can fail.
#
# WHAT THIS PROVES: same-machine determinism only, on this machine and this build.  Nothing about
# agreement with a Windows build, which needs a replay recorded on Windows; none exists yet.
#
# Needs ZH_DATA_DIR (a folder holding zerohour/); without it, exit 77, which ctest reports as Skipped.
# Usage: run_replay_check.sh <generals>
set -u
GENERALS="$1"
HARNESS="$(cd "$(dirname "$0")/../Tools" && pwd)/replay-check.sh"
if [ -z "${ZH_DATA_DIR:-}" ]; then
	echo "skip: no game data (ZH_DATA_DIR)"
	exit 77
fi
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 1200 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 0 ]' "seed 0 agrees with itself (exit $status)"
check 'printf "%s" "$out" | grep -q "the playback and the second run agree at frame 1200"' "at frame 1200, the playback and the second run both"

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 600 --control 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 1 ]' "the control (the replay's seed edited) fails the harness (exit $status)"
check 'printf "%s" "$out" | grep -q "DIVERGED from live .* playback 0x"' "and it is the playback that diverged"
check '! printf "%s" "$out" | grep -q "second run 0x"' "while the unedited second run still agreed"

exit $failed
