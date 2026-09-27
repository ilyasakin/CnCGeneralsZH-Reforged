#!/usr/bin/env bash
#
# linux-portable.sh: the portable Linux build (P3, docs/mac-port/tasks/P3-steam-deck.md) - one folder that runs
# on the Steam Deck's SteamOS and any desktop Linux of the last five years, with nothing installed.
#
#   <out>/zero-hour-reforged                      the launcher: runs bin/generals with the player's arguments
#   <out>/bin/generals                            stripped; bin/generals.debug beside it (a GNU debuglink)
#   <out>/share/zero-hour-reforged/overlay/       the staged overlay (zh_overlay), its art copied in
#   <out>/share/zero-hour-reforged/licenses/      from macos-app-licenses.txt, checked against the link line
#   <out>/share/applications/, share/icons/       a .desktop file and the icon (Main/Generals.ico's 48 px)
#   <out>/VERSION, <out>/README.txt               which build this is; how to install it and point it at Zero Hour
#   <out>.tar.zst                                 the folder, packed (not with --no-tar)
#
# THE BUILD runs inside Valve's Steam Runtime 3 "sniper" SDK container (Debian 11, glibc 2.31, g++-14, mold),
# as the user running this, with the repository, the build folder and CMake mounted at their own paths.
# libstdc++ and libgcc are linked statically, so the Deck's C++ runtime version does not matter.
#
# Refused, before the folder is written:
#   - generals needing a glibc symbol newer than GLIBC_2.31, or any libstdc++ symbol (GLIBCXX_, CXXABI_);
#   - generals needing a shared library outside the ones every SteamOS and desktop Linux has (glibc's own,
#     and fontconfig); everything else is static or loaded at run time by SDL and miniaudio;
#   - a static library on generals' link line that macos-app-licenses.txt does not name;
#   - a file in the folder that turns the HUD overlay off (the user's directive).
#
# Usage: linux-portable.sh --build <folder> --out <folder> --cmake <cmake>
#          [--image <sdk image>] [--jobs <n>] [--no-art] [--no-tar] [--no-build]
#   --build     the build folder (made if missing); kept between runs, so a second build is incremental
#   --out       the folder to make; its name is the package's (e.g. .../ZeroHourReforged-linux-x86_64)
#   --cmake     a Linux CMake of 3.29 or later that runs inside the container: the SDK's own 3.25 cannot
#               configure this project, and a distribution's CMake needs its distribution's glibc.
#               Kitware's release tarball (cmake-3.31.6-linux-x86_64) is what P3 uses
#   --image     default registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest
#   --jobs      the build's parallelism (default: ZHEAVY_JOBS, else the CPU count)
#   --no-art    leaves the 1.6 GB of Reforged*.big art out (the game then looks as ClassicGraphics does)
#   --no-build  stages from what <build> already holds
# Needs docker (the user in its group), python3, rsync, and GNU tar with zstd for the archive.
# Exit status: 0 made and checked; 1 refused or failed (the partial folder is removed).

set -u

BUILD="" OUT="" CMAKE="" IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest" JOBS="" ART=1 TAR=1 DOBUILD=1
while [ $# -gt 0 ]; do
	case "$1" in
		--build) BUILD="$2"; shift 2;;
		--out) OUT="$2"; shift 2;;
		--cmake) CMAKE="$2"; shift 2;;
		--image) IMAGE="$2"; shift 2;;
		--jobs) JOBS="$2"; shift 2;;
		--no-art) ART=0; shift;;
		--no-tar) TAR=0; shift;;
		--no-build) DOBUILD=0; shift;;
		*) echo "linux-portable: unknown argument $1" >&2; exit 2;;
	esac
