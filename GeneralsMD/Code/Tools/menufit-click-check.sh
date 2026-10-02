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
# MenuLayout's mouse (GlobalData.h, GameWindowManagerScript.cpp's parseScreenRect): a click lands on the window that
# is drawn there, Fit or Stretch.  The main menu's Credits button, authored at 540,276 - 748,312 on 800x600, is
# worked out here the way each layout places it at 1280x800, with arithmetic of the script's own:
#   Stretch - 1.6 across and 1.333 down: x 864 - 1196;
#   Fit     - 1.333 both ways, the 4:3 area centred 106.67 pixels in: x 826 - 1104.
# Two points tell them apart: x 845, in Fit's button only, and x 1150, in Stretch's only, both on the button's
# middle line.  Four runs, each one click and the debug log's "Shell:push(Menus/CreditsMenu.wnd)":
#   fit at 845 and stretch at 1150 open the credits; fit at 1150 and stretch at 845 click on nothing.
# A click that opens nothing where the button is drawn, or something where it is not, is a hit test that did not
# follow the layout.  -offscreen and ZH_AUDIO_BACKEND=null: no window, no sound.  Rule 9: a farm of the install,
# listed before and after.
#
# Usage: menufit-click-check.sh --generals <path> [--data <dir>] [--keep]

set -u

GENERALS=""; DATA="${ZH_DATA_DIR:-}"; KEEP=0; TIMEOUT="${MENUFIT_CLICK_TIMEOUT:-300}"
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "menufit-click-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "menufit-click-check: --generals must name the POSIX generals executable" >&2
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
WORK="$(mktemp -d "${TMPDIR:-/tmp}/menufit-click-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "menufit-click-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
USERDATA="$WORK/user"
TAG="mf$$_"

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
. "$(dirname "$0")/gamepad-game.sh"	# ROOT, the farm the game runs in, and GAME, how it starts: here or on Windows
gamepad_game_setup

# The button's two placements at 1280x800, from its authored rectangle
W=1280; H=800
read -r FIT_LEFT FIT_RIGHT STRETCH_LEFT STRETCH_RIGHT TOP BOTTOM <<< "$(awk -v w=$W -v h=$H 'BEGIN {
	sx = w / 800; sy = h / 600; s = (sx < sy) ? sx : sy; left = (w - 800 * s) / 2
	printf "%d %d %d %d %d %d\n", 540 * s + left, 748 * s + left, 540 * sx, 748 * sx, 276 * sy, 312 * sy }')"
Y=$(( (TOP + BOTTOM) / 2 ))
FIT_ONLY=$(( FIT_LEFT + (STRETCH_LEFT - FIT_LEFT) / 2 ))
STRETCH_ONLY=$(( FIT_RIGHT + (STRETCH_RIGHT - FIT_RIGHT) / 2 ))
echo "Credits at ${W}x$H: Fit x $FIT_LEFT - $FIT_RIGHT, Stretch x $STRETCH_LEFT - $STRETCH_RIGHT, y $TOP - $BOTTOM; clicks at x $FIT_ONLY and $STRETCH_ONLY, y $Y"

run() {	# run <name> <MenuLayout> <x>: sets OPENED to 1 when the credits opened, else 0
	local name="$1" layout="$2" x="$3"
	local log="$EXEDIR/${TAG}${name}DebugLogFile.txt" user="$USERDATA/$name"
	mkdir -p "$user"
	printf 'MenuLayout = %s\n' "$layout" > "$user/Options.ini"
	# the main menu comes up some 150 passes in and drops its buttons in after; the pointer moves first, as
	# EA's first entry holds the buttons back until it does, then clicks once they have long stood still
	{ echo "p300 mouse move $(( W / 2 )) $(( H / 2 ))"
	  echo "p340 mouse move $x $Y"
	  echo "p360 mouse left down $x $Y"
	  echo "n mouse left up $x $Y"
	  echo "p600 quit"; } > "$WORK/$name.txt"
	rm -f -- "$log"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$user" ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ=30 ZH_INPUT_SCRIPT="$WORK/$name.txt" ZH_AUDIO_BACKEND=null \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
		"${GAME[@]}" -noaudio -win -xres "$W" -yres "$H" -quickstart -noshellmap \
		-multiInstance -logPrefix "${TAG}${name}" > "$WORK/$name.out" 2> "$WORK/$name.err" )
	STATUS=$?
	OPENED=0
	grep -a -q "Shell:push(Menus/CreditsMenu.wnd)" "$log" 2> /dev/null && OPENED=1
	PLAYED="$(grep -a -c "INPUT SCRIPT: " "$log" 2> /dev/null)"
	echo "$name: MenuLayout $layout, click at $x,$Y: credits $([ $OPENED -eq 1 ] && echo opened || echo "not opened") (exit $STATUS, ${PLAYED:-0} script steps)"
}

status=0
expect() {	# expect <name> <MenuLayout> <x> <1 if the credits must open>
	run "$1" "$2" "$3"
	if [ "${PLAYED:-0}" -lt 4 ]; then
		echo "FAIL: $1 did not play its clicks"; status=1
	elif [ "$OPENED" -ne "$4" ]; then
		echo "FAIL: $1's click $([ "$4" -eq 1 ] && echo "missed the button where it is drawn" || echo "opened the credits where no button is drawn")"; status=1
	fi
}
expect fit-on-button     1 "$FIT_ONLY"     1
expect fit-off-button    1 "$STRETCH_ONLY" 0
expect stretch-on-button 0 "$STRETCH_ONLY" 1
expect stretch-off-button 0 "$FIT_ONLY"    0
[ $status -eq 0 ] && echo "PASS: in both layouts a click opens the credits where the button is drawn, and only there"
if ! verify_install; then exit 99; fi
echo "the install is unchanged"
exit $status
