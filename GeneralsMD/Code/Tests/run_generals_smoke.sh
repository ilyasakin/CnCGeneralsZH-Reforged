#!/usr/bin/env bash
#
# The POSIX generals, started without game data (C2): the whole entry sequence up to the first thing
# that needs data.  Rooted at an empty folder, PosixMain sets the root, starts the log, the memory
# manager and the version, takes the one-copy lock in the user data directory, and runs GameMain;
# SdlGameEngine starts GameEngine::init, which mounts no archives and stops, as generals.exe does on
# Windows, with the "no base game" message and exit code 1.
#
# What it cannot see: anything past the archive check, which needs the game's data; a run with the
# data is E1's harness's.  Usage: run_generals_smoke.sh <generals>
set -u
GENERALS="$1"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/generals_smoke.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/root" "$WORK/user"
failed=0
check() { if ! eval "$1"; then echo "FAIL: $2"; failed=1; else echo "ok: $2"; fi; }

cd "$WORK"
ZH_USER_DATA_DIR="$WORK/user" "$GENERALS" -headless -root "$WORK/root" > "$WORK/out.txt" 2> "$WORK/err.txt"
status=$?
check '[ $status -eq 1 ]' "rooted at an empty folder it exits 1 (was $status)"
check 'grep -q "none of the base game'"'"'s .big files could be found" "$WORK/err.txt"' "it stops at the base game check and says why"
check '[ -e "$WORK/user/Generals-685EAFF2-3216-4265-B047-251C5F4B82F3.lock" ]' "the one-copy lock is in ZH_USER_DATA_DIR"

# A root that cannot be entered is refused before anything else starts.
ZH_USER_DATA_DIR="$WORK/user" "$GENERALS" -headless -root "$WORK/no such folder" > /dev/null 2> "$WORK/err2.txt"
status=$?
check '[ $status -eq 1 ] && grep -q "cannot use .* as the install root" "$WORK/err2.txt"' "a root that does not exist is refused"

exit $failed
