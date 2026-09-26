#!/usr/bin/env bash
#
# Tools/stage-overlay.sh keeps the HUD overlay on (a user directive, 2026-09-26): staging refuses a
# GameData.ini line that sets ShowHudOverlay to No, loose or inside an archive the overlay links, and
# stages everything else. Made-up Code/Data and Run folders; no game data.
#   1. ShowHudOverlay = Yes, and a commented-out No       -> staged
#   2. a loose Data/INI/GameData.ini with "ShowHudOverlay = No" -> refused, exit 1, nothing left behind
#   3. the same line in a Reforged*.big the overlay links  -> refused
#   4. "ShowHudOverlay = 0" and "= false", in any case     -> refused
set -u
STAGE="$(cd "$(dirname "$0")/../Tools" && pwd)/stage-overlay.sh"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/test_stage_overlay_hud.XXXXXX")"
trap 'rm -rf -- "${WORK:?}"' EXIT
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

data() {	# data <folder> <GameData.ini text>: a made-up Code/Data
	mkdir -p "$1/INI"
	printf '%s\n' "$2" > "$1/INI/GameData.ini"
	printf 'Patch\n' > "$1/Patch.str"
}

data "$WORK/on" $'GameData\n  ShowHudOverlay = Yes\n  ;ShowHudOverlay = No\nEnd'
"$STAGE" "$WORK/on" "" "$WORK/out1" 2> "$WORK/err1"; status=$?
check '[ $status -eq 0 ] && [ -f "$WORK/out1/Data/INI/GameData.ini" ] && [ -f "$WORK/out1.staged" ]' "Yes, and a commented-out No: staged (exit $status)"

data "$WORK/off" $'GameData\n  ShowHudOverlay = No\nEnd'
"$STAGE" "$WORK/off" "" "$WORK/out2" 2> "$WORK/err2"; status=$?
check '[ $status -eq 1 ] && grep -q "turns the HUD overlay off" "$WORK/err2" && [ ! -e "$WORK/out2" ] && [ ! -e "$WORK/out2.staged" ]' \
	"a loose ShowHudOverlay = No: refused, nothing left (exit $status)"

mkdir -p "$WORK/run"
printf 'BIGF....\0\0Data\\INI\\GameData.ini\0GameData\r\n  ShowHudOverlay = No\r\nEnd\r\n' > "$WORK/run/ReforgedTest.big"
"$STAGE" "$WORK/on" "$WORK/run" "$WORK/out3" 2> "$WORK/err3"; status=$?
check '[ $status -eq 1 ] && grep -q "ReforgedTest.big" "$WORK/err3"' "the line inside a linked Reforged*.big: refused (exit $status)"

for value in 0 FALSE; do
	data "$WORK/v$value" "  showhudoverlay=$value"
	"$STAGE" "$WORK/v$value" "" "$WORK/out_$value" 2> /dev/null; status=$?
	check '[ $status -eq 1 ]' "ShowHudOverlay = $value, in another case: refused (exit $status)"
done

exit $failed
