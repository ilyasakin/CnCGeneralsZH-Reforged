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
# G1's D-pad, screen by screen: a virtual pad presses fixed D-pad
# paths over the main menu, every page of Options, the Solo Play pane, skirmish setup, and a match's command card,
# and the focus must land where the lists below say, step by step.  The lists are what GamepadFocus::pickNeighbourBox
# gives: rows and columns, a 45-degree cone, the column kept through a shorter row; each was looked over by eye when
# it was recorded (--record prints them instead of checking).
#
# The menus are one run, each press when the menu has stood still ("s" steps), its release in the same pass ("n");
# the command card is a second run, a skirmish, its steps keyed to logic frames and logged by grid place ("GAMEPAD BAR: Q").  Both
# -offscreen and ZH_AUDIO_BACKEND=null: no window, no sound.  Rule 9: a farm of the install, listed before and after.
#
# Usage: gamepad-dpad-check.sh --generals <path> [--data <dir>] [--keep] [--record]

set -u

GENERALS=""; DATA="${ZH_DATA_DIR:-}"; KEEP=0; RECORD=0; TIMEOUT="${GAMEPAD_DPAD_TIMEOUT:-900}"	# a backstop: the walk waits on the menus
HZ="${GAMEPAD_MENU_HZ:-30}"	# the passes' pace; 5 is the slow-machine control: a pass is 200 ms, past the menu's 350 ms repeat in two
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		--record) RECORD=1; shift;;
		*) echo "gamepad-dpad-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "gamepad-dpad-check: --generals must name the POSIX generals executable" >&2
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
WORK="$(mktemp -d "${TMPDIR:-/tmp}/gamepad-dpad-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "gamepad-dpad-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
ROOT="$WORK/root"
USERDATA="$WORK/user"
TAG="gd$$_"

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

mkdir -p "$ROOT" "$USERDATA"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
OVERLAY="$WORK/overlay"
"$(dirname "$0")/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$OVERLAY"


tap() { echo "w pad $1 down"; echo "n pad $1 up"; }		# "w": an edge rightly moves nothing
taps() { local b="$1" n="$2"; for (( i = 0; i < n; ++i )); do tap "$b"; done; }
# each Options page, a column at a time, so every widget on it is reached (the check below holds the walk to that):
# the column the page starts in, down; the next one up from the bottom row, where right goes (right from a column's
# top does not); the page again from its start (the shoulders out and back), the third column up from the bottom row;
# the page again, the column to the left of the start, down (Controls starts in its middle column)
reenter() { if [ "${1:-}" = first ]; then tap RightShoulder; tap LeftShoulder; else tap LeftShoulder; tap RightShoulder; fi; }
page() { taps DPadDown 14; tap DPadRight; taps DPadUp 14; reenter "${1:-}"; taps DPadDown 14; taps DPadRight 2; taps DPadUp 14; reenter "${1:-}"; tap DPadLeft; taps DPadDown 14; }
{
	taps DPadDown 6; taps DPadUp 3					# the main menu's column, round and back to Options
	tap South; page first					# Options, its first page (the shoulders out and back go right, then left)
	for p in 2 3 4 5 6 7; do tap RightShoulder; [ "$RECORD" -eq 1 ] && echo "w shot"; page; done		# and the other six
	tap East; taps DPadUp 3; tap South				# back; up to Solo Play; its pane
	taps DPadDown 4; tap South					# down to Skirmish; skirmish setup, on Start Game
	# the Lobby Settings tab by the shoulder (its place among the player rows is a near tie the D-pad does not always
	# break the same way), then that page's settings, the supply pile limit first, and back up through the rows
	tap RightShoulder; taps DPadLeft 6; taps DPadDown 6; taps DPadUp 8; taps DPadRight 4; taps DPadDown 8
	echo "s quit"
} > "$WORK/menus.txt"
{
	echo "60 mouse move 512 384"
	echo "90 pad LeftShoulder down"; echo "n pad LeftShoulder up"			# the idle dozer
	echo "150 pad axis RightTrigger 32767"; echo "n pad axis RightTrigger -32768"	# a tap: the command grid, its first place
	f=180
	for b in DPadRight DPadRight DPadRight DPadDown DPadLeft DPadLeft DPadUp DPadRight DPadDown DPadRight DPadRight DPadRight DPadUp; do
		echo "$f pad $b down"; echo "n pad $b up"; f=$(( f + 15 ))
	done
	echo "$(( f + 10 )) quit"
} > "$WORK/card.txt"

