#!/bin/sh
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
# fetch-art.sh: the upscaled art of a Linux package or the macOS app, fetched the way upstream's Windows
# player gets it.
#
# The packages carry the engine only; the Reforged*.big art (over a gigabyte) comes from the art release,
# the same source and check as Tools/vendor.sh (and vendor.ps1): art.json lists each file with its size and
# sha256, and a file is used only once its sha256 matches.  A Linux package installs this script as
# share/zero-hour-reforged/fetch-art.sh and its launcher starts it in the background before the game; the
# macOS app carries it as Contents/Resources/fetch-art.sh and the game starts it (PosixMain.cpp).  So a
# first start plays at the original textures while the art arrives, and the next one has it.
#
#   <user data>/ReforgedArt/              the verified files, which the game reads as an overlay
#                                         (PosixMain.cpp, appendUserArtOverlay); .verified records them
#   <user data>/ReforgedArt.download/     downloads in progress (resumed on the next start)
#   <user data>/Logs/art-fetch.log        what the last fetch did
#
# <user data> is the game's own (EarlyOptions.h): $ZH_USER_DATA_DIR; else on macOS ~/Library/Application
# Support, and elsewhere $XDG_DATA_HOME (an absolute one) or ~/.local/share; then "Command and Conquer
# Generals Zero Hour Data".  POSIX sh, and only what a desktop has: curl or wget, sha256sum or shasum, and
# flock where there is one (a lock folder where there is not, as on macOS).
#
# Environment: ZHR_NO_ART_FETCH=1 fetches nothing; ZHR_ART_URL replaces the release's address (a test
# passes a file:// folder), and art-source.txt beside the script the built-in one (see ART_URL below).  Offline, or on any failure, it logs why and leaves what is verified in place;
# the next start tries again.  A file in ReforgedArt that art.json no longer lists is removed.
#
# Usage: fetch-art.sh [--background]
#   --background   detach, lower its priority, and write only the log (the launcher's way)
# Exit status: 0 the art is complete; 1 it is not (the log says why); 2 bad usage.

# Where the art is: ZHR_ART_URL; else the first line of art-source.txt beside this script, which the package or the
# app was built with (make-macos-app.sh and linux-portable.sh --art-url, CMake's ZH_ART_URL); else upstream's
# art-latest release.  Only an https:// or file:// address is taken from the file.
source_file="$(dirname "$0")/art-source.txt"
default_url="https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/download/art-latest"
if [ -f "$source_file" ]; then
	built="$(head -n 1 "$source_file" | tr -d '\r')"
	case "$built" in https://*|file://*) default_url="${built%/}";; esac
fi
ART_URL="${ZHR_ART_URL:-$default_url}"
leaf="Command and Conquer Generals Zero Hour Data"
if [ -n "${ZH_USER_DATA_DIR:-}" ]; then
	data="${ZH_USER_DATA_DIR%/}"
elif [ "$(uname -s)" = Darwin ]; then
	data="$HOME/Library/Application Support/$leaf"
