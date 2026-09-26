#!/usr/bin/env bash
#
# make-macos-app.sh: builds "Zero Hour Reforged.app" (P1 step 5, the task file's section 3), the
# macos_app CMake target's one step.
#
#   Contents/Info.plist, PkgInfo
#   Contents/MacOS/generals            stripped; its dSYM beside the bundle (<out>.dSYM), not in it, for C5
#   Contents/Resources/AppIcon.icns    from Main/Generals.ico's 48 px image (soft on Retina: an open item)
#   Contents/Resources/Overlay/        the staged overlay (zh_overlay), its art archives as APFS clones
#   Contents/Resources/Licenses/       from macos-app-licenses.txt, checked against the link line
#
# Refused, before anything is written:
#   - a static library on generals' link line (build.ninja) that macos-app-licenses.txt does not name;
#   - a file in the overlay that turns the HUD overlay off (ShowHudOverlay = No: the user's directive,
#     as Tools/stage-overlay.sh enforces), checked again over the finished bundle.
# Last, an ad-hoc signature (codesign --sign - --timestamp=none) over the final contents, and
# codesign --verify --deep --strict of it.  Anything written into the bundle afterwards breaks that seal.
#
# THE ART is cloned (cp -c, clonefile) from the files the staged overlay links to, so on APFS the
# bundle's 1.6 GB costs almost no space; on another file system, or across volumes, cp -c fails and so
# does this script - it never falls back to a real copy (the disk rule).  --no-art leaves the archives
# out (a local bundle; the game then looks as ClassicGraphics does).
#
# Usage: make-macos-app.sh --generals <exe> --overlay <staged overlay> --build <build dir>
#          --out <.../Zero Hour Reforged.app> --bundle-id <id> [--no-art] [--link-ninja <build.ninja>]
#   --link-ninja  the build.ninja whose generals link line is checked (default <build>/build.ninja)
# Exit status: 0 built, signed and verified; 1 refused or failed (the partial bundle is removed).

set -u

GENERALS="" OVERLAY="" BUILD="" OUT="" BUNDLE_ID="" ART=1 NINJA=""
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--overlay) OVERLAY="$2"; shift 2;;
		--build) BUILD="$2"; shift 2;;
		--out) OUT="$2"; shift 2;;
		--bundle-id) BUNDLE_ID="$2"; shift 2;;
		--no-art) ART=0; shift;;
		--link-ninja) NINJA="$2"; shift 2;;
		*) echo "make-macos-app: unknown argument $1" >&2; exit 2;;
	esac
done
fail() { echo "make-macos-app: $*" >&2; [ -n "${STARTED:-}" ] && rm -rf -- "${OUT:?}"; exit 1; }
[ -x "$GENERALS" ] && [ -d "$OVERLAY" ] && [ -d "$BUILD" ] && [ -n "$BUNDLE_ID" ] || fail "--generals, --overlay, --build and --bundle-id are required"
case "$OUT" in *.app) ;; *) fail "--out must name a .app";; esac
[ -n "$NINJA" ] || NINJA="$BUILD/build.ninja"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$CODE/../.." && pwd)"
TABLE="$CODE/Tools/macos-app-licenses.txt"

# ---- the link line against the licence table ----------------------------------------------------------
linked="$(python3 - "$NINJA" <<'LINK_EOF'
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r"^build generals: CXX_EXECUTABLE_LINKER.*?\n  LINK_LIBRARIES = (.*?)\n", text, re.S | re.M)
if not m:
    sys.exit("no generals link line in " + sys.argv[1])
names = sorted(set(re.findall(r"(?:^|[\s/])lib([\w\-+]+)\.a(?=\s|$)", m.group(1))))
print("\n".join(names))
LINK_EOF
)" || fail "cannot read generals' link line from $NINJA"
[ -n "$linked" ] || fail "generals' link line in $NINJA names no static library"
unlicensed=""
for lib in $linked; do
	awk -v l="$lib" '$1 == l { found = 1 } END { exit !found }' "$TABLE" || unlicensed="$unlicensed $lib"
done
[ -z "$unlicensed" ] || fail "refused: generals links libraries macos-app-licenses.txt does not cover:$unlicensed"
ENTRIES="$( { for lib in $linked; do awk -v l="$lib" '$1 == l { print $2 }' "$TABLE"; done; awk '$1 == "+" { print $2 }' "$TABLE"; } | sort -u)"

# ---- the HUD directive, over what will be staged in --------------------------------------------------------
hud_off='^[[:space:]]*ShowHudOverlay[[:space:]]*=[[:space:]]*(no|false|0)([^[:alnum:]]|$)'
hud_check() {	# hud_check <dir>: fails naming the files that turn the HUD overlay off
	# find -L walks every file, symbolic links followed (the staged overlay links its art): a recursive
	# grep may not follow them, and which grep is first in PATH varies (ugrep's -r does not)
	local found
	found="$(find -L "$1" -type f -exec grep -a -i -l -E "$hud_off" -- {} + 2>/dev/null)"
	[ -z "$found" ] || fail "refused: the HUD overlay must stay on (ShowHudOverlay = No in: $(printf '%s ' $found))"
}
hud_check "$OVERLAY"

