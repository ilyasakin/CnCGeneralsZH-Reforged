#!/usr/bin/env bash
#
# Fetches the third-party sources the build needs and this repository does not carry.
#
# This is the POSIX half of Tools/vendor.ps1 and deliberately a second implementation rather than
# a shared one: a Python dependency costs more here than the duplication does. Behaviour, paths and
# output are meant to match vendor.ps1 line for line, so read that one too before changing this one.
#
# EA stripped these from the source release and they are not ours to commit, so every fresh clone
# has to get them once. build.sh runs this before it configures; running it again when everything
# is in place costs one directory check per library and nothing else.
#
#   --force   re-fetch even what is already there
#
# What it cannot get is the game itself: the .big files from a Zero Hour install go next to the
# executable in GeneralsMD/Run, and the base game's in Run/ZH_Generals. The game says so on startup
# when they are missing.

set -euo pipefail

force=

while [ $# -gt 0 ]; do
  case "$1" in
    --force|-f) force=1 ;;
    -h|--help) sed -n '3,$p' "$0" | sed -n '/^[^#]/q; s/^#\{1\} \{0,1\}//p'; exit 0 ;;
    *) echo "[vendor] ERROR: unknown argument '$1' (try --force)" >&2; exit 1 ;;
  esac
  shift
done

tools_dir=$(cd "$(dirname "$0")" && pwd)
code_root=$(dirname "$tools_dir")
libraries="$code_root/Libraries"
run_folder="$(dirname "$code_root")/Run"
work="${TMPDIR:-/tmp}/zhr-vendor"
work="${work%/}"

# Progress goes to stderr, not stdout. vendor.ps1 can call Write-Host freely because PowerShell
# keeps the host and the pipeline apart; here a function that both logs and returns a value would
# hand its caller the log line as part of the value, and the first run of this copied an archive
# named "[vendor] downloading lzhl.zip" before the streams were separated.
step() { echo "[vendor] $1" >&2; }

# --- the same three helpers vendor.ps1 has, in the same order ------------------------------------

# Both of these run inside $( ), where bash does not carry set -e, so a failed curl or unzip used to
# be ignored: the function printed its result anyway, the caller copied an empty folder over the
# library and the run ended with "everything the build needs is in place" (measured with a bogus
# commit, 2026-09-25). They return failure explicitly now, which the caller's assignment turns into
# an exit under set -e - the way vendor.ps1's $ErrorActionPreference = 'Stop' already behaved.
# The download goes to a .part file and is renamed only once complete, because an existing file is
# taken as already downloaded, and an interrupted transfer would otherwise be reused on every run.
get_file() { # url destination -> prints destination
  local url="$1" destination="$2"
  if [ -e "$destination" ]; then printf '%s\n' "$destination"; return 0; fi
  mkdir -p "$(dirname "$destination")"
  step "downloading $(basename "$destination")"
  if ! curl -fsSL "$url" -o "$destination.part"; then
    rm -f "$destination.part"
    echo "[vendor] ERROR: could not download $url" >&2
    return 1
  fi
  mv -f "$destination.part" "$destination"
  printf '%s\n' "$destination"
}

