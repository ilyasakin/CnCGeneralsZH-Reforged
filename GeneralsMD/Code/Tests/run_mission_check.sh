#!/usr/bin/env bash
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# -mission, headless, with the game's data: each run must reach its frame limit, or quit when the map is
# not there, rather than sit at the main menu (GAME_NONE) until something kills it.
#   1. A campaign mission by its path, at hard: campaign 'usa' mission 'mission01', at hard.
#   2. A challenge by its bare map name: GC_ChemGeneral is campaign 'challenge_0', a challenge.  The
#      challenge menu's setup (TheChallengeGameInfo) is what GameLogic reads for it.
#   3. A map no campaign plays: loaded plain, as -file loads it in _DEBUG and _INTERNAL builds.
#   4. A map that is not there: logged, and the run quits without starting a game.
#   5. Defect #32: the shipped test map Hovercraft, whose map.ini defines two locomotors of its own.  The
#      match ends at once (no enemy), and tearing it down must finish: LocomotorStore::reset used to erase
#      through a dangling iterator and spin there until the alarm.
# Every run has a time limit, so a start that never happens fails here instead of hanging ctest.
#
# RULE 9: the game never runs with the real install as its root.  The root is a farm of links into it,
# and the install is listed before the farm is built and checked again at the end (Tools/install-guard.sh).
#
# Needs ZH_DATA_DIR (a folder holding zerohour/); without it, exit 77, which ctest reports as Skipped.
# Usage: run_mission_check.sh <generals>
set -u
if [ -z "${ZH_DATA_DIR:-}" ] || [ ! -d "$ZH_DATA_DIR/zerohour" ]; then
	echo "skip: no game data (ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi
TOOLS="$(cd "$(dirname "$0")/../Tools" && pwd)"
CODE="$(cd "$TOOLS/.." && pwd)"
INSTALL="$(cd "$ZH_DATA_DIR/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$1")" && pwd)"
GENERALS="$EXEDIR/$(basename "$1")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mission-check.XXXXXX")"
ROOT="$WORK/root"
USERDATA="$WORK/user"
TAG="mc$$_"
MAXFRAMES=150
LIMIT="${MISSION_CHECK_LIMIT:-300}"	# seconds for one run; a run that reaches its frames takes a few

. "$TOOLS/install-guard.sh"
cleanup() {
	if ! install_verify "$INSTALL" "$WORK/install-before.list" "$WORK/install-after.list"; then
		echo "kept for inspection: $WORK" >&2
		exit 99
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT
install_snapshot "$INSTALL" "$WORK/install-before.list"

mkdir -p "$ROOT" "$USERDATA"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
"$TOOLS/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$WORK/overlay" > /dev/null

failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

# run_mission <n> <switches...>: sets STATUS, LOG (the run's -mission lines) and RESULT (its HEADLESS RESULT)
run_mission() {
	local prefix="$TAG$1"; shift
	local log="$EXEDIR/${prefix}DebugLogFile.txt"
	rm -f -- "$log"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" perl -e 'alarm shift; exec @ARGV' "$LIMIT" \
		"$GENERALS" -headless -root "$ROOT" -overlay "$WORK/overlay" -quickstart -noshellmap -multiInstance \
		-noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$prefix" "$@" > /dev/null 2>&1 )
	STATUS=$?
	LOG="$(grep -a -- '^-mission' "$log" 2>/dev/null)"
	RESULT="$(grep -a 'HEADLESS RESULT: ' "$log" 2>/dev/null | tail -1)"
	printf '%s\n%s\n' "$LOG" "$RESULT"
}

run_mission 1 -mission 'Maps\MD_USA01\MD_USA01.map' hard
check '[ $STATUS -eq 0 ] && printf "%s" "$RESULT" | grep -q "frame limit reached on frame $MAXFRAMES"' \
	"a campaign mission reaches frame $MAXFRAMES (exit $STATUS)"
check 'printf "%s" "$LOG" | grep -q "is campaign '"'usa'"' mission '"'mission01'"', hard"' "as the USA campaign's first mission, at hard"

run_mission 2 -mission GC_ChemGeneral
check '[ $STATUS -eq 0 ] && printf "%s" "$RESULT" | grep -q "frame limit reached on frame $MAXFRAMES"' \
	"a challenge, named bare, reaches frame $MAXFRAMES (exit $STATUS)"
check 'printf "%s" "$LOG" | grep -q "GC_ChemGeneral.map'"'"' is campaign '"'challenge_0'"' mission '"'mission01'"', a challenge, normal"' \
	"as challenge_0, at normal"

run_mission 3 -mission 'Maps\Tournament Desert\Tournament Desert.map'
check '[ $STATUS -eq 0 ] && printf "%s" "$RESULT" | grep -q "frame limit reached on frame $MAXFRAMES"' \
	"a map no campaign plays reaches frame $MAXFRAMES (exit $STATUS)"
check 'printf "%s" "$LOG" | grep -q "no campaign plays .* plain single player map, normal"' "loaded plain"

run_mission 4 -mission 'Maps\NoSuch\NoSuch.map'
check '[ $STATUS -eq 0 ] && [ -z "$RESULT" ]' "a map that is not there starts nothing and quits (exit $STATUS)"
check 'printf "%s" "$LOG" | grep -q "NoSuch.map'"'"' is not there"' "and says so"

run_mission 5 -mission 'Maps\Hovercraft\Hovercraft.map'
check '[ $STATUS -eq 0 ] && [ -n "$RESULT" ]' \
	"Hovercraft, whose map.ini defines two locomotors of its own, ends and exits (defect #32; exit $STATUS)"

exit $failed
