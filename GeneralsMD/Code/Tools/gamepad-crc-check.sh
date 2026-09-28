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
# G1's test 2 (docs/mac-port/tasks/G1-gamepad.md, §5): the same skirmish, driven once by a pad and once
# by hand, must end on the same CRC, and both must differ from the match left alone.
#
# Three runs of one fixed-seed skirmish, each with no window at all (-offscreen, which starts SDL's gamepads
# only for ZH_INPUT_SCRIPT, so a worker's own pad cannot join) and no sound (-noaudio), timed out and
# killed with their process group:
#   pad   - ZH_INPUT_SCRIPT plays a virtual pad: select the idle dozer (a back button, the command map's
#           I), a move order (East, the right button), make group 1 (right shoulder + D-pad up, Ctrl+1),
#           a second move order, and stop (West, S) - the local player's only unit at the start is the dozer;
#   hand  - the same through SDL's own mouse and key events, on the same logic frames;
#   none  - no input: the armed control, so equal CRCs cannot come from input that did nothing.
# PASS: pad and hand agree on the HEADLESS CRC and frame, and none differs.
#
# Rule 9: the install is only read, through a farm of links, and listed before and after
# (install-guard.sh); the overlay is staged into the work folder; the user data folder is in it too.
#
# Usage: gamepad-crc-check.sh --generals <path> [--data <dir>] [--maxframes 600] [--keep]
#   --data defaults to ZH_DATA_DIR, a folder holding zerohour/.  Exit 77 without it.
set -u

GENERALS=""; DATA="${ZH_DATA_DIR:-}"; MAXFRAMES=600; KEEP=0; TIMEOUT="${GAMEPAD_CRC_TIMEOUT:-300}"
RADIAL_SITE="${GAMEPAD_RADIAL_SITE:-600 450}"		# where the radial's building goes, in the game's 1024x768 pixels
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--maxframes) MAXFRAMES="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "gamepad-crc-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "gamepad-crc-check: --generals must name the POSIX generals executable" >&2
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
WORK="$(mktemp -d "${TMPDIR:-/tmp}/gamepad-crc-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "gamepad-crc-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
ROOT="$WORK/root"
USERDATA="$WORK/user"
TAG="gp$$_"

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
	if [ "$KEEP" -eq 1 ]; then echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"; return; fi
	rm -rf -- "${WORK:?}"
	rm -f -- "${EXEDIR:?}/${TAG:?}"*DebugLogFile*.txt
}
trap cleanup EXIT
install_snapshot "$INSTALL" "$WORK/install-before.list"

mkdir -p "$ROOT" "$USERDATA"
# Aim assist (GamepadAim.h) moves a resting pad's pointer onto a unit near it, so the pad would click beside
# the hand's pixel: this check is about the same click at the same pixel, so the assist is off here
printf 'GamepadAim = no\n' > "$USERDATA/Options.ini"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
OVERLAY="$WORK/overlay"
"$(dirname "$0")/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$OVERLAY"

# ---- the two scripts: the same commands on the same frames ----------------------------------------
cat > "$WORK/pad.txt" <<'SCRIPT'
60 mouse move 512 384
90 pad LeftPaddle1 down
92 pad LeftPaddle1 up
120 pad East down
122 pad East up
150 pad RightShoulder down
152 pad DPadUp down
154 pad DPadUp up
156 pad RightShoulder up
200 mouse move 700 500
210 pad East down
212 pad East up
300 pad West down
302 pad West up
SCRIPT
cat > "$WORK/hand.txt" <<'SCRIPT'
60 mouse move 512 384
90 key I down
92 key I up
120 mouse right down 512 384
122 mouse right up 512 384
150 key Left_Ctrl down
152 key 1 down
154 key 1 up
156 key Left_Ctrl up
200 mouse move 700 500
210 mouse right down 700 500
212 mouse right up 700 500
300 key S down
302 key S up
SCRIPT

run_game() {	# run_game <name> [script]: sets RUN_CRC, RUN_FRAME, RUN_PLAYED, RUN_STATUS
	local prefix="$TAG$1" script="${2:-}"
	local log="$EXEDIR/${prefix}DebugLogFile.txt"
	rm -f -- "$log"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" ZH_UNATTENDED=1 ZH_INPUT_SCRIPT="$script" \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
		"$GENERALS" -offscreen -noaudio -win -xres 1024 -yres 768 -root "$ROOT" -overlay "$OVERLAY" -quickstart -noshellmap \
		-multiInstance -noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$prefix" \
		-randommap 0 2 -autoskirmish 2 -aidiff brutal -seed 0 \
		> "$WORK/${prefix}.out" 2> "$WORK/${prefix}.err" )
	RUN_STATUS=$?
	RUN_CRC=""; RUN_FRAME=""; RUN_PLAYED=0
	[ -f "$log" ] || return
	local line
	line="$(grep -a 'HEADLESS CRC: 0x' "$log" | tail -1)"
	RUN_CRC="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1/p')"
	RUN_FRAME="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\2/p')"
	RUN_PLAYED="$(grep -a 'INPUT SCRIPT: frame ' "$log" | grep -a -c -v 'not understood')"
}

