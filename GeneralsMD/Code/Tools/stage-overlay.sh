#!/usr/bin/env bash
#
# Stages the fork's overlay (P1, decision 9): the folder the game searches before its install root,
# exactly as the app bundle will carry it in Contents/Resources/Overlay. One script, so the build's
# zh_overlay target, the app bundle and every harness (replay-check.sh, overlay-crc-check.sh,
# root-readonly-check.sh, packaging-resolution-check.sh) stage the same files the same way.
#
#   stage-overlay.sh <Code/Data> <Run folder, or ""> <out> [--dev]
#
# The shipped overlay, from generals' Windows post-build list (CMakeLists.txt) less what package.bat
# leaves out, as P1's task file infers it (to be confirmed against upstream's release tooling):
#   Data/INI/, Data/Patch.str, Data/Scripts/, Data/Turkish/, Install_Final.bmp, Art/Textures/, Window/
# and the fork's art archives, Reforged*.big from the Run folder when vendor.sh has fetched them,
# LINKED here (1.65 GB): the bundle step copies them through the links.
# --dev stages the development-only folders instead, Scenarios/ and Cinema/ (-scenario, -cinema),
# which never ship: a second overlay the tools pass, so E1 runs exactly the shipped one.
#
# <out> is removed and made anew first, so every file is written into a folder of its own and never
# through a link (the lesson of P1 step 2's incident).
set -euo pipefail

if [ $# -lt 3 ]; then
	echo "usage: $0 <Code/Data> <Run folder, or \"\"> <out> [--dev]" >&2
	exit 2
fi
data="$(cd "$1" && pwd)"
run="${2:-}"
out="$3"
dev="${4:-}"

rm -rf -- "$out"
mkdir -p "$out"

copy() {	# copy <file or folder under Code/Data> <where under out>
	local src="$data/$1" dst="$out/$2"
	if [ -d "$src" ]; then
		mkdir -p "$dst"
		( cd "$src" && find . -type f ! -name '._*' ) | while IFS= read -r f; do
			mkdir -p "$(dirname "$dst/$f")"
			cp -- "$src/$f" "$dst/$f"
		done
	elif [ -f "$src" ]; then
		mkdir -p "$(dirname "$dst")"
		cp -- "$src" "$dst"
	fi
}

if [ "$dev" = "--dev" ]; then
	copy Scenarios Scenarios
	copy Cinema Cinema
else
	copy INI Data/INI
	copy Patch.str Data/Patch.str
	copy Scripts Data/Scripts
	copy Turkish Data/Turkish
	copy Install_Final.bmp Install_Final.bmp
	copy Art/Textures Art/Textures
	copy Window Window
	if [ -n "$run" ] && [ -d "$run" ]; then
		for big in "$run"/Reforged*.big; do
			[ -f "$big" ] && ln -s "$(cd "$(dirname "$big")" && pwd)/$(basename "$big")" "$out/$(basename "$big")"
		done
	fi
fi
touch "$out.staged"	# beside, not inside: the game would see a file inside as a loose file