# Unpacks into a folder of its own and hands back whatever single directory the archive contained,
# which for a GitHub source zip is the repository at that commit.
expand_source() { # archive name -> prints the unpacked root
  local archive="$1" name="$2" target="$work/$2"
  rm -rf "$target"
  mkdir -p "$target"
  step "unpacking $name"
  case "$archive" in
    *.tar.gz) tar -xf "$archive" -C "$target" || { echo "[vendor] ERROR: could not unpack $archive" >&2; return 1; } ;;
    *)        unzip -qo "$archive" -d "$target" || { echo "[vendor] ERROR: could not unpack $archive" >&2; return 1; } ;;
  esac
  local entry count=0 only=
  for entry in "$target"/*; do
    [ -e "$entry" ] || continue
    count=$((count + 1))
    only="$entry"
  done
  if [ "$count" -eq 1 ] && [ -d "$only" ]; then printf '%s\n' "$only"; else printf '%s\n' "$target"; fi
}

copy_files() { # destination file... 
  local destination="$1"; shift
  mkdir -p "$destination"
  local file
  for file in "$@"; do cp -f "$file" "$destination/$(basename "$file")"; done
}

# The files directly in a folder with one of these extensions, minus the named exceptions. Both
# lists are matched case-insensitively, the way PowerShell's -contains and -in are, because the
# DirectX drop spells its headers in a different case from the script that asks for them.
#
#   list_top_level <folder> <extensions, comma separated> [<excluded names, comma separated>]
list_top_level() {
  local folder="$1" extensions=",$(lower "$2")," excluded=",$(lower "${3:-}"),"
  local file base
  for file in "$folder"/*; do
    [ -f "$file" ] || continue
    base=$(lower "$(basename "$file")")
    case "$base" in *.*) ;; *) continue ;; esac
    case "$extensions" in *",.${base##*.},"*) ;; *) continue ;; esac
    case "$excluded" in *",$base,"*) continue ;; esac
    printf '%s\n' "$file"
  done
}

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

sha256_of() { shasum -a 256 "$1" | awk '{print $1}'; }

# --- zlib 1.1.4, flat. maketree.c is a generator with its own main() and does not belong in the lib.
install_zlib() {
  local destination="$libraries/Source/Compression/ZLib"
  if [ -e "$destination/deflate.c" ] && [ -z "$force" ]; then
    # Still checked on a second run, and on a tree vendor.ps1 filled: zlib that arrived from the
    # PowerShell script is unpatched, and a shared checkout is exactly where that happens.
    patch_zlib_for_apple "$destination"
    patch_zlib_zutil_for_apple "$destination"
    return 0
  fi
  local archive source
  archive=$(get_file 'https://zlib.net/fossils/zlib-1.1.4.tar.gz' "$work/zlib-1.1.4.tar.gz")
  source=$(expand_source "$archive" 'zlib')
  local IFS=$'\n'
  copy_files "$destination" $(list_top_level "$source" '.c,.h' 'maketree.c')
  unset IFS
  patch_zlib_for_apple "$destination"
  patch_zlib_zutil_for_apple "$destination"
  step "zlib 1.1.4 -> Libraries/Source/Compression/ZLib"
}

# zlib 1.1.4 guards its own Byte typedef:
#
#     #if !defined(MACOS) && !defined(TARGET_OS_MAC)
#     typedef unsigned char  Byte;  /* 8 bits */
#     #endif
#
# In 2002 that deferred to Classic Mac OS's MacTypes.h, which defined Byte itself. On a modern Mac
# TARGET_OS_MAC still arrives transitively through Apple's system headers, so the typedef is
# skipped - and MacTypes.h is not what zlib.h ends up including, so nothing supplies it. Every
# translation unit then dies at zconf.h:223, "unknown type name 'Byte'".
#
# No compile definition fixes this: the guard tests defined(), so defining either name skips the
# typedef harder. The file has to change. zlib upstream reached the same conclusion and deleted the
# guard outright in 1.2.0, which is exactly what this does.
#
# Windows is unaffected either way - neither name is ever defined there, so the typedef happens
# before and after. It does mean vendor.ps1 and vendor.sh now leave different bytes in zconf.h;
# that is in WINDOWS-DEBT.md.
patch_zlib_for_apple() {
  local zconf="$1/zconf.h"
  [ -e "$zconf" ] || return 0
  grep -q 'TARGET_OS_MAC' "$zconf" || return 0   # already patched, or a zlib that dropped it

  awk '
    /^#if !defined\(MACOS\) && !defined\(TARGET_OS_MAC\)$/ { dropping = 1; next }
    dropping && /^#endif$/                                    { dropping = 0; next }
    { print }
  ' "$zconf" > "$zconf.patched"

  # Loudly if it did not take. A silently unpatched zconf.h is a wall of "unknown type name 'Byte'"
  # with nothing pointing back at this script.
  if grep -q 'TARGET_OS_MAC' "$zconf.patched" || ! grep -q '^typedef unsigned char  Byte;' "$zconf.patched"; then
    rm -f "$zconf.patched"
    echo "[vendor] ERROR: could not unguard the Byte typedef in $zconf - the guard has moved" >&2
    exit 1
  fi
  mv -f "$zconf.patched" "$zconf"
  step 'unguarded zlib Byte typedef for Apple (see the comment in this script)'
}

