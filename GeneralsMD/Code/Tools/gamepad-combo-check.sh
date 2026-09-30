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
# G1's combo boxes: a pad's list is its own until it picks.  In EA's
# list box a selection is the choice (GLM_SELECTED, then the combo's GCM_SELECTED to its screen, which applies it), so
# the pad keeps a highlight of its own in an open list, and the screen hears of a choice only when A picks.
#
# Four skirmish set-ups from the same menu walk, each started and run to -maxframes (the match's frame; the menus'
# frames stay under it), at a fixed -seed, comparing the CRC of that frame (HEADLESS CRC AT LIMIT):
#   pad     - on skirmish setup's row for slot 1: right on its Team combo (the rightmost, so the value steps in place),
#             left to its Army combo (another combo beside it: left and right move along such a row), then A opens
#             the list, down twice and right (the highlight only), B (shut, nothing chosen), A, down, A (picked);
#   cancel  - the Army list opened, the highlight moved, B: nothing chosen, so the same match as the control;
#   hand    - the same Army by the mouse: a click on the combo's drop-down button and one on the row, where the pad's
#             run logged them (a team changes no match, so the stepped Team is left out);
#   control - no choice at all.
# PASS: pad and hand agree, cancel and control agree, pad and control differ, and the pad's list went as it should
# (its log's "GAMEPAD LIST" lines, the highlight moving while the choice stays until the pick).  -offscreen and
# ZH_AUDIO_BACKEND=null: no window, no sound.  Rule 9: a farm of the install, listed before and after.
#
# Usage: gamepad-combo-check.sh --generals <path> [--data <dir>] [--keep]

set -u

GENERALS=""; DATA="${ZH_DATA_DIR:-}"; KEEP=0; TIMEOUT="${GAMEPAD_COMBO_TIMEOUT:-900}"	# a backstop: the walk waits on the menus
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "gamepad-combo-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "gamepad-combo-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/gamepad-combo-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "gamepad-combo-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
USERDATA="$WORK/user"
TAG="gc$$_"

. "$(dirname "$0")/install-guard.sh"	# install_snapshot, install_verify
INSTALL_VERIFIED=0
verify_install() {
	[ "$INSTALL_VERIFIED" -eq 1 ] && return 0
	INSTALL_VERIFIED=1
	install_verify "$INSTALL" "$WORK/install-before.list" "$WORK/install-after.list" > "$WORK/install-verdict.txt" 2>&1
	local verdict=$?
	[ "$verdict" -eq 0 ] && return 0
	cat "$WORK/install-verdict.txt" >&2 2>/dev/null || echo "FAIL: COULD NOT VERIFY the install" >&2
	return 1
}
cleanup() {
	if ! verify_install; then
		trap - EXIT
		echo "kept for inspection: $WORK" >&2
		exit 99
	fi
	if [ "$KEEP" -eq 1 ]; then echo "kept: $WORK, and the log $EXEDIR/${TAG}*"; return; fi
	rm -rf -- "${WORK:?}"
	rm -f -- "${EXEDIR:?}/${TAG:?}"*DebugLogFile*.txt
}
trap cleanup EXIT
install_snapshot "$INSTALL" "$WORK/install-before.list"

mkdir -p "$USERDATA"
. "$(dirname "$0")/gamepad-game.sh"	# ROOT, the farm the game runs in, and GAME, how it starts: here or on Windows
gamepad_game_setup



MAXFRAMES="${GAMEPAD_COMBO_FRAMES:-1500}"		# the match's frame; the menu walk takes some 700 of the shell's
tap() { echo "w pad $1 down"; echo "n pad $1 up"; }
taps() { local b="$1" n="$2"; for (( i = 0; i < n; ++i )); do tap "$b"; done; }
to_team1() { tap South; taps DPadDown 4; tap South; taps DPadUp 4; tap DPadLeft; }	# Solo Play, Skirmish, slot 1's Team
start() { tap Start; echo "$(( MAXFRAMES + 200 )) quit"; }
{ to_team1; tap DPadRight; tap DPadLeft
  tap South; tap DPadDown; tap DPadDown; tap DPadRight; tap East
  tap South; tap DPadDown; tap South; start; } > "$WORK/pad.txt"
{ to_team1; tap DPadLeft; tap South; tap DPadDown; tap DPadDown; tap East; start; } > "$WORK/cancel.txt"
{ to_team1; start; } > "$WORK/control.txt"

