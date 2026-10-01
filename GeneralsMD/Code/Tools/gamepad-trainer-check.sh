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
# The trainer on a pad: Start opens the pause menu, whose Trainer key (shown where the cheats are on offer) opens
# the console's cheat panel (Window/Html/Cheats.html), and the D-pad goes over the panel's keys as over a menu's
# widgets (GamepadFocus's box source), A clicks the focused key and B shuts the panel.  A virtual pad walks a
# fixed path, in the style of gamepad-dpad-check.sh: up to the menu's top, down to Trainer, A; on the panel down
# from the first key (money's first amount) to the row under it (general's points' first), A; then B.  The focus
# must land where the list below says, step by step (--record prints it instead of checking).
#
# Three runs of one fixed-seed skirmish, each compared at -maxframes' own frame (HEADLESS CRC AT LIMIT):
#   cheat - the walk, the points key pressed: the log says the cheat ran, once.  General's points are in the CRC
#           (Player::crc's science purchase points) where money is not, so the cheat shows in the match's CRC;
#   walk  - the same walk with no A on the panel: no cheat, and the match must be the plain one's - the menu's
#           pause and the panel's focus change nothing in the logic;
#   none  - no input.
# PASS: the focus followed the list in both walks, B shut the panel in both, the cheat ran in the first only, its
# CRC differs from the plain match's, and the walk's equals it.
# -offscreen and ZH_AUDIO_BACKEND=null: no window, no sound.  Rule 9: a farm of the install, listed before and after.
#
# Usage: gamepad-trainer-check.sh --generals <path> [--data <dir>] [--keep] [--record]

set -u

GENERALS=""; DATA="${ZH_DATA_DIR:-}"; KEEP=0; RECORD=0; TIMEOUT="${GAMEPAD_TRAINER_TIMEOUT:-600}"
MAXFRAMES=1200		# 40 s of match at the pace a screen draws: the walk's own wait on each step is well inside it
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		--record) RECORD=1; shift;;
		*) echo "gamepad-trainer-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "gamepad-trainer-check: --generals must name the POSIX generals executable" >&2
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
WORK="$(mktemp -d "${TMPDIR:-/tmp}/gamepad-trainer-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "gamepad-trainer-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
USERDATA="$WORK/user"
TAG="gt$$_"

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

mkdir -p "$USERDATA"
printf 'GamepadAim = no\n' > "$USERDATA/Options.ini"
. "$(dirname "$0")/gamepad-game.sh"	# ROOT, the farm the game runs in, and GAME, how it starts
gamepad_game_setup

tap() { echo "$1 pad $2 down"; echo "n pad $2 up"; }		# tap <when> <button>: "w" where an edge rightly moves nothing
# walk <press the points key: 1 or 0>
walk() {
	echo "60 mouse move 512 384"
	tap 90 Start									# the pause menu (the match stands still under it)
	for i in 1 2 3 4 5 6; do tap w DPadUp; done		# its top key, whichever had the focus
	tap w DPadDown; tap w DPadDown					# Options, then Trainer
	tap s South										# the menu goes, the panel comes up on its first key
	tap s DPadDown									# the row under it: general's points, its first amount
	[ "$1" -eq 1 ] && tap w South					# pressed: the cheat runs as the mouse's click runs it
	tap w East										# and the panel shut
}
walk 1 > "$WORK/cheat.txt"
walk 0 > "$WORK/walk.txt"

run_game() {	# run_game <name> [script]: sets RUN_LOG, RUN_CRC, RUN_FRAME, RUN_PLAYED, RUN_AGAIN, RUN_STATUS
	local prefix="$TAG$1" script="${2:-}"
	RUN_LOG="$EXEDIR/${prefix}DebugLogFile.txt"
	rm -f -- "$RUN_LOG"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ=30 ZH_INPUT_SCRIPT="$script" ZH_AUDIO_BACKEND=null \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
		"${GAME[@]}" -noaudio -win -xres 1280 -yres 800 -quickstart -noshellmap \
		-multiInstance -maxframes "$MAXFRAMES" -logPrefix "$prefix" \
		-randommap 0 2 -autoskirmish 2 -aidiff easy -seed 0 \
		> "$WORK/$1.out" 2> "$WORK/$1.err" )
	RUN_STATUS=$?
	RUN_CRC=""; RUN_FRAME=""; RUN_PLAYED=0; RUN_AGAIN=0
	[ -f "$RUN_LOG" ] || return
	local line
	line="$(grep -a 'HEADLESS CRC AT LIMIT: 0x' "$RUN_LOG" | tail -1)"
	RUN_CRC="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC AT LIMIT: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1/p')"
	RUN_FRAME="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC AT LIMIT: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\2/p')"
	RUN_PLAYED="$(grep -a 'INPUT SCRIPT: frame ' "$RUN_LOG" | grep -a -c -v 'not understood')"
	RUN_AGAIN="$(grep -a -c 'INPUT SCRIPT AGAIN' "$RUN_LOG")"
}
focus_of() { grep -a 'GAMEPAD FOCUS: ' "$1" 2>/dev/null | sed 's/.*GAMEPAD FOCUS: //'; }
cheats_in() { grep -a 'Cheat panel: [a-z]' "$1" 2>/dev/null | sed 's/.*Cheat panel: //'; }	# not the keys' places ("...")