done
fail() { echo "linux-portable: $*" >&2; [ -n "${STARTED:-}" ] && rm -rf -- "${OUT:?}"; exit 1; }
[ -n "$BUILD" ] && [ -n "$OUT" ] || fail "--build and --out are required"
[ "$DOBUILD" -eq 0 ] || [ -x "$CMAKE" ] || fail "--cmake must name a Linux CMake of 3.29 or later (Kitware's release tarball)"
command -v docker >/dev/null || fail "no docker"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$CODE/../.." && pwd)"
TABLE="$CODE/Tools/macos-app-licenses.txt"
mkdir -p "$BUILD" && BUILD="$(cd "$BUILD" && pwd)" || fail "cannot make $BUILD"
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT";; esac
[ -n "$JOBS" ] || JOBS="${ZHEAVY_JOBS:-$(nproc)}"
. "$CODE/Tools/package-common.sh"

# everything the container reads or writes, mounted at its own path
mounts=( -v "$REPO:$REPO" -v "$BUILD:$BUILD" )
[ -n "$CMAKE" ] && mounts+=( -v "$(cd "$(dirname "$CMAKE")/.." && pwd):$(cd "$(dirname "$CMAKE")/.." && pwd)" )
in_sdk() {	# in_sdk <command>: runs a shell command inside the SDK as this user
	docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp "${mounts[@]}" -w "$BUILD" "$IMAGE" bash -c "$1"
}

# ---- the build ---------------------------------------------------------------------------------------------
if [ "$DOBUILD" -eq 1 ]; then
	echo "linux-portable: building generals and the overlay in $IMAGE (-j$JOBS; the log: $BUILD/linux-portable.log)"
	in_sdk "'$CMAKE' -S '$CODE' -B '$BUILD' -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-14 \
		-DCMAKE_CXX_COMPILER=g++-14 '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc' && \
		ninja -C '$BUILD' -j$JOBS generals zh_overlay" > "$BUILD/linux-portable.log" 2>&1 \
		|| { grep -E "error|FAILED|undefined reference" "$BUILD/linux-portable.log" | head -20 >&2; fail "the build failed"; }
fi
GENERALS="$BUILD/generals"
[ -x "$GENERALS" ] && [ -d "$BUILD/overlay" ] || fail "$BUILD holds no generals or no staged overlay"

# ---- the licences, against the link line -----------------------------------------------------------------
LIBPATHS="$(mktemp "${TMPDIR:-/tmp}/zh-link-libs.XXXXXX")"
trap 'rm -f -- "$LIBPATHS"' EXIT
linked="$(package_link_libraries "$BUILD/build.ninja" "$LIBPATHS")" || fail "cannot read generals' link line"
[ -n "$linked" ] || fail "generals' link line names no static library"
ENTRIES="$(package_license_entries "$TABLE" $linked)" || fail "refused: generals links libraries macos-app-licenses.txt does not cover:$ENTRIES"

# ---- the HUD directive, over the overlay that will be copied in ------------------------------------------
found="$(hud_check_files "$BUILD/overlay")"
[ -z "$found" ] || fail "refused: the HUD overlay must stay on (ShowHudOverlay = No in: $(printf '%s ' $found))"

# ---- what generals needs of the system ---------------------------------------------------------------------
needs="$(in_sdk "readelf -d '$GENERALS' | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; echo '--'; objdump -T '$GENERALS'")" \
	|| fail "cannot read generals' dynamic section"
libs="$(printf '%s\n' "$needs" | sed '/^--$/q' | grep -v '^--$')"
glibc="$(printf '%s\n' "$needs" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
[ -n "$glibc" ] || fail "cannot read generals' glibc symbols"
[ "$(printf '%s\n%s\n' "$glibc" GLIBC_2.31 | sort -V | tail -1)" = GLIBC_2.31 ] || fail "refused: generals needs $glibc, newer than GLIBC_2.31"
! printf '%s\n' "$needs" | grep -qE 'GLIBCXX_|CXXABI_' || fail "refused: generals needs libstdc++ symbols (it must link it statically)"
allowed='^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|ld-linux-x86-64\.so\.2|libfontconfig\.so\.1)$'
extra="$(printf '%s\n' "$libs" | grep -vE "$allowed")"
[ -z "$extra" ] || fail "refused: generals needs shared libraries a SteamOS may lack: $(printf '%s ' $extra)"

