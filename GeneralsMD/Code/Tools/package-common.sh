# package-common.sh: what the two packagers share, sourced by both (P1's make-macos-app.sh, P3's
# linux-portable.sh).  Each function reports failure by its status; the caller says why, through its own fail.
#
#   package_link_libraries <build.ninja> <paths file>
#       the static libraries on generals' link line, one name a line (lib<name>.a); their paths go into
#       <paths file>, for the checks that read the libraries themselves
#   package_license_entries <table> <names...>
#       the licence entries those libraries need, with the table's "+" lines; status 1, printing the
#       libraries the table does not name, when any is missing
#   hud_check_files <dir>
#       the files under <dir> that turn the HUD overlay off (the user's directive: it stays on), if any
#   license_entry <entry> <folder>
#       copies that entry's files into <folder>; CODE, REPO and BUILD are the caller's

package_link_libraries() {
	python3 - "$1" "$2" <<'LINK_EOF'
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r"^build generals: CXX_EXECUTABLE_LINKER.*?\n  LINK_LIBRARIES = (.*?)\n", text, re.S | re.M)
if not m:
    sys.exit("no generals link line in " + sys.argv[1])
names = sorted(set(re.findall(r"(?:^|[\s/])lib([\w\-+]+)\.a(?=\s|$)", m.group(1))))
print("\n".join(names))
with open(sys.argv[2], "w") as paths:		# the libraries themselves
    paths.write("\n".join(sorted(set(t for t in m.group(1).split() if t.endswith(".a")))) + "\n")
LINK_EOF
}

package_license_entries() {
	local table="$1"; shift
	local lib unlicensed=""
	for lib in "$@"; do
		awk -v l="$lib" '$1 == l { found = 1 } END { exit !found }' "$table" || unlicensed="$unlicensed $lib"
	done
	if [ -n "$unlicensed" ]; then
		echo "$unlicensed"
		return 1
	fi
	{ for lib in "$@"; do awk -v l="$lib" '$1 == l { print $2 }' "$table"; done; awk '$1 == "+" { print $2 }' "$table"; } | sort -u
}

hud_off='^[[:space:]]*ShowHudOverlay[[:space:]]*=[[:space:]]*(no|false|0)([^[:alnum:]]|$)'
hud_check_files() {
	# find -L walks every file, symbolic links followed (the staged overlay links its art): a recursive
	# grep may not follow them, and which grep is first in PATH varies (ugrep's -r does not)
	find -L "$1" -type f -exec grep -a -i -l -E "$hud_off" -- {} + 2>/dev/null
}

license_entry() {
	local L="$2" s="$CODE/Libraries/Source"
	case "$1" in
		game) cp "$REPO/LICENSE.md" "$L/Zero-Hour-Reforged-LICENSE.md";;
		ffmpeg)
			cp "$BUILD/ffmpeg/LICENSE.txt" "$L/FFmpeg-LICENSE.txt" || return 1
			local v; v="$(awk -F= '/^version=/ {print $2; exit}' "$CODE/Tools/ffmpeg-build-posix.sh")"
			printf '%s\n' "FFmpeg $v, statically linked under the LGPL version 2.1 or later." \
				"Source: https://ffmpeg.org/releases/ffmpeg-$v.tar.xz, also kept in this game's repository at" \
				"GeneralsMD/Code/Libraries/Source/FFmpeg/ffmpeg-$v.tar.xz, configured by" \
				"GeneralsMD/Code/Tools/ffmpeg-build-posix.sh (its configure line is the build's)." \
				"To relink the game against another FFmpeg, build it from this game's source:" \
				"https://github.com/olcayseygan/CnCGeneralsZH-Reforged" > "$L/FFmpeg-SOURCE.txt";;
		sdl3) cp "$s/SDL3/LICENSE.txt" "$L/SDL3-LICENSE.txt";;
		shadercross) cp "$s/SDL_shadercross/LICENSE.txt" "$L/SDL_shadercross-LICENSE.txt";;
		freetype)
			cp "$s/freetype/LICENSE.TXT" "$L/FreeType-LICENSE.txt" && cp "$s/freetype/docs/FTL.TXT" "$L/FreeType-FTL.txt";;
		glslang) cp "$s/glslang/LICENSE.txt" "$L/glslang-LICENSE.txt";;
		spirv-cross) cp "$s/SPIRV-Cross/LICENSE" "$L/SPIRV-Cross-LICENSE.txt";;
		litehtml) cp "$s/litehtml/LICENSE" "$L/litehtml-LICENSE.txt";;
		gumbo) cp "$s/litehtml/src/gumbo/LICENSE" "$L/gumbo-LICENSE.txt";;
		miniaudio) cp "$s/miniaudio/LICENSE" "$L/miniaudio-LICENSE.txt";;
		gamespy) cp "$s/GameSpy/LICENSE" "$L/GameSpy-LICENSE.txt";;
		zlib)		# zlib's licence is the comment that opens zlib.h
			awk 'NR == 1 && !/^\/\*/ { exit 1 } { print } /\*\// { exit }' "$s/Compression/ZLib/zlib.h" > "$L/zlib-LICENSE.txt" &&
			grep -q "This notice may not be removed" "$L/zlib-LICENSE.txt";;
		nanosvg) cp "$s/nanosvg/LICENSE.txt" "$L/nanosvg-LICENSE.txt";;
		*) echo "no license_entry named $1" >&2; return 1;;
	esac
}