# zutil.h:113 is the same predicate a second time, and it was missed the first time round for a
# reason worth keeping: the compiler stopped at zconf.h, so this one never got a chance to fail.
# Fixing the error the compiler reports is not the same as fixing the file.
#
#     #if defined(MACOS) || defined(TARGET_OS_MAC)
#     #  define OS_CODE  0x07
#     #  ...
#     #    ifndef fdopen
#     #      define fdopen(fd,mode) NULL      /* No fdopen() */
#
# TARGET_OS_MAC arrives transitively here too, so on macOS this branch is live: OS_CODE becomes
# 0x07 and fdopen becomes a macro expanding to NULL - `#ifndef fdopen` is true because fdopen is a
# function, not a macro.  It was written for Classic Mac OS, where there genuinely was no fdopen
# and MWERKS was the compiler; on Darwin, which is Unix, both statements are simply false.
#
# Nothing calls it today: OS_CODE is read only at gzio.c:165 and fdopen only at gzio.c:156, both
# inside the gzip wrapper, and the game calls z_compress2/z_uncompress, which are zlib format.
# gzio.c is compiled and never called.  So this is a landmine rather than a fire - and that is the
# argument for spending six lines on it, not against.  The first caller of gzopen on a Mac would
# get a FILE* built from NULL.
#
# Narrowed rather than deleted, because the Classic Mac branch is still correct for Classic Mac:
# __APPLE__ is defined on Darwin and was not on Mac OS 9 or MWERKS.  Skipping the branch lets
# zutil.h fall through to its own `#ifndef OS_CODE / #define OS_CODE 0x03 /* assume Unix */`,
# which is what Darwin is, and leaves fdopen as the real function.
#
# Windows is untouched: neither MACOS nor TARGET_OS_MAC is ever defined there, so the branch was
# already dead and the condition it is now guarded by is never evaluated.
patch_zlib_zutil_for_apple() {
  local zutil="$1/zutil.h"
  [ -e "$zutil" ] || return 0

  # Matched on meaning rather than on spacing: any #if that tests TARGET_OS_MAC and does not
  # already exclude __APPLE__.  An exact-text match would silently do nothing if the upstream line
  # were ever respelled, which is the same quiet failure this whole patch exists to avoid.
  grep -nE '^[[:space:]]*#[[:space:]]*if.*TARGET_OS_MAC' "$zutil" | grep -qv '__APPLE__' || return 0

  awk '
    /^[[:space:]]*#[[:space:]]*if/ && /TARGET_OS_MAC/ && !/__APPLE__/ {
      sub(/^[[:space:]]*#[[:space:]]*if[[:space:]]*/, "")
      print "#if (" $0 ") && !defined(__APPLE__)"
      next
    }
    { print }
  ' "$zutil" > "$zutil.patched"

  if grep -nE '^[[:space:]]*#[[:space:]]*if.*TARGET_OS_MAC' "$zutil.patched" | grep -qv '__APPLE__'; then
    rm -f "$zutil.patched"
    echo "[vendor] ERROR: could not narrow the Classic Mac branch in $zutil - it has moved" >&2
    exit 1
  fi
  mv -f "$zutil.patched" "$zutil"
  step 'narrowed zlib Classic Mac branch to Classic Mac (see the comment in this script)'
}

