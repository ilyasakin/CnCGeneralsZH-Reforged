#!/usr/bin/env bash
#
# The first launch's folder question, end to end and headless (the user's rule: no run may need a human).
# PosixMain's test stand-in for SDL's dialog, ZH_TEST_CHOOSER_ANSWERS, feeds the real generals a list of
# answers; each refusal's reason goes to stderr where the dialog's message box would have shown it.
#
#   (a) an empty folder, a folder that is not Zero Hour, and Zero Hour without its base game: each refused with
#       its own reason and asked again; then a farm of the install: accepted, remembered in Registry.ini, and the
#       game runs its frames from it;
#   (b) an empty folder, then cancel: refused, asked again, then the "will close" message, a failed start, and
#       nothing remembered;
#   (c) a second start with (a)'s Registry.ini and no answers at all: nothing asked, the game runs.
#
# The root is always a farm of ZH_DATA_DIR's zerohour (rule 9), HOME an empty folder (no known place can answer),
# the user data folder a scratch one.  Skipped (77) without ZH_DATA_DIR.  What it cannot see: the SDL panel
# itself, which the user saw in front on the MacBook Air on 2026-09-27; this is everything around it.
#
# Each start plays a seeded two-player AI skirmish to 30 frames (so -maxframes ends it), with the build's staged
# overlay as a package carries it, under a 300 s alarm: nothing can wait on a person.
#
# Usage: test_first_launch_chooser.sh <generals> <staged overlay>

set -u
GENERALS="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
[ -n "${ZH_DATA_DIR:-}" ] && [ -d "$ZH_DATA_DIR/zerohour" ] || { echo "skip: ZH_DATA_DIR names no zerohour/"; exit 77; }
[ -d "${2:-}" ] || { echo "skip: no staged overlay at ${2:-} (build zh_overlay)"; exit 77; }
OVERLAY="$(cd "$2" && pwd)"
DATA="$(cd "$ZH_DATA_DIR" && pwd -P)"
EXEDIR="$(dirname "$GENERALS")"
failures=0
check() {	# check <condition> <what>
	if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
}
T="$(mktemp -d "${TMPDIR:-/tmp}/test_first_launch_chooser.XXXXXX")"
T="$(cd "$T" && pwd -P)"
TAG="chooser$$"
trap 'rm -rf -- "$T"; rm -f -- "$EXEDIR/$TAG"*' EXIT

# the farm, and the wrong answers
FARM="$T/farm"
( cd "$DATA/zerohour" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$FARM/$d"; done
( cd "$DATA/zerohour" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$DATA/zerohour/${f#./}" "$FARM/$f"; done
mkdir -p "$T/empty" "$T/notzh" "$T/nobase" "$T/home"
echo readme > "$T/notzh/readme.txt"
: > "$T/nobase/INIZH.big"

start() {	# start <label> <user data> <answers file>: a headless start through the stand-in; sets STATUS
	( cd "$T" && HOME="$T/home" ZH_USER_DATA_DIR="$2/" ZH_TEST_CHOOSER_ANSWERS="$3" perl -e 'alarm shift; exec @ARGV' 300 \
		"$GENERALS" -headless -overlay "$OVERLAY" -quickstart -noshellmap -multiInstance -noFPSLimit -maxframes 30 \
		-randommap 0 2 -autoskirmish 2 -aidiff brutal -seed 0 -observer -logPrefix "$TAG$1" > "$T/$1.out" 2> "$T/$1.err" )
	STATUS=$?
}
installPath() { sed -n 's/^InstallPath *= *//p' "$1/Registry.ini" 2>/dev/null; }
ran() { grep -a -q 'HEADLESS CRC: 0x' "$EXEDIR/$TAG$1DebugLogFile.txt" 2>/dev/null; }

# (a)
printf '%s\n' "$T/empty" "$T/notzh" "$T/nobase" "$FARM" > "$T/a.answers"
mkdir -p "$T/user-a"
start a "$T/user-a" "$T/a.answers"
refusals="$(grep -c 'chooser (test answers): refused, asking again' "$T/a.err")"
check '[ "$refusals" -eq 3 ]' "(a) three answers refused, each asked again ($refusals)"
check 'grep -q "refused, asking again: \"$T/empty\" is not a Command & Conquer Generals Zero Hour folder" "$T/a.err"' \
	"(a) the empty folder: not a Zero Hour folder"
check 'grep -q "refused, asking again: \"$T/notzh\" is not a Command & Conquer Generals Zero Hour folder" "$T/a.err"' \
	"(a) a folder that is not Zero Hour: the same reason, naming it"
check 'grep -q "refused, asking again: \"$T/nobase\" holds Zero Hour, but not the original Command & Conquer Generals" "$T/a.err"' \
	"(a) Zero Hour without its base game: that reason"
check '[ "$(installPath "$T/user-a")" = "$FARM" ]' "(a) the farm accepted and remembered: Registry.ini InstallPath = $(installPath "$T/user-a")"
check '[ $STATUS -eq 0 ] && ran a' "(a) the game ran its frames from it (exit $STATUS)"

# (b)
printf '%s\n' "$T/empty" cancel > "$T/b.answers"
mkdir -p "$T/user-b"
start b "$T/user-b" "$T/b.answers"
check '[ "$(grep -c "refused, asking again" "$T/b.err")" -eq 1 ] && grep -q "chooser (test answers): cancel" "$T/b.err"' \
	"(b) the empty folder refused, asked again, then cancelled"
check 'grep -q "No Zero Hour folder was chosen, so the game will close" "$T/b.err" && [ $STATUS -ne 0 ]' \
	"(b) the \"will close\" message, and the start fails (exit $STATUS)"
check '[ -z "$(installPath "$T/user-b")" ] && ! ran b' "(b) nothing remembered, no game"

# (c)
: > "$T/c.answers"
start c "$T/user-a" "$T/c.answers"
check '! grep -q "chooser (test answers)" "$T/c.err"' "(c) with Registry.ini, nothing is asked"
check '[ $STATUS -eq 0 ] && ran c' "(c) the game runs from the remembered folder (exit $STATUS)"

[ $failures -eq 0 ] || { echo "--- (a) stderr"; head -20 "$T/a.err"; echo "--- (b) stderr"; head -10 "$T/b.err"; }
echo "$failures failure(s)"
[ $failures -eq 0 ]
