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
# G1's menus on a pad: a virtual pad walks the shell's
# screens, and the focus must land where the list of screens says, step by step.
#
# One run, with no window at all (-offscreen, which starts SDL's gamepads only for ZH_INPUT_SCRIPT, so a
# worker's own pad cannot join) and no sound (-offscreen and ZH_AUDIO_BACKEND=null; -noaudio is a Debug build's
# switch), timed out and killed with its process group.  The shell's menus move in on the clock, so each
# press waits for the menu to stand still ("s" lines, SdlInputScript.h) rather than for a pass count, and
# ZH_OFFSCREEN_HZ keeps the passes at a screen's pace.  The engine logs each change of focus
# ("GAMEPAD FOCUS: <screen> <widget>"), and the walk must give exactly EXPECTED below:
#   the main menu starts on Solo Play (kept once the pad is first used, so the log begins with the first
#   press's move: down from Solo Play is Multiplayer); down, down, down reaches Options; A opens it on its first widget;
#   RB turns to the second page, on that page's first widget, and down moves within it; B cancels back
#   to the main menu with Options still focused; B there goes to Exit, and down wraps to Solo Play; A opens
#   its pane on the first side; A on it opens the difficulty pane on Medium; B twice backs out to the main
#   menu; A and down four times reach Skirmish; A opens skirmish setup on Start Game.
#
# Rule 9: the install is only read, through a farm of links, and listed before and after
# (install-guard.sh); the overlay is staged into the work folder; the user data folder is in it too.
#
# Usage: gamepad-menu-check.sh --generals <path> [--data <dir>] [--keep]
#   --data defaults to ZH_DATA_DIR, a folder holding zerohour/.  Exit 77 without it.
set -u

GENERALS=""; DATA="${ZH_DATA_DIR:-}"; KEEP=0; TIMEOUT="${GAMEPAD_MENU_TIMEOUT:-600}"	# a backstop: the walk waits on the menus
HZ="${GAMEPAD_MENU_HZ:-30}"	# the passes' pace; 5 is the slow-machine control: a pass is 200 ms, past the menu's 350 ms repeat in two
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "gamepad-menu-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "gamepad-menu-check: --generals must name the POSIX generals executable" >&2
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
WORK="$(mktemp -d "${TMPDIR:-/tmp}/gamepad-menu-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "gamepad-menu-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
USERDATA="$WORK/user"
TAG="gm$$_"

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

# ---- the walk: each press waits until the menu has stood still for a second since the one before (focus,
# screen and every widget's place: GamepadFocus::isSettled), and its release follows in the same pass.  A
# walk timed in passes misrouted under load: a release that came 350 ms after its press, on a loaded
# worker, let the menu's own repeat press the D-pad a second time, and the focus went to Credits.
tap() { echo "s pad $1 down"; echo "n pad $1 up"; }
{
	tap DPadDown; tap DPadDown; tap DPadDown		# Multiplayer, Load, Options
	tap South						# Options, on its first page's first widget
	tap RightShoulder					# the second page, on its first widget
	tap DPadDown						# the widget below it
	tap East						# Cancel: the main menu, Options kept
	tap East						# B on the main menu's top level: Exit
	tap DPadDown						# the column wraps: Solo Play
	tap South						# the Solo Play pane, on its first side
	tap South						# that side's difficulty pane, on Medium
	tap East						# its Back: the Solo Play pane
	tap East						# its Back: the main menu, on Solo Play
	tap South						# the Solo Play pane again
	tap DPadDown; tap DPadDown; tap DPadDown; tap DPadDown	# to Skirmish
	tap South						# skirmish setup, on Start Game
	echo "s quit"
} > "$WORK/walk.txt"

EXPECTED="MainMenu.wnd MainMenu.wnd:ButtonMultiplayer
MainMenu.wnd MainMenu.wnd:ButtonLoadReplay
MainMenu.wnd MainMenu.wnd:ButtonOptions
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMonitor
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxDetail
MainMenu.wnd MainMenu.wnd:ButtonOptions
MainMenu.wnd MainMenu.wnd:ButtonExit
MainMenu.wnd MainMenu.wnd:ButtonSinglePlayer
MainMenu.wnd MainMenu.wnd:ButtonUSA
MainMenu.wnd MainMenu.wnd:ButtonMedium
MainMenu.wnd MainMenu.wnd:ButtonUSA
MainMenu.wnd MainMenu.wnd:ButtonSinglePlayer
MainMenu.wnd MainMenu.wnd:ButtonUSA
MainMenu.wnd MainMenu.wnd:ButtonGLA
MainMenu.wnd MainMenu.wnd:ButtonChina
MainMenu.wnd MainMenu.wnd:ButtonChallenge
MainMenu.wnd MainMenu.wnd:ButtonSkirmish
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ButtonStart"

LOG="$EXEDIR/${TAG}walkDebugLogFile.txt"
rm -f -- "$LOG"
( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ="$HZ" ZH_INPUT_SCRIPT="$WORK/walk.txt" ZH_AUDIO_BACKEND=null \
	perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
	"${GAME[@]}" -noaudio -win -xres 1280 -yres 800 -quickstart -noshellmap \
	-multiInstance -logPrefix "${TAG}walk" > "$WORK/walk.out" 2> "$WORK/walk.err" )
STATUS=$?