# --- LZH-Light 1.0. Lzhl_tcp.cpp is a socket layer nothing calls; Test.c has its own main().
install_lzhl() {
  local header="$libraries/Source/Compression/LZHCompress/CompLibHeader"
  local source_folder="$libraries/Source/Compression/LZHCompress/CompLibSource"
  if [ -e "$source_folder/Lzhl.cpp" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/TheSuperHackers/lzhl-1.0/archive/dfd96e2.zip' "$work/lzhl.zip")
  source=$(expand_source "$archive" 'lzhl')
  local IFS=$'\n'
  copy_files "$header" $(list_top_level "$source" '.h')
  copy_files "$source_folder" $(list_top_level "$source" '.cpp,.tbl' 'Lzhl_tcp.cpp,Test.c')
  step "LZH-Light 1.0 -> Libraries/Source/Compression/LZHCompress"
}

# --- The DirectX 8 headers and import libraries are Windows-only and vendor.ps1 keeps them. Nothing
# that compiles on a Mac includes d3d8.h, and the .lib files are MSVC import libraries that no
# toolchain here can link, so fetching them would cost 40 MB to satisfy nobody.
#
# CMakeLists.txt's vendored-sources guard still lists Libraries/DirectX/Include/d3d8.h, so a
# configure on a Mac fails on it until A1 puts that entry behind the same platform guard. That is
# A1's line to move, not this script's.
report_directx() {
  step 'skipping the DirectX 8 SDK: Windows only, and vendor.ps1 is what fetches it'
}

# --- GameSpy SDK, whole repository. It brings its own CMakeLists, which CMakeLists.txt adds.
install_gamespy() {
  local destination="$libraries/Source/GameSpy"
  if [ -e "$destination/CMakeLists.txt" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/TheSuperHackers/GamespySDK/archive/b1b77d8.zip' "$work/gamespy.zip")
  source=$(expand_source "$archive" 'gamespy')
  # Every one of these folders holds a committed .gitignore that keeps the code out of the
  # repository. Emptying the folder first takes that with it, and then the whole SDK shows up as
  # untracked - which is how 780 files of third-party source nearly went into a commit.
  # Moved aside and moved back rather than read and rewritten, so it returns byte for byte. Read
  # into a variable it comes back a trailing newline short, and then the file the whole dance
  # exists to protect shows up as modified in every diff.
  local keep="$destination/.gitignore" kept="$work/gamespy.gitignore"
  rm -f "$kept"
  if [ -e "$keep" ]; then mv "$keep" "$kept"; fi
  rm -rf "$destination"
  mkdir -p "$destination"
  # The trailing /. copies the contents rather than the directory, dot files included.
  cp -Rf "$source/." "$destination/"
  if [ -e "$kept" ]; then mv -f "$kept" "$keep"; fi
  step "GamespySDK -> Libraries/Source/GameSpy"
}

# --- FFmpeg. Not fetched by either script: Libraries/Source/FFmpeg/dist is committed, and it is a
# Windows distribution - .lib import libraries and avcodec-62.dll and friends. A Mac build needs a
# different FFmpeg entirely, and whether that is Homebrew, a vendored dylib or a static build is
# still open.
#
# TODO(C4/D-track): decide where macOS gets FFmpeg from and add the step here. Guessing now would
# pin a choice that C4 has not made, and M1 is a headless build that links neither binkvideo nor
# milesaudio, so nothing before M2 needs it.

# --- The fork's own upscaled art: every 3D texture at twice its size, the normal maps the models
# are lit through, and the ground. Not in git - ReforgedTextures.big alone is a gigabyte, ten times
# what GitHub takes in a file, and LFS in a fork is billed to the parent repository.
#
# It comes from the release channel, the same place a player's launcher takes it from, and the
# channel's own _versions.json carries the sha256 of every file in the newest release. Reading the
# hash from there rather than pinning it here means regenerating the art does not leave this script
# lying.
#
# Two places it can come from, in this order:
#
#   1. the release channel, if this checkout knows one. This repository is public and does not name
#      it: the address comes from ZHR_CHANNEL_URL, or from the launcher checkout beside this one.
#   2. this repository's own art release on GitHub, which is where anyone who just cloned the
#      public repository gets it. art.json there lists each file with its sha256, so the hashes are
#      not pinned in this script and regenerating the art does not leave it lying.
#
# With neither, the step is skipped: the game plays at the textures it shipped with, and
# experiments/doku-upscale is where the art is made.
art_pattern='Reforged.*\.big$'
art_release='https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/download/art-latest'

# --- litehtml 0.10, the whole repository: the HTML and CSS layout engine behind the pages upstream
# draws over the battlefield. gameengine links it, so unlike DirectX it is needed on macOS too.
# Same .gitignore dance as GameSpy, and for the same reason.
install_litehtml() {
  local destination="$libraries/Source/litehtml"
  if [ -e "$destination/CMakeLists.txt" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/litehtml/litehtml/archive/9bc84b8b8d15a4e50f18b327aa30955048b441c2.zip' "$work/litehtml-0.10.zip")
  source=$(expand_source "$archive" 'litehtml')
  local keep="$destination/.gitignore" kept="$work/litehtml.gitignore"
  rm -f "$kept"
  if [ -e "$keep" ]; then mv "$keep" "$kept"; fi
  rm -rf "$destination"
  mkdir -p "$destination"
  cp -Rf "$source/." "$destination/"
  if [ -e "$kept" ]; then mv -f "$kept" "$keep"; fi
  step "litehtml 0.10 -> Libraries/Source/litehtml"
}

# --- The fork's one change to litehtml, Libraries/Source/litehtml-parsed-css.patch. HtmlOverlay.cpp
# does not compile without it. A patched copy says so by the parameter name `master_parsed` in
# document.h, so a copy fetched before the patch existed gets it on the next run too.
#
# GIT_CEILING_DIRECTORIES is not optional. litehtml sits inside this repository's checkout, and
# without the ceiling git finds the outer repository, treats every file in the patch as outside it,
# skips them all - and says nothing. vendor.ps1 records exactly that.
#
# And the result is checked by the marker, not by git apply's exit status. The failure above is the
# silent kind; a status check would pass on the one case it exists to catch.
install_litehtml_patch() {
  local destination="$libraries/Source/litehtml"
  local header="$destination/include/litehtml/document.h"
  if grep -q 'master_parsed' "$header" 2>/dev/null; then return 0; fi
  local patch="$libraries/Source/litehtml-parsed-css.patch"
  GIT_CEILING_DIRECTORIES="$libraries/Source" \
    git -C "$destination" -c core.autocrlf=false apply "$patch" || true
  if ! grep -q 'master_parsed' "$header" 2>/dev/null; then
    echo "[vendor] litehtml-parsed-css.patch did not apply to Libraries/Source/litehtml" >&2
    echo "[vendor] ($header still lacks master_parsed)" >&2
    exit 1
  fi
  step "litehtml-parsed-css.patch -> Libraries/Source/litehtml"
}

# --- nanosvg, the two headers: parses and rasterises the SVG pictures a page names in url(). Copied
# file by file rather than by replacing the folder, so its committed .gitignore is never disturbed.
install_nanosvg() {
  local destination="$libraries/Source/nanosvg"
  if [ -e "$destination/nanosvgrast.h" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/memononen/nanosvg/archive/239e102ec2c691f2902e20ace2ed36ee4a35cfe6.zip' "$work/nanosvg.zip")
  source=$(expand_source "$archive" 'nanosvg')
  local headers=()
  while IFS= read -r f; do headers+=("$f"); done < <(list_top_level "$source/src" '.h')
  copy_files "$destination" "${headers[@]}" "$source/LICENSE.txt"
  step "nanosvg -> Libraries/Source/nanosvg"
}

# --- SDL3 3.4.16, the whole repository: the window, the events, the entry point and the GPU API on
# every platform that is not Windows (decision 3 in docs/mac-port/README.md). Windows keeps
# Win32Device, so vendor.ps1 does not fetch it - it says so, the way report_directx does here. Same
# .gitignore dance as litehtml. Pinned to the release's commit, not its tag, because a tag can move.
install_sdl3() {
  local destination="$libraries/Source/SDL3"
  if [ -e "$destination/CMakeLists.txt" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/libsdl-org/SDL/archive/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c.zip' "$work/SDL3-3.4.16.zip")
  source=$(expand_source "$archive" 'SDL3')
  local keep="$destination/.gitignore" kept="$work/SDL3.gitignore"
  rm -f "$kept"
  if [ -e "$keep" ]; then mv "$keep" "$kept"; fi
  rm -rf "$destination"
  mkdir -p "$destination"
  cp -Rf "$source/." "$destination/"
  if [ -e "$kept" ]; then mv -f "$kept" "$keep"; fi
  if [ ! -e "$destination/include/SDL3/SDL_gpu.h" ]; then
    echo "[vendor] SDL3 unpacked without include/SDL3/SDL_gpu.h - not the tree this build expects" >&2
    exit 1
  fi
  step "SDL3 3.4.16 -> Libraries/Source/SDL3"
}

# --- miniaudio 0.11.25, the one header and its one implementation file: audio beneath C4's port of
# MilesAudioManager (decision 3). POSIX only, like SDL3. Copied file by file, like nanosvg, so its
# committed .gitignore is never disturbed; upstream's CMakeLists builds extras this does not want.
install_miniaudio() {
  local destination="$libraries/Source/miniaudio"
  if [ -e "$destination/miniaudio.c" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/mackron/miniaudio/archive/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d.zip' "$work/miniaudio-0.11.25.zip")
  source=$(expand_source "$archive" 'miniaudio')
  if [ ! -e "$source/miniaudio.h" ] || [ ! -e "$source/miniaudio.c" ]; then
    echo "[vendor] miniaudio 0.11.25 unpacked without miniaudio.h and miniaudio.c" >&2
    exit 1
  fi
  copy_files "$destination" "$source/miniaudio.h" "$source/miniaudio.c" "$source/LICENSE"
  step "miniaudio 0.11.25 -> Libraries/Source/miniaudio"
}

get_channel_url() {
  if [ -n "${ZHR_CHANNEL_URL:-}" ]; then printf '%s/\n' "${ZHR_CHANNEL_URL%/}"; return 0; fi
  local launcher
  launcher="$(dirname "$(dirname "$(dirname "$code_root")")")/launcher/update.js"
  if [ -e "$launcher" ]; then
    local url
    url=$(sed -n "s/.*CHANNEL_URL[[:space:]]*=[[:space:]]*'\([^']*\)'.*/\1/p" "$launcher" | head -n 1)
    if [ -n "$url" ]; then printf '%s/\n' "${url%/}"; return 0; fi
  fi
  return 0
}

# Each source hands back the same shape: one line per file, name, url, size and sha256, tab
# separated. The two manifests are JSON and sh does not read JSON, so python3 does that part - the
# one from the command line tools, not a Homebrew one. Without it the art step is skipped and says
# so, which is the same outcome as an unreachable channel and equally survivable.
have_python() { command -v python3 >/dev/null 2>&1; }

get_art_from_channel() {
  local channel_url
  channel_url=$(get_channel_url)
  [ -n "$channel_url" ] || return 0
  local versions
  if ! versions=$(curl -fsSL "${channel_url}_versions.json" 2>/dev/null); then
    step 'the release channel is not reachable'
    return 0
  fi
  printf '%s' "$versions" | python3 -c '
import json, re, sys
channel, pattern = sys.argv[1], re.compile(sys.argv[2])
release = json.load(sys.stdin)["game"][0]
for f in release["files"]:
    if pattern.search(f["path"]):
        path = f["path"].replace("\\", "/")
        name = path.rsplit("/", 1)[-1]
        print("\t".join([name, channel + release["folder"] + "/" + path,
                         str(f["size"]), f["sha256"]]))
' "$channel_url" "$art_pattern"
}

get_art_from_release() {
  local manifest
  if ! manifest=$(curl -fsSL "$art_release/art.json" 2>/dev/null); then return 0; fi
  printf '%s' "$manifest" | python3 -c '
import json, sys
base = sys.argv[1]
for f in json.load(sys.stdin)["files"]:
    print("\t".join([f["name"], base + "/" + f["name"], str(f["size"]), f["sha256"]]))
' "$art_release"
}

install_art() {
  if ! have_python; then
    step 'no python3, so the art manifest cannot be read - skipping the upscaled art'
    return 0
  fi
  local wanted
  wanted=$(get_art_from_channel)
  if [ -z "$wanted" ]; then wanted=$(get_art_from_release); fi
  if [ -z "$wanted" ]; then
    step 'no upscaled art is published yet, so the game will use the textures it shipped with'
    return 0
  fi

  mkdir -p "$run_folder"
  local name url size sha target partial hash
  while IFS=$'\t' read -r name url size sha; do
    [ -n "$name" ] || continue
    target="$run_folder/$name"
    if [ -e "$target" ] && [ -z "$force" ] && [ "$(sha256_of "$target")" = "$(lower "$sha")" ]; then
      continue
    fi
    partial="$target.part"
    step "downloading $name ($(( (size + 524288) / 1048576 )) MB)"
    curl -fsSL "$url" -o "$partial"
    hash=$(sha256_of "$partial")
    # Loudly, and without leaving the half-file behind: art that is quietly wrong is a game that
    # looks subtly wrong an hour later, with nothing to point at.
    if [ "$hash" != "$(lower "$sha")" ]; then
      rm -f "$partial"
      echo "[vendor] ERROR: $name downloaded with hash $hash, and it was published as $sha" >&2
      exit 1
    fi
    mv -f "$partial" "$target"
    step "$name -> Run"
  done <<EOF
$wanted
EOF
}

mkdir -p "$work"
install_zlib
install_lzhl
report_directx
install_gamespy
install_litehtml
install_litehtml_patch
install_nanosvg
install_sdl3
install_miniaudio
install_art
step 'everything the build needs is in place'