run() {	# run <name> <script> [args...]: sets LOG, STATUS
	local name="$1" script="$2"; shift 2
	LOG="$EXEDIR/${TAG}${name}DebugLogFile.txt"
	rm -f -- "$LOG"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ=30 ZH_INPUT_SCRIPT="$script" ZH_AUDIO_BACKEND=null \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm shift; system @ARGV; exit($? >> 8)' "$TIMEOUT" \
		"$GENERALS" -offscreen -noaudio -win -xres 1280 -yres 800 -root "$ROOT" -overlay "$OVERLAY" -quickstart -noshellmap \
		-multiInstance -logPrefix "${TAG}${name}" "$@" > "$WORK/$name.out" 2> "$WORK/$name.err" )
	STATUS=$?
}
printf 'GamepadAim = no\n' > "$USERDATA/Options.ini"
run menus "$WORK/menus.txt"
MENU_STATUS=$STATUS
MENUS="$(grep -a 'GAMEPAD FOCUS: ' "$LOG" 2>/dev/null | sed 's/.*GAMEPAD FOCUS: //; s|^Menus/||')"
MENU_PLAYED="$(grep -a -c 'INPUT SCRIPT: frame ' "$LOG" 2>/dev/null)"; MENU_AGAIN="$(grep -a -c 'INPUT SCRIPT AGAIN' "$LOG" 2>/dev/null)"
run card "$WORK/card.txt" -randommap 0 2 -autoskirmish 2 -aidiff easy -seed 0 -maxframes 420
CARD_STATUS=$STATUS
CARD="$(grep -a 'GAMEPAD BAR: ' "$LOG" 2>/dev/null | sed 's/.*GAMEPAD BAR: //')"

if [ "$RECORD" -eq 1 ]; then
	echo "---- menus (exit $MENU_STATUS, $MENU_PLAYED of $(grep -c . "$WORK/menus.txt") steps, ${MENU_AGAIN:-0} again)"; printf '%s\n' "$MENUS"
	echo "---- card (exit $CARD_STATUS)"; printf '%s\n' "$CARD"
	if ! verify_install; then exit 99; fi
	exit 0
fi