# ---- the folder --------------------------------------------------------------------------------------------
rm -rf -- "$OUT" "$OUT.tar.zst"
STARTED=1
mkdir -p "$OUT" || fail "cannot create $OUT"
OUT="$(cd "$OUT" && pwd)"
mounts+=( -v "$(dirname "$OUT"):$(dirname "$OUT")" )
S="$OUT/share/zero-hour-reforged"
mkdir -p "$OUT/bin" "$S/overlay" "$S/licenses" "$OUT/share/applications" "$OUT/share/icons/hicolor/48x48/apps" \
	|| fail "cannot create $OUT"
in_sdk "objcopy --only-keep-debug '$GENERALS' '$OUT/bin/generals.debug' && strip -S -x -o '$OUT/bin/generals' '$GENERALS' && \
	cd '$OUT/bin' && objcopy --add-gnu-debuglink=generals.debug generals" || fail "cannot strip generals"
# the overlay, its links followed: the art is copied (a reflink where the file system has them)
if [ "$ART" -eq 1 ]; then
	cp -R -L --reflink=auto "$BUILD/overlay/." "$S/overlay/" || fail "cannot copy the overlay"
else
	rsync -aL --exclude 'Reforged*.big' "$BUILD/overlay/" "$S/overlay/" || fail "cannot copy the overlay"
fi
for e in $ENTRIES; do
	license_entry "$e" "$S/licenses" || fail "cannot write the licence entry '$e'"
done

cat > "$OUT/zero-hour-reforged" <<'LAUNCHER'
#!/bin/sh
# Zero Hour Reforged's launcher (P3): the game finds its overlay beside it and the player's Zero Hour by itself
# (Registry.ini, then ~/Games, the Steam libraries and the rest; see README.txt).  Arguments pass through,
# so Steam's launch options reach the game: -root "<your Zero Hour folder>" names the install.
here="$(dirname "$(readlink -f "$0")")"
exec "$here/bin/generals" "$@"
LAUNCHER
chmod +x "$OUT/zero-hour-reforged"

python3 - "$CODE/Main/Generals.ico" "$OUT/share/icons/hicolor/48x48/apps/zero-hour-reforged.png" <<'ICON_EOF' || fail "cannot make the icon"
# the .ico's largest 24-bit image, its AND mask as alpha, written as a PNG
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
count = struct.unpack("<H", d[4:6])[0]
best = None
for i in range(count):
    w, h, c, r, planes, bpp, size, off = struct.unpack("<BBBBHHII", d[6 + 16 * i:22 + 16 * i])
    w, h = w or 256, h or 256
    if d[off:off + 4] == b"\x89PNG":
        continue
    header = struct.unpack("<IiiHHIIiiII", d[off:off + 40])
    if header[4] == 24 and (best is None or w > best[0]):
        best = (w, h, off + header[0])