run_game pad "$WORK/pad.txt";   PAD_CRC="$RUN_CRC"; PAD_FRAME="$RUN_FRAME"; PAD_PLAYED="$RUN_PLAYED"; PAD_STATUS="$RUN_STATUS"
run_game hand "$WORK/hand.txt"; HAND_CRC="$RUN_CRC"; HAND_FRAME="$RUN_FRAME"; HAND_PLAYED="$RUN_PLAYED"; HAND_STATUS="$RUN_STATUS"
run_game none "";               NONE_CRC="$RUN_CRC"; NONE_FRAME="$RUN_FRAME"

echo "pad:  CRC ${PAD_CRC:-none} at frame ${PAD_FRAME:-none}, $PAD_PLAYED of 14 actions played (exit $PAD_STATUS)"
echo "hand: CRC ${HAND_CRC:-none} at frame ${HAND_FRAME:-none}, $HAND_PLAYED of 14 actions played (exit $HAND_STATUS)"
echo "none: CRC ${NONE_CRC:-none} at frame ${NONE_FRAME:-none}"
status=0
if [ -z "$PAD_CRC" ] || [ -z "$HAND_CRC" ] || [ -z "$NONE_CRC" ]; then
	echo "FAIL: a run gave no result"; status=1
elif [ "$PAD_PLAYED" != "14" ] || [ "$HAND_PLAYED" != "14" ]; then
	echo "FAIL: the scripts were not played whole"; status=1
elif [ "$PAD_CRC" != "$HAND_CRC" ] || [ "$PAD_FRAME" != "$HAND_FRAME" ]; then
	echo "FAIL: the pad and the hand disagree"; status=1
elif [ "$PAD_CRC" = "$NONE_CRC" ]; then
	echo "FAIL: the input changed nothing (the armed control), so the agreement proves nothing"; status=1
else
	echo "PASS: the pad and the hand end on the same CRC, and the input changed the match"
fi

# ---- the radial menu (GamepadRadial.h): the same match, and then a building from the ring ------------------
# The pad holds Y, tilts the stick up (the ring's first sector: the dozer's first command button), lets Y go,
# and places the building with A; the hand clicks that button, at the centre the pad's run logged, and places
# it with a click.  They must agree, and differ from the plain pad run above: the difference is the building.
{ cat "$WORK/pad.txt"; cat <<'SCRIPT'
330 pad North down
332 pad axis LeftY -32767
334 pad axis LeftY 0
336 pad North up
360 mouse move RADIAL_X RADIAL_Y
370 pad South down
372 pad South up
SCRIPT
} | sed "s/RADIAL_X RADIAL_Y/$RADIAL_SITE/" > "$WORK/pad-radial.txt"
run_game padradial "$WORK/pad-radial.txt"
PR_CRC="$RUN_CRC"; PR_FRAME="$RUN_FRAME"; PR_PLAYED="$RUN_PLAYED"; PR_STATUS="$RUN_STATUS"
PRESSED="$(grep -a 'GAMEPAD RADIAL: pressed' "$EXEDIR/${TAG}padradialDebugLogFile.txt" 2>/dev/null | tail -1)"
BUTTON_AT="$(printf '%s' "$PRESSED" | sed -n 's/.*centre \([0-9]*\),\([0-9]*\).*/\1 \2/p')"
HR_CRC=""; HR_FRAME=""; HR_PLAYED=0; HR_STATUS=""
if [ -n "$BUTTON_AT" ]; then
	{ cat "$WORK/hand.txt"; printf '%s\n' "336 mouse move $BUTTON_AT" "336 mouse left down $BUTTON_AT" "338 mouse left up $BUTTON_AT" \
		"360 mouse move $RADIAL_SITE" "370 mouse left down $RADIAL_SITE" "372 mouse left up $RADIAL_SITE"; } > "$WORK/hand-radial.txt"
	run_game handradial "$WORK/hand-radial.txt"
	HR_CRC="$RUN_CRC"; HR_FRAME="$RUN_FRAME"; HR_PLAYED="$RUN_PLAYED"; HR_STATUS="$RUN_STATUS"
fi
echo "radial: ${PRESSED#*GAMEPAD RADIAL: }"
echo "pad, radial:  CRC ${PR_CRC:-none} at frame ${PR_FRAME:-none}, $PR_PLAYED of 21 actions played (exit $PR_STATUS)"
echo "hand, button: CRC ${HR_CRC:-none} at frame ${HR_FRAME:-none}, $HR_PLAYED of 20 actions played (exit $HR_STATUS)"
if [ -z "$BUTTON_AT" ]; then
	echo "FAIL: the pad's radial pressed no button (no GAMEPAD RADIAL line in its log)"; status=1
elif [ -z "$PR_CRC" ] || [ -z "$HR_CRC" ]; then
	echo "FAIL: a radial run gave no result"; status=1
elif [ "$PR_PLAYED" != "21" ] || [ "$HR_PLAYED" != "20" ]; then
	echo "FAIL: the radial scripts were not played whole"; status=1
elif [ "$PR_CRC" != "$HR_CRC" ] || [ "$PR_FRAME" != "$HR_FRAME" ]; then
	echo "FAIL: the radial and the hand's click on the button disagree"; status=1
elif [ "$PR_CRC" = "$PAD_CRC" ]; then
	echo "FAIL: the radial's building changed nothing (the plain pad run ends the same), so the agreement proves nothing"; status=1
else
	echo "PASS: a building from the radial menu is the building a click on the command bar makes"
fi
if ! verify_install; then exit 99; fi
echo "the install is unchanged"
exit $status
