#!/bin/bash
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
# test_fetch_art.sh: Tools/fetch-art.sh, the Linux packages' and the macOS app's art download, against a local file://
# release (no network): an art.json and one .big, as upstream's art-latest release lays them out.
#
#   1. a first fetch: the .big downloaded, its sha256 checked, moved into <user data>/ReforgedArt, recorded;
#   2. a second fetch: nothing downloaded (it was verified before, and is still that size);
#   3. armed: a changed file under an unchanged art.json entry is fetched again; a published hash the file
#      does not have is refused, and nothing wrong is left in ReforgedArt or the download folder;
#   4. armed: a name that is not a plain Reforged*.big (a path) refuses the whole list, and nothing changes;
#   5. a file art.json no longer lists is removed;
#   6. ZHR_NO_ART_FETCH=1 fetches nothing; an unreachable release changes nothing and says so;
#   7. the launcher's --background mode returns at once and leaves the same result behind;
#   8. without ZH_USER_DATA_DIR, the platform's own folder, as the game's (EarlyOptions.h): ~/Library/Application
#      Support on macOS, $XDG_DATA_HOME or ~/.local/share elsewhere.
# Needs curl or wget, and sha256sum or shasum; flock is used where there is one (Linux), a lock folder
# where there is not (macOS).  Skipped (77) without them.

set -u
CODE="$(cd "$(dirname "$0")/.." && pwd)"
FETCH="$CODE/Tools/fetch-art.sh"
command -v sha256sum > /dev/null || command -v shasum > /dev/null || { echo "skip: neither sha256sum nor shasum"; exit 77; }
command -v curl > /dev/null || command -v wget > /dev/null || { echo "skip: neither curl nor wget"; exit 77; }
# a work folder that could not be made is the end of the run: going on with T empty would write under /
T="$(mktemp -d "${TMPDIR:-/tmp}/fetch-art.XXXXXX")" || T=""
if [ -z "$T" ] || [ ! -d "$T" ]; then echo "test_fetch_art: cannot make a work folder under ${TMPDIR:-/tmp}" >&2; exit 2; fi
trap 'rm -rf -- "$T"' EXIT
failures=0
check() { if eval "$1"; then echo "PASS $2"; else echo "FAIL $2"; failures=$((failures + 1)); fi; }