run() {	# run <name> <script>: sets LOG, STATUS, CRC
	local name="$1" script="$2"
	LOG="$EXEDIR/${TAG}${name}DebugLogFile.txt"
	rm -f -- "$LOG"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA/$name" ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ=30 ZH_INPUT_SCRIPT="$script" ZH_AUDIO_BACKEND=null \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
		"${GAME[@]}" -noaudio -win -xres 1280 -yres 800 -quickstart -noshellmap \
		-multiInstance -seed 7 -maxframes "$MAXFRAMES" -logPrefix "${TAG}${name}" > "$WORK/$name.out" 2> "$WORK/$name.err" )
	STATUS=$?
	CRC="$(grep -a "HEADLESS CRC AT LIMIT: " "$LOG" 2>/dev/null | tail -1 | sed 's/.*LIMIT: \(0x[0-9A-F]*\).*/\1/')"
}
mkdir -p "$USERDATA/pad" "$USERDATA/cancel" "$USERDATA/hand" "$USERDATA/control"
for u in pad cancel hand control; do printf 'GamepadAim = no\n' > "$USERDATA/$u/Options.ini"; done
run pad "$WORK/pad.txt"; PAD_STATUS=$STATUS; PAD_CRC=$CRC
LIST="$(grep -a 'GAMEPAD LIST: ' "$LOG" 2>/dev/null | sed 's/.*GAMEPAD LIST: //; s/, at .*//; s/^[^ ]*://')"
# the hand clicks where the pad's run logged them: the combo's drop-down button (its right end) and the picked row
ARMY="$(grep -a -A1 'GAMEPAD FOCUS: .*ComboBoxPlayerTemplate1$' "$LOG" | grep -a 'GAMEPAD FOCUS AT' | tail -1 | sed 's/.*AT: //')"
B="$(grep -a 'GAMEPAD LIST: .*ComboBoxPlayerTemplate1 opened' "$LOG" | head -1 | sed 's/.*choice \([0-9]*\) of.*/\1/')"
N="$(grep -a 'GAMEPAD LIST: .*ComboBoxPlayerTemplate1 opened' "$LOG" | head -1 | sed 's/.* of \([0-9]*\).*/\1/')"
ROW="$(grep -a "GAMEPAD LIST: .*ComboBoxPlayerTemplate1 moved, highlight $(( ${B:-0} + 1 )), choice ${B:-0} of ${N:-0}, at " "$LOG" | tail -1 | sed 's/.*, at //')"
BUTTON_AT="$(printf '%s' "$ARMY" | awk -F'[ ,x]' '{ print $1 + $3 - 10, $2 + int($4 / 2) }')"
ROW_AT="$(printf '%s' "$ROW" | awk -F'[ ,x]' '{ print $1 + int($3 / 2), $2 + int($4 / 2) }')"
{ to_team1; tap DPadLeft
  echo "w mouse move $BUTTON_AT"; echo "n mouse left down $BUTTON_AT"; echo "n mouse left up $BUTTON_AT"
  echo "w mouse move $ROW_AT"; echo "n mouse left down $ROW_AT"; echo "n mouse left up $ROW_AT"
  start; } > "$WORK/hand.txt"
run cancel "$WORK/cancel.txt"; CANCEL_STATUS=$STATUS; CANCEL_CRC=$CRC
run hand "$WORK/hand.txt"; HAND_STATUS=$STATUS; HAND_CRC=$CRC
run control "$WORK/control.txt"; CONTROL_STATUS=$STATUS; CONTROL_CRC=$CRC

# the Army list's own count and first choice (Random) come from its first opening; the rest must follow from them
B=${B:-0}; N=${N:-0}; A="ComboBoxPlayerTemplate1"
EXPECTED_LIST="ComboBoxTeam1 stepped, choice 1 of 5
$A opened, highlight $B, choice $B of $N
$A moved, highlight $((B + 1)), choice $B of $N
$A moved, highlight $((B + 2)), choice $B of $N
$A shut, nothing chosen, highlight $((B + 2)), choice $B of $N
$A opened, highlight $B, choice $B of $N
$A moved, highlight $((B + 1)), choice $B of $N
$A picked, highlight $((B + 1)), choice $((B + 1)) of $N"
status=0
echo "pad:     CRC ${PAD_CRC:-none} at frame $MAXFRAMES (exit $PAD_STATUS)"
echo "cancel:  CRC ${CANCEL_CRC:-none} (exit $CANCEL_STATUS)"
echo "hand:    CRC ${HAND_CRC:-none} (exit $HAND_STATUS), clicking the button at $BUTTON_AT and the row at $ROW_AT"
echo "control: CRC ${CONTROL_CRC:-none} (exit $CONTROL_STATUS)"
if [ "$LIST" != "$EXPECTED_LIST" ]; then
	echo "FAIL: the pad's list did not go as it should:"; diff <(printf '%s\n' "$EXPECTED_LIST") <(printf '%s\n' "$LIST") | sed 's/^/  /'; status=1
else
	echo "PASS: in the open list the highlight moved and the choice stayed, B chose nothing, A picked"
fi
if [ -z "$PAD_CRC" ] || [ -z "$HAND_CRC" ] || [ -z "$CANCEL_CRC" ] || [ -z "$CONTROL_CRC" ]; then
	echo "FAIL: a run gave no CRC at frame $MAXFRAMES"; status=1
elif [ "$PAD_CRC" != "$HAND_CRC" ]; then
	echo "FAIL: the pad's choice and the mouse's give different matches"; status=1
elif [ "$CANCEL_CRC" != "$CONTROL_CRC" ]; then
	echo "FAIL: a list opened, moved through and shut with B changed the match (a highlight chose something)"; status=1
elif [ "$PAD_CRC" = "$CONTROL_CRC" ]; then
	echo "FAIL: the choice changed nothing (pad = control), so the agreement proves nothing"; status=1
else
	echo "PASS: the pad's pick is the mouse's pick, and a list shut with B chose nothing"
fi
if ! verify_install; then exit 99; fi
echo "the install is unchanged"
exit $status