# ---- what the D-pad must give (recorded, and looked over) ------------------------------------------------------
EXPECTED_MENUS="MainMenu.wnd MainMenu.wnd:ButtonMultiplayer
MainMenu.wnd MainMenu.wnd:ButtonLoadReplay
MainMenu.wnd MainMenu.wnd:ButtonOptions
MainMenu.wnd MainMenu.wnd:ButtonCredits
MainMenu.wnd MainMenu.wnd:ButtonExit
MainMenu.wnd MainMenu.wnd:ButtonSinglePlayer
MainMenu.wnd MainMenu.wnd:ButtonExit
MainMenu.wnd MainMenu.wnd:ButtonCredits
MainMenu.wnd MainMenu.wnd:ButtonOptions
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMonitor
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxResolution
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxWindowMode
OptionsMenu.wnd OptionsMenu.wnd:CheckVSync
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:CheckSmoothMotion
OptionsMenu.wnd OptionsMenu.wnd:SliderGamma
OptionsMenu.wnd OptionsMenu.wnd:TabControls
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMonitor
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxResolution
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxWindowMode
OptionsMenu.wnd OptionsMenu.wnd:CheckVSync
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMonitor
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxResolution
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxWindowMode
OptionsMenu.wnd OptionsMenu.wnd:CheckVSync
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxDetail
OptionsMenu.wnd OptionsMenu.wnd:LowResSlider
OptionsMenu.wnd OptionsMenu.wnd:ParticleCapSlider
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:SliderAnisotropy
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxTextureFilter
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxBloomThreshold
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxBloom
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMSAA
OptionsMenu.wnd OptionsMenu.wnd:TabControls
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMonitor
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxDetail
OptionsMenu.wnd OptionsMenu.wnd:LowResSlider
OptionsMenu.wnd OptionsMenu.wnd:ParticleCapSlider
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:CheckShowProps
OptionsMenu.wnd OptionsMenu.wnd:CheckSmoothWater
OptionsMenu.wnd OptionsMenu.wnd:CheckGroundLighting
OptionsMenu.wnd OptionsMenu.wnd:CheckCloudShadows
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxMonitor
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxDetail
OptionsMenu.wnd OptionsMenu.wnd:LowResSlider
OptionsMenu.wnd OptionsMenu.wnd:ParticleCapSlider
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:Check3DShadows
OptionsMenu.wnd OptionsMenu.wnd:Check2DShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckInfantryShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckProjectileShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckPropShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckParticleShadows
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:CheckBehindBuilding
OptionsMenu.wnd OptionsMenu.wnd:CheckHeatEffects
OptionsMenu.wnd OptionsMenu.wnd:CheckTreeSway
OptionsMenu.wnd OptionsMenu.wnd:CheckExtraAnimations
OptionsMenu.wnd OptionsMenu.wnd:TabControls
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:Check3DShadows
OptionsMenu.wnd OptionsMenu.wnd:Check2DShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckInfantryShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckProjectileShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckPropShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckParticleShadows
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:CheckNoDynamicLOD
OptionsMenu.wnd OptionsMenu.wnd:CheckParticleBounce
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxSmoke
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:CheckClassicGraphics
OptionsMenu.wnd OptionsMenu.wnd:Check3DShadows
OptionsMenu.wnd OptionsMenu.wnd:Check2DShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckInfantryShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckProjectileShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckPropShadows
OptionsMenu.wnd OptionsMenu.wnd:CheckParticleShadows
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:SliderMusicVolume
OptionsMenu.wnd OptionsMenu.wnd:SliderSFXVolume
OptionsMenu.wnd OptionsMenu.wnd:SliderVoiceVolume
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:TabControls
OptionsMenu.wnd OptionsMenu.wnd:Check3DShadows
OptionsMenu.wnd OptionsMenu.wnd:SliderMusicVolume
OptionsMenu.wnd OptionsMenu.wnd:SliderSFXVolume
OptionsMenu.wnd OptionsMenu.wnd:SliderVoiceVolume
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:Check3DShadows
OptionsMenu.wnd OptionsMenu.wnd:SliderMusicVolume
OptionsMenu.wnd OptionsMenu.wnd:SliderSFXVolume
OptionsMenu.wnd OptionsMenu.wnd:SliderVoiceVolume
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:Retaliation
OptionsMenu.wnd OptionsMenu.wnd:CheckDoubleClickAttackMove
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:CheckChromaLighting
OptionsMenu.wnd OptionsMenu.wnd:SliderDragTolerance
OptionsMenu.wnd OptionsMenu.wnd:CheckGamepadSwapConfirm
OptionsMenu.wnd OptionsMenu.wnd:CheckGamepadAim
OptionsMenu.wnd OptionsMenu.wnd:CheckGamepad
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:SliderMusicVolume
OptionsMenu.wnd OptionsMenu.wnd:Retaliation
OptionsMenu.wnd OptionsMenu.wnd:CheckDoubleClickAttackMove
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:CheckChromaLighting
OptionsMenu.wnd OptionsMenu.wnd:SliderDragTolerance
OptionsMenu.wnd OptionsMenu.wnd:CheckGamepadSwapConfirm
OptionsMenu.wnd OptionsMenu.wnd:CheckGamepadAim
OptionsMenu.wnd OptionsMenu.wnd:CheckGamepad
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:SliderMusicVolume
OptionsMenu.wnd OptionsMenu.wnd:Retaliation
OptionsMenu.wnd OptionsMenu.wnd:CheckZoomToCursor
OptionsMenu.wnd OptionsMenu.wnd:CheckIsometricCamera
OptionsMenu.wnd OptionsMenu.wnd:CheckStartAtMaxZoom
OptionsMenu.wnd OptionsMenu.wnd:SliderCloserZoom
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxHealthBars
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxPlayerColors
OptionsMenu.wnd OptionsMenu.wnd:CheckOrderLines
OptionsMenu.wnd OptionsMenu.wnd:CheckEmptyBuildingPips
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:CheckNetBox
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxIncomeRate
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxHudScale
OptionsMenu.wnd OptionsMenu.wnd:TabControls
OptionsMenu.wnd OptionsMenu.wnd:Retaliation
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxHealthBars
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxPlayerColors
OptionsMenu.wnd OptionsMenu.wnd:CheckOrderLines
OptionsMenu.wnd OptionsMenu.wnd:CheckEmptyBuildingPips
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxLanguage
OptionsMenu.wnd OptionsMenu.wnd:TabNetwork
OptionsMenu.wnd OptionsMenu.wnd:Retaliation
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxHealthBars
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxPlayerColors
OptionsMenu.wnd OptionsMenu.wnd:CheckOrderLines
OptionsMenu.wnd OptionsMenu.wnd:CheckEmptyBuildingPips
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxOnlineIP
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxIP
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:CheckSendDelay
OptionsMenu.wnd OptionsMenu.wnd:ButtonFirewallRefresh
OptionsMenu.wnd OptionsMenu.wnd:TextEntryFirewallPortOverride
OptionsMenu.wnd OptionsMenu.wnd:TabControls
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxHealthBars
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxOnlineIP
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxIP
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
OptionsMenu.wnd OptionsMenu.wnd:ButtonBack
OptionsMenu.wnd OptionsMenu.wnd:ButtonAccept
OptionsMenu.wnd OptionsMenu.wnd:TextEntryHTTPProxy
OptionsMenu.wnd OptionsMenu.wnd:TabGameplay
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxHealthBars
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxOnlineIP
OptionsMenu.wnd OptionsMenu.wnd:ComboBoxIP
OptionsMenu.wnd OptionsMenu.wnd:ButtonDefaults
MainMenu.wnd MainMenu.wnd:ButtonOptions
MainMenu.wnd MainMenu.wnd:ButtonLoadReplay
MainMenu.wnd MainMenu.wnd:ButtonMultiplayer
MainMenu.wnd MainMenu.wnd:ButtonSinglePlayer
MainMenu.wnd MainMenu.wnd:ButtonUSA
MainMenu.wnd MainMenu.wnd:ButtonGLA
MainMenu.wnd MainMenu.wnd:ButtonChina
MainMenu.wnd MainMenu.wnd:ButtonChallenge
MainMenu.wnd MainMenu.wnd:ButtonSkirmish
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ButtonStart
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ButtonBack
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxSupplyPileLimit
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxIncomeSharing
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxSuperweapons
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:TabInfo
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxPlayer7
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxPlayer6
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxPlayer5
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxColor5
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate5
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:CheckBoxUnitLimit
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ComboBoxTechRespawn
SkirmishGameOptionsMenu.wnd SkirmishGameOptionsMenu.wnd:ButtonStart"
EXPECTED_CARD="Q
W
E
R
B
X
Z
Q
W
S
E
R
B
R"