REL="$T/release"; USER_DATA="$T/user"; ART="$USER_DATA/ReforgedArt"
mkdir -p "$REL"
head -c 300000 /dev/urandom > "$REL/ReforgedTest.big"
publish() {	# publish <name> <size> <sha256> [<name> <size> <sha256>...]: art.json as the release writes it
	{ echo '{'; echo '  "files": ['
	  local first=1
	  while [ $# -gt 0 ]; do
		[ $first -eq 1 ] || echo '    },'
		first=0
		printf '    {\n      "name": "%s",\n      "size": %s,\n      "sha256": "%s"\n' "$1" "$2" "$3"
		shift 3
	  done
	  echo '    }'; echo '  ]'; echo '}'; } > "$REL/art.json"
}
size() { wc -c < "$1" | tr -d ' '; }
if command -v sha256sum > /dev/null; then sha() { sha256sum "$1" | cut -d ' ' -f 1; }; else sha() { shasum -a 256 "$1" | cut -d ' ' -f 1; }; fi
fetch() { ZH_USER_DATA_DIR="$USER_DATA" ZHR_ART_URL="file://$REL" sh "$FETCH" "$@" > "$T/out" 2>&1; STATUS=$?; }

# 1.
publish ReforgedTest.big "$(size "$REL/ReforgedTest.big")" "$(sha "$REL/ReforgedTest.big")"
fetch
check '[ $STATUS -eq 0 ] && cmp -s "$REL/ReforgedTest.big" "$ART/ReforgedTest.big"' "1. fetched and verified into ReforgedArt (exit $STATUS)"
check 'grep -q "^ReforgedTest.big $(size "$REL/ReforgedTest.big") $(sha "$REL/ReforgedTest.big")$" "$ART/.verified" && cmp -s "$REL/art.json" "$ART/art.json"' \
	"1. recorded in .verified, and art.json kept beside it"
check '[ -z "$(ls "$USER_DATA/ReforgedArt.download" | grep -v "^wanted$")" ]' "1. nothing left in the download folder"

# 2.
fetch
check '[ $STATUS -eq 0 ] && ! grep -q downloading "$T/out"' "2. a second fetch downloads nothing"

# 3.
head -c 300000 /dev/urandom > "$REL/ReforgedTest.big"		# republished: a new file and hash
publish ReforgedTest.big "$(size "$REL/ReforgedTest.big")" "$(sha "$REL/ReforgedTest.big")"
fetch
check '[ $STATUS -eq 0 ] && grep -q "downloading ReforgedTest.big" "$T/out" && cmp -s "$REL/ReforgedTest.big" "$ART/ReforgedTest.big"' \
	"3. a republished file is fetched again"
good="$(sha "$ART/ReforgedTest.big")"
publish ReforgedTest.big "$(size "$REL/ReforgedTest.big")" "$(printf '%064d' 0)"
fetch
check '[ $STATUS -ne 0 ] && grep -q "sha256 .* but art.json says" "$T/out"' "3. armed: a hash the file does not have is refused (exit $STATUS)"
check '[ "$(sha "$ART/ReforgedTest.big")" = "$good" ] && [ ! -e "$USER_DATA/ReforgedArt.download/ReforgedTest.big.part" ]' \
	"3. armed: the verified copy stays, and the refused download is not kept"

# 4.
cp "$ART/art.json" "$T/art.json.before"
publish ../ReforgedEvil.big 10 "$(printf '%064d' 0)" ReforgedTest.big "$(size "$REL/ReforgedTest.big")" "$(sha "$REL/ReforgedTest.big")"
fetch
check '[ $STATUS -ne 0 ] && grep -q "no usable files" "$T/out" && [ ! -e "$USER_DATA/ReforgedEvil.big" ] && cmp -s "$T/art.json.before" "$ART/art.json"' \
	"4. armed: a name with a path in it refuses the whole list, and nothing changes (exit $STATUS)"

# 5.
cp "$REL/ReforgedTest.big" "$REL/ReforgedOther.big"
publish ReforgedOther.big "$(size "$REL/ReforgedOther.big")" "$(sha "$REL/ReforgedOther.big")"
fetch
check '[ $STATUS -eq 0 ] && [ ! -e "$ART/ReforgedTest.big" ] && [ -f "$ART/ReforgedOther.big" ] && ! grep -q "^ReforgedTest.big " "$ART/.verified"' \
	"5. a file art.json no longer lists is removed, and its record with it"

# 6.
rm -rf "$USER_DATA"
ZHR_NO_ART_FETCH=1 fetch
check '[ $STATUS -eq 0 ] && [ ! -e "$ART" ]' "6. ZHR_NO_ART_FETCH=1: nothing fetched, nothing made"
ZH_USER_DATA_DIR="$USER_DATA" ZHR_ART_URL="file://$T/nowhere" sh "$FETCH" > "$T/out" 2>&1; STATUS=$?
check '[ $STATUS -ne 0 ] && grep -q "cannot read art.json" "$T/out" && [ -z "$(ls "$ART" 2>/dev/null | grep -v "^\.verified$")" ]' \
	"6. an unreachable release: it says so, and nothing is used (exit $STATUS)"

# 7.
rm -rf "$USER_DATA"
ZH_USER_DATA_DIR="$USER_DATA" ZHR_ART_URL="file://$REL" sh "$FETCH" --background; STATUS=$?
for i in $(seq 50); do grep -q "the art is complete" "$USER_DATA/Logs/art-fetch.log" 2>/dev/null && break; sleep 0.2; done
check '[ $STATUS -eq 0 ] && grep -q "the art is complete" "$USER_DATA/Logs/art-fetch.log" && cmp -s "$REL/ReforgedOther.big" "$ART/ReforgedOther.big"' \
	"7. --background returns at once; its log says complete and the file is in place"

# 8.
mkdir -p "$T/home"
if [ "$(uname -s)" = Darwin ]; then own="$T/home/Library/Application Support/Command and Conquer Generals Zero Hour Data"
else own="$T/home/.local/share/Command and Conquer Generals Zero Hour Data"; fi
env -u ZH_USER_DATA_DIR -u XDG_DATA_HOME HOME="$T/home" ZHR_ART_URL="file://$REL" sh "$FETCH" > "$T/out" 2>&1; STATUS=$?
check '[ $STATUS -eq 0 ] && cmp -s "$REL/ReforgedOther.big" "$own/ReforgedArt/ReforgedOther.big"' \
	"8. without ZH_USER_DATA_DIR: into the platform's own user data folder ($(uname -s))"

echo "$failures failure(s)"
[ $failures -eq 0 ]