# ---- the bundle ---------------------------------------------------------------------------------------------
rm -rf -- "${OUT:?}"
STARTED=1
C="$OUT/Contents"
mkdir -p "$C/MacOS" "$C/Resources/Overlay" "$C/Resources/Licenses" || fail "cannot create $OUT"

# the executable, stripped, and its symbols beside the bundle
rm -rf -- "${OUT:?}.dSYM"
dsymutil "$GENERALS" -o "$OUT.dSYM" >/dev/null 2>&1 || fail "dsymutil failed"
cp "$GENERALS" "$C/MacOS/generals" && strip -S -x "$C/MacOS/generals" || fail "cannot place the executable"

# Info.plist, from the build's version header and the executable's own minimum macOS
version="$(awk '/#define VERSION_MAJOR/ {a=$3} /#define VERSION_MINOR/ {b=$3} /#define VERSION_BUILDNUM/ {c=$3} END {print a"."b"."c}' "$BUILD/generated/BuildVersion.h")"
minos="$(otool -l "$GENERALS" | awk '/LC_BUILD_VERSION/ {f=1} f && $1 == "minos" {print $2; exit}')"
[ -n "$minos" ] && [ "$version" != ".." ] || fail "cannot read the version ($version) or the minimum macOS ($minos)"
cat > "$C/Info.plist" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>		<string>en</string>
	<key>CFBundleDisplayName</key>			<string>Zero Hour Reforged</string>
	<key>CFBundleExecutable</key>			<string>generals</string>
	<key>CFBundleIconFile</key>			<string>AppIcon</string>
	<key>CFBundleIdentifier</key>			<string>$BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key>	<string>6.0</string>
	<key>CFBundleName</key>				<string>Zero Hour Reforged</string>
	<key>CFBundlePackageType</key>			<string>APPL</string>
	<key>CFBundleShortVersionString</key>		<string>$version</string>
	<key>CFBundleVersion</key>			<string>$version</string>
	<key>LSApplicationCategoryType</key>		<string>public.app-category.strategy-games</string>
	<key>LSMinimumSystemVersion</key>		<string>$minos</string>
	<key>NSHighResolutionCapable</key>		<true/>
</dict>
</plist>
PLIST_EOF
plutil -lint "$C/Info.plist" >/dev/null || fail "Info.plist does not lint"
printf 'APPL????' > "$C/PkgInfo"

# the icon: the .ico's 48 px image scaled to every size an .icns holds
iconset="$(mktemp -d "${TMPDIR:-/tmp}/zh-icon.XXXXXX")/AppIcon.iconset"
mkdir -p "$iconset"
sips -s format png "$CODE/Main/Generals.ico" --out "$iconset/base.png" >/dev/null 2>&1 || fail "cannot read Main/Generals.ico"
for s in 16 32 128 256 512; do
	sips -z $s $s "$iconset/base.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null 2>&1 &&
	sips -z $((s * 2)) $((s * 2)) "$iconset/base.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null 2>&1 || fail "sips failed at $s px"
done
rm -f "$iconset/base.png"
iconutil -c icns "$iconset" -o "$C/Resources/AppIcon.icns" || fail "iconutil failed"
rm -rf -- "$(dirname "$iconset")"

# the overlay: everything as staged; the art archives cloned from the files the staging links to
for entry in "$OVERLAY"/*; do
	name="$(basename "$entry")"
	case "$name" in
		*.big)
			[ "$ART" -eq 1 ] || continue
			cp -c "$entry" "$C/Resources/Overlay/$name" || fail "cannot clone $name (cp -c: is the build on APFS, on the art's volume?)"
			;;
		*) cp -R -L "$entry" "$C/Resources/Overlay/$name" || fail "cannot copy $name";;
	esac
done

# the licences
L="$C/Resources/Licenses"
license_entry() {	# license_entry <entry>: copies that entry's files into Licenses/
	local s="$CODE/Libraries/Source"
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
for e in $ENTRIES; do
	license_entry "$e" || fail "cannot write the licence entry '$e'"
done

# the directive once more, over the finished contents (the clones included)
hud_check "$C"

# ---- the signature, last, over the final contents ------------------------------------------------------------
codesign --force --sign - --timestamp=none "$OUT" 2>/dev/null || fail "codesign failed"
codesign --verify --deep --strict "$OUT" 2>/dev/null || fail "the fresh signature does not verify"
echo "make-macos-app: $OUT: version $version, minimum macOS $minos, $(printf '%s\n' $ENTRIES | wc -l | tr -d ' ') licence entries for $(printf '%s\n' $linked | wc -l | tr -d ' ') linked libraries, art $( [ "$ART" -eq 1 ] && echo cloned || echo left out); signed ad hoc and verified"