w, h, pixels = best
row = (w * 3 + 3) & ~3
mask_row = ((w + 31) // 32) * 4
mask = pixels + row * h
raw = b""
for y in range(h):
    src = pixels + row * (h - 1 - y)
    msrc = mask + mask_row * (h - 1 - y)
    line = bytearray([0])
    for x in range(w):
        b, g, r = d[src + 3 * x:src + 3 * x + 3]
        transparent = (d[msrc + x // 8] >> (7 - x % 8)) & 1
        line += bytes([r, g, b, 0 if transparent else 255])
    raw += bytes(line)
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
ICON_EOF

cat > "$OUT/share/applications/zero-hour-reforged.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Zero Hour Reforged
Comment=Command & Conquer Generals Zero Hour, reforged
Exec=zero-hour-reforged
Icon=zero-hour-reforged
# a template: to use it, put the folder's path in Exec and copy this file to ~/.local/share/applications
Categories=Game;StrategyGame;
Terminal=false
DESKTOP

commit="$(git -C "$CODE" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)"
if [ "$commit" != unknown ] && ! git -C "$CODE" diff --quiet HEAD -- . 2>/dev/null; then commit="$commit-dirty"; fi
built="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
image_id="$(docker image inspect --format '{{index .RepoDigests 0}}' "$IMAGE" 2>/dev/null || echo "$IMAGE")"
compiler="$(in_sdk "g++-14 --version | head -1")"
version="$(awk '/#define VERSION_MAJOR/ {a=$3} /#define VERSION_MINOR/ {b=$3} /#define VERSION_BUILDNUM/ {c=$3} END {print a"."b"."c}' "$BUILD/generated/BuildVersion.h")"
cat > "$OUT/VERSION" <<VERSION_EOF
Zero Hour Reforged $version for Linux (x86_64)
commit $commit
built $built
built in $image_id
compiler $compiler
needs glibc $glibc or newer, and fontconfig; art $( [ "$ART" -eq 1 ] && echo included || echo left out)
VERSION_EOF

cat > "$OUT/README.txt" <<'README_EOF'
Zero Hour Reforged for Linux and the Steam Deck
===============================================

This folder is the whole game engine; nothing is installed. It needs your own copy of Command & Conquer
Generals Zero Hour (Steam, EA App, CD or First Decade), with the original Generals it builds on.

INSTALL
  Unpack the folder anywhere you can write, e.g. ~/Games/ZeroHourReforged. To start it, run
  zero-hour-reforged in the folder.

  In Steam: Games > Add a Non-Steam Game to My Library > Browse, choose zero-hour-reforged.

WHERE YOUR ZERO HOUR IS
  The game finds it by itself when it is in one of these places:
    ~/Games/Command & Conquer Generals Zero Hour (or "Zero Hour", or the other usual names)
    any Steam library, the SD card's included, as Steam installs it
  with the original Generals in a ZH_Generals folder inside it, or in a folder named "Command & Conquer
  Generals" beside it.

  If it is elsewhere, the first start asks for the folder (in Desktop Mode) and remembers it. In the
  Steam Deck's Game Mode there is no folder dialog: give the folder in Steam's launch options instead.
  Select the game in Steam, then Properties > General > Launch Options, and enter:

    -root "~/Games/Command & Conquer Generals Zero Hour"

  with the path of your own Zero Hour folder (the one with INIZH.big in it) between the quotes.

FILES
  Settings, saves, replays and logs:  ~/.local/share/Command and Conquer Generals Zero Hour Data/
                                     ($XDG_DATA_HOME's, when that is set)
  Which build this is:               VERSION
  Licences:                          share/zero-hour-reforged/licenses/

UNINSTALL
  Delete this folder. Your settings and saves stay in ~/.local/share/Command and Conquer Generals Zero
  Hour Data until you delete them too.
README_EOF

# the directive once more, over the finished folder
found="$(hud_check_files "$OUT")"
[ -z "$found" ] || fail "refused: the HUD overlay must stay on (ShowHudOverlay = No in: $(printf '%s ' $found))"

size="$(du -sh "$OUT" | cut -f1)"
if [ "$TAR" -eq 1 ]; then
	tar -C "$(dirname "$OUT")" -I 'zstd -T0 -10' -cf "$OUT.tar.zst" "$(basename "$OUT")" || fail "cannot pack $OUT.tar.zst"
fi
STARTED=""
echo "linux-portable: $OUT ($size$( [ "$TAR" -eq 1 ] && echo ", packed $(du -sh "$OUT.tar.zst" | cut -f1)")): commit $commit, needs $glibc, $(printf '%s\n' $ENTRIES | wc -l) licence entries for $(printf '%s\n' $linked | wc -l) linked libraries, art $( [ "$ART" -eq 1 ] && echo included || echo left out)"