run_game cheat "$WORK/cheat.txt"
CHEAT_LOG="$RUN_LOG"; CHEAT_CRC="$RUN_CRC"; CHEAT_FRAME="$RUN_FRAME"; CHEAT_PLAYED="$RUN_PLAYED"; CHEAT_AGAIN="$RUN_AGAIN"; CHEAT_STATUS="$RUN_STATUS"
run_game walk "$WORK/walk.txt"
WALK_LOG="$RUN_LOG"; WALK_CRC="$RUN_CRC"; WALK_FRAME="$RUN_FRAME"; WALK_PLAYED="$RUN_PLAYED"; WALK_AGAIN="$RUN_AGAIN"; WALK_STATUS="$RUN_STATUS"
run_game none ""
NONE_CRC="$RUN_CRC"; NONE_FRAME="$RUN_FRAME"; NONE_STATUS="$RUN_STATUS"

CHEAT_FOCUS="$(focus_of "$CHEAT_LOG")"; WALK_FOCUS="$(focus_of "$WALK_LOG")"
CHEAT_RAN="$(cheats_in "$CHEAT_LOG")"; WALK_RAN="$(cheats_in "$WALK_LOG")"
echo "cheat: CRC ${CHEAT_CRC:-none} at frame ${CHEAT_FRAME:-none}, $CHEAT_PLAYED of $(grep -c . "$WORK/cheat.txt") steps, $CHEAT_AGAIN again (exit $CHEAT_STATUS); cheats run: ${CHEAT_RAN:-none}"
echo "walk:  CRC ${WALK_CRC:-none} at frame ${WALK_FRAME:-none}, $WALK_PLAYED of $(grep -c . "$WORK/walk.txt") steps, $WALK_AGAIN again (exit $WALK_STATUS); cheats run: ${WALK_RAN:-none}"
echo "none:  CRC ${NONE_CRC:-none} at frame ${NONE_FRAME:-none} (exit $NONE_STATUS)"

if [ "$RECORD" -eq 1 ]; then
	echo "---- the cheat run's focus"; printf '%s\n' "$CHEAT_FOCUS"
	echo "---- the walk's focus"; printf '%s\n' "$WALK_FOCUS"
	if ! verify_install; then exit 99; fi
	exit 0
fi

# ---- what the pad must give (recorded, and looked over) --------------------------------------------------------
EXPECTED_WALK="QuitMenu.wnd QuitMenu.wnd:ButtonSaveLoad
QuitMenu.wnd QuitMenu.wnd:ButtonOptions
QuitMenu.wnd QuitMenu.wnd:ButtonTrainer
Cheats.html money 1000
Cheats.html points 1
Cheats.html shut"
EXPECTED_CHEAT="QuitMenu.wnd QuitMenu.wnd:ButtonSaveLoad
QuitMenu.wnd QuitMenu.wnd:ButtonOptions
QuitMenu.wnd QuitMenu.wnd:ButtonTrainer
Cheats.html money 1000
Cheats.html points 1
Cheats.html points 1 pressed
Cheats.html shut"

status=0
for name in cheat walk; do
	if [ "$name" = cheat ]; then st=$CHEAT_STATUS; played=$CHEAT_PLAYED; again=$CHEAT_AGAIN; got="$CHEAT_FOCUS"; want="$EXPECTED_CHEAT"
	else st=$WALK_STATUS; played=$WALK_PLAYED; again=$WALK_AGAIN; got="$WALK_FOCUS"; want="$EXPECTED_WALK"; fi
	steps="$(grep -c . "$WORK/$name.txt")"
	if [ "$st" -ne 0 ] && [ "$st" -ne 1 ]; then
		echo "FAIL: the $name run did not end by itself (exit $st)"; status=1
	elif [ "$played" != "$steps" ] || [ "$again" != "0" ]; then
		echo "FAIL: the $name run's walk did not play whole and clean ($played of $steps steps, $again again)"; status=1
	elif [ "$got" != "$want" ]; then
		echo "FAIL: the pad's focus in the $name run did not follow the list:"
		diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | sed 's/^/  /'
		status=1
	else
		echo "PASS: the $name run: Start, the pause menu's Trainer, the panel's keys and B went where the list says"
	fi
done
if [ "$CHEAT_RAN" != "points done" ] || [ -n "$WALK_RAN" ]; then
	echo "FAIL: the cheat must run once in the cheat run (it ran: ${CHEAT_RAN:-nothing}) and never in the walk (${WALK_RAN:-nothing})"; status=1
else
	echo "PASS: A on the panel's points key ran the general's points cheat, and the walk alone ran none"
fi
if [ -z "$CHEAT_CRC" ] || [ -z "$WALK_CRC" ] || [ -z "$NONE_CRC" ] || [ "$CHEAT_FRAME" != "$MAXFRAMES" ] \
		|| [ "$WALK_FRAME" != "$MAXFRAMES" ] || [ "$NONE_FRAME" != "$MAXFRAMES" ]; then
	echo "FAIL: a run gave no CRC at frame $MAXFRAMES"; status=1
elif [ "$WALK_CRC" != "$NONE_CRC" ]; then
	echo "FAIL: the walk with no cheat changed the match ($WALK_CRC, the plain match $NONE_CRC)"; status=1
elif [ "$CHEAT_CRC" = "$NONE_CRC" ]; then
	echo "FAIL: the cheat did not change the match (both $NONE_CRC)"; status=1
else
	echo "PASS: the cheat changed the match, and the walk without it left the match as it was"
fi
if ! verify_install; then exit 99; fi
echo "the install is unchanged"
exit $status