else
	case "${XDG_DATA_HOME:-}" in
		/*) data="${XDG_DATA_HOME%/}/$leaf";;
		*) data="$HOME/.local/share/$leaf";;
	esac
fi
art="$data/ReforgedArt"
stage="$data/ReforgedArt.download"
log="$data/Logs/art-fetch.log"

case "${1:-}" in
	--background)
		[ "${ZHR_NO_ART_FETCH:-0}" = 1 ] && exit 0
		mkdir -p "$data/Logs" 2>/dev/null || exit 1
		nohup nice -n 10 sh "$0" > "$log" 2>&1 < /dev/null &
		exit 0;;
	"") ;;
	*) echo "fetch-art: unknown argument $1" >&2; exit 2;;
esac

say() { echo "fetch-art: $*"; }
[ "${ZHR_NO_ART_FETCH:-0}" = 1 ] && { say "ZHR_NO_ART_FETCH=1: nothing fetched"; exit 0; }
mkdir -p "$art" "$stage" || { say "cannot make $art"; exit 1; }

# one fetch at a time: a second start while the first still downloads leaves it to the first
if command -v flock > /dev/null 2>&1; then
	exec 9> "$stage/.lock" || exit 1
	flock -n 9 || { say "another fetch is running"; exit 0; }
else		# no flock (macOS): a lock folder holding its owner's pid; one whose owner is gone is taken over
	lockdir="$stage/.lock.d"
	if ! mkdir "$lockdir" 2> /dev/null; then
		owner="$(cat "$lockdir/pid" 2> /dev/null)"
		if [ -n "$owner" ] && kill -0 "$owner" 2> /dev/null; then say "another fetch is running"; exit 0; fi
		rm -rf "$lockdir" && mkdir "$lockdir" 2> /dev/null || { say "another fetch is running"; exit 0; }
	fi
	echo $$ > "$lockdir/pid"
	trap 'rm -rf "$lockdir"' EXIT
fi

if command -v curl > /dev/null 2>&1; then
	get() { curl -fsSL --connect-timeout 20 --retry 2 -o "$2" "$1"; }
	resume() { curl -fsSL --connect-timeout 20 --retry 2 -C - -o "$2" "$1"; }
elif command -v wget > /dev/null 2>&1; then
	get() { wget -q -T 20 -t 3 -O "$2" "$1"; }
	resume() { wget -q -T 20 -t 3 -c -O "$2" "$1"; }
else
	say "neither curl nor wget: the game plays at its original textures"; exit 1
fi
if command -v sha256sum > /dev/null 2>&1; then
	sha() { sha256sum "$1" | cut -d ' ' -f 1; }
elif command -v shasum > /dev/null 2>&1; then
	sha() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
	say "neither sha256sum nor shasum: nothing can be checked, so nothing is fetched"; exit 1
fi
size_of() { wc -c < "$1" | tr -d ' '; }

say "$(date -u +%Y-%m-%dT%H:%M:%SZ): art from $ART_URL into $art"
if ! get "$ART_URL/art.json" "$stage/art.json.new"; then
	# offline, or no art published there (upstream's art-latest is not, at times): not an error to worry anyone
	say "no public art source reachable ($ART_URL): playing at the original textures; the next start tries again"
	rm -f "$stage/art.json.new"
	exit 1
fi

# art.json -> "name size sha256" lines.  Its names become file names here, so each must be a plain
# Reforged*.big name, its size digits and its hash 64 hex digits; anything else refuses the whole list.
tr '{},' '\n\n\n' < "$stage/art.json.new" | sed -n \
	-e 's/^[[:space:]]*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)"[[:space:]]*$/name \1/p' \
	-e 's/^[[:space:]]*"size"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/size \1/p' \
	-e 's/^[[:space:]]*"sha256"[[:space:]]*:[[:space:]]*"\([^"]*\)"[[:space:]]*$/sha256 \1/p' \
	| awk '$1 == "name" { n = $2; s = ""; h = "" } $1 == "size" { s = $2 } $1 == "sha256" { h = tolower($2) }
		n != "" && s != "" && h != "" { print n, s, h; n = "" }' > "$stage/wanted"
bad="$(awk '$1 !~ /^Reforged[A-Za-z0-9_.-]*\.big$/ || $2 !~ /^[0-9]+$/ || $3 !~ /^[0-9a-f]+$/ || length($3) != 64' "$stage/wanted")"
if [ ! -s "$stage/wanted" ] || [ -n "$bad" ]; then
	say "art.json lists no usable files (or a malformed one: $bad): nothing changed"; exit 1
fi

touch "$art/.verified"
status=0
while read -r name size hash; do
	if [ -f "$art/$name" ] && [ "$(size_of "$art/$name")" = "$size" ] && grep -qx "$name $size $hash" "$art/.verified"; then
		continue		# verified before, and still that size
	fi
	part="$stage/$name.part"
	say "downloading $name ($(( (size + 524288) / 1048576 )) MB)"
	if ! resume "$ART_URL/$name" "$part"; then
		say "$name: the download failed; it resumes next start"; status=1; continue
	fi
	got="$(sha "$part")"
	if [ "$got" != "$hash" ]; then
		# loudly, and without keeping the file: art that is quietly wrong is a game that looks wrong
		say "$name: sha256 $got, but art.json says $hash: removed"; rm -f "$part"; status=1; continue
	fi
	mv -f "$part" "$art/$name" || { say "cannot move $name into $art"; status=1; continue; }
	{ grep -v "^$name " "$art/.verified"; echo "$name $size $hash"; } > "$art/.verified.new" && mv -f "$art/.verified.new" "$art/.verified"
	say "$name verified"
done < "$stage/wanted"

# what art.json no longer lists goes, so a withdrawn file is never read again
for f in "$art"/Reforged*.big; do
	[ -f "$f" ] || continue
	name="${f##*/}"
	if ! awk -v n="$name" '$1 == n { found = 1 } END { exit !found }' "$stage/wanted"; then
		rm -f "$f" && say "$name: no longer in art.json, removed"
		grep -v "^$name " "$art/.verified" > "$art/.verified.new"; mv -f "$art/.verified.new" "$art/.verified"
	fi
done
mv -f "$stage/art.json.new" "$art/art.json"
[ $status -eq 0 ] && say "the art is complete" || say "the art is incomplete: the next start tries again"
exit $status