status=0
echo "menus: exit $MENU_STATUS, $MENU_PLAYED of $(grep -c . "$WORK/menus.txt") steps played, ${MENU_AGAIN:-0} pressed again"
if [ "$MENU_STATUS" -ne 0 ] || [ "$MENU_PLAYED" != "$(grep -c . "$WORK/menus.txt")" ] || [ "${MENU_AGAIN:-0}" != "0" ]; then
	echo "FAIL: the menu walk did not play whole and clean"; status=1
elif [ "$MENUS" != "$EXPECTED_MENUS" ]; then
	echo "FAIL: the D-pad's focus in the menus did not follow the list:"
	diff <(printf '%s\n' "$EXPECTED_MENUS") <(printf '%s\n' "$MENUS") | sed 's/^/  /'
	status=1
else
	echo "PASS: the D-pad went where the list says on the main menu, all seven Options pages, Solo Play and skirmish setup"
fi
# Reach, whatever the list says: every widget the Options catalog names (OptionsCatalog.cpp's OPT_WND rows) and the lobby's
# supply pile limit must be among the widgets the walk focused.  A new option with no way there on a pad fails here by
# name, rather than as a list nobody re-reads.
UNREACHED=""
for widget in $(grep -o 'OPT_WND( "[A-Za-z0-9]*" )' "$CODE/GameEngine/Source/Common/OptionsCatalog.cpp" | sed 's/OPT_WND( "//; s/" )//' | sort -u) \
		SkirmishGameOptionsMenu.wnd:ComboBoxSupplyPileLimit; do
	case "$widget" in *:*) name="$widget";; *) name="OptionsMenu.wnd:$widget";; esac
	printf '%s\n' "$MENUS" | grep -q -x ".* $name" || UNREACHED="$UNREACHED $name"
done
if [ -n "$UNREACHED" ]; then
	echo "FAIL: not reached on the pad:$UNREACHED"; status=1
else
	echo "PASS: every option in the catalog, and the lobby's supply pile limit, is reached on the pad"
fi
if [ "$CARD_STATUS" -ne 0 ] && [ "$CARD_STATUS" -ne 1 ]; then
	echo "FAIL: the match with the command card did not end by itself (exit $CARD_STATUS)"; status=1
elif [ "$CARD" != "$EXPECTED_CARD" ]; then
	echo "FAIL: the D-pad on the command card did not follow the list:"
	diff <(printf '%s\n' "$EXPECTED_CARD") <(printf '%s\n' "$CARD") | sed 's/^/  /'
	status=1
else
	echo "PASS: the D-pad went where the list says on the command card"
fi
if ! verify_install; then exit 99; fi
echo "the install is unchanged"
exit $status