# the screen keys are layout files, some with the shell's Menus/ folder: the file name is enough
GOT="$(grep -a 'GAMEPAD FOCUS: ' "$LOG" 2>/dev/null | sed 's/.*GAMEPAD FOCUS: //; s|^Menus/||')"
PLAYED="$(grep -a 'INPUT SCRIPT: frame ' "$LOG" 2>/dev/null | grep -a -c -v 'not understood')"
# a step the menu dropped is pressed again and logged apart (SdlInputScript.h): a diagnostic that must count none, or
# a player's press would have been dropped there too
AGAIN="$(grep -a -c 'INPUT SCRIPT AGAIN' "$LOG" 2>/dev/null)"; AGAIN=${AGAIN:-0}
ACTIONS="$(grep -c . "$WORK/walk.txt")"
echo "exit $STATUS; $PLAYED of $ACTIONS actions played, $AGAIN pressed again; the focus went:"
printf '%s\n' "$GOT" | sed 's/^/  /'
status=0
if [ "$STATUS" -ne 0 ]; then
	echo "FAIL: the run did not end by the script's quit (exit $STATUS)"; status=1
elif [ "$PLAYED" != "$ACTIONS" ]; then
	echo "FAIL: the script was not played whole"; status=1
elif [ "$AGAIN" != "0" ]; then
	echo "FAIL: the menu dropped $AGAIN press(es) the walk made once it stood still, and pressed again (INPUT SCRIPT AGAIN in the log)"; status=1
elif [ "$GOT" != "$EXPECTED" ]; then
	echo "FAIL: the focus did not follow the list; expected:"
	printf '%s\n' "$EXPECTED" | sed 's/^/  /'
	status=1
else
	echo "PASS: the pad walked the menus and the focus landed where the list of screens says, every step"
fi

# ---- a press held through a transition (GamepadFocus::update): EA's main menu drops a press for about a second
# after a pane opens; the pad's B pressed then is held and pressed when the transition ends.  The walk opens the
# difficulty pane and presses B three passes later: the log must show B held, then pressed, and the focus back on
# the Solo Play pane.  The control (ZH_TEST_NO_HOLD=1) makes the same press, dropped as a mouse's click is: the
# focus stays on the difficulty pane.
run_hold() {	# run_hold <name> [VAR=value]: sets HOLD_LOG, HOLD_STATUS
	local name="$1"; shift
	HOLD_LOG="$EXEDIR/${TAG}${name}DebugLogFile.txt"
	rm -f -- "$HOLD_LOG"
	( cd "$ROOT" && env "$@" ZH_USER_DATA_DIR="$USERDATA" ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ="$HZ" ZH_INPUT_SCRIPT="$WORK/hold.txt" ZH_AUDIO_BACKEND=null \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
		"${GAME[@]}" -noaudio -win -xres 1280 -yres 800 -quickstart -noshellmap \
		-multiInstance -logPrefix "${TAG}${name}" > "$WORK/$name.out" 2> "$WORK/$name.err" )
	HOLD_STATUS=$?
}
{
	tap South; tap South						# the Solo Play pane, then a side's difficulty pane
	echo "d3 pad East down"; echo "n pad East up"			# B three passes later, while the pane is still opening
	echo "s quit"
} > "$WORK/hold.txt"
run_hold hold
HELD="$(grep -a -c 'GAMEPAD HELD: B, pressed during a transition' "$HOLD_LOG" 2>/dev/null)"
APPLIED="$(grep -a 'GAMEPAD HELD: B pressed now' "$HOLD_LOG" 2>/dev/null | tail -1 | sed 's/.*pressed now, //')"
LAST="$(grep -a 'GAMEPAD FOCUS: ' "$HOLD_LOG" 2>/dev/null | tail -1 | sed 's/.*GAMEPAD FOCUS: //; s|^Menus/||')"
run_hold holdcontrol ZH_TEST_NO_HOLD=1
CONTROL_LAST="$(grep -a 'GAMEPAD FOCUS: ' "$HOLD_LOG" 2>/dev/null | tail -1 | sed 's/.*GAMEPAD FOCUS: //; s|^Menus/||')"
CONTROL_HELD="$(grep -a -c 'GAMEPAD HELD' "$HOLD_LOG" 2>/dev/null)"
echo "hold: exit $HOLD_STATUS; B held ${HELD:-0} time(s), pressed ${APPLIED:-never}; the focus ended on ${LAST:-nothing}"
echo "hold control (no holding): the focus ended on ${CONTROL_LAST:-nothing}, ${CONTROL_HELD:-0} hold(s)"
case "$LAST" in *ButtonUSA|*ButtonGLA|*ButtonChina|*ButtonTraining|*ButtonChallenge|*ButtonSkirmish|*ButtonSingleBack) back_ok=1;; *) back_ok=0;; esac
case "$CONTROL_LAST" in *ButtonEasy|*ButtonMedium|*ButtonHard|*ButtonDiffBack) control_ok=1;; *) control_ok=0;; esac
if [ "${HELD:-0}" -lt 1 ] || [ -z "$APPLIED" ] || [ "$back_ok" -ne 1 ]; then
	echo "FAIL: B pressed while the difficulty pane opened was not held and pressed when it could be"; status=1
elif [ "$control_ok" -ne 1 ] || [ "${CONTROL_HELD:-0}" != "0" ]; then
	echo "FAIL: the control: without holding the same press must be dropped, as a mouse's click is (the focus stays on the difficulty pane)"; status=1
else
	echo "PASS: B pressed during the pane's transition was held and pressed after it; without holding it is dropped"
fi
if ! verify_install; then exit 99; fi
echo "the install is unchanged"
exit $status
