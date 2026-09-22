#!/usr/bin/env bash
#
# Build C&C Generals Zero Hour on macOS, arm64. The POSIX half of build.bat, same arguments:
#
#   ./build.sh                    configure (if needed) + build Release
#   ./build.sh Debug              build another config (Release|RelWithDebInfo|Debug)
#   ./build.sh Release test       build, then run ctest
#   ./build.sh Release generals   build a single target
#   ./build.sh clean              delete the build tree and configure from scratch
#
# Nothing to edit before the first run: it finds cmake itself, fetches the third-party sources EA
# stripped and the fork's own art, and builds. What it cannot fetch is the game: a Zero Hour
# install's *.big go next to the executable in GeneralsMD/Run, and the base game's in
# Run/ZH_Generals.
#
# The build tree is build-mac/, beside Windows' build64/, so one checkout can be built by both -
# which is the normal case when the Mac work is being compared against Windows over a share or in
# a VM.
#
# The knobs below are overrides and every one of them empty is the supported path. A machine that
# does need one puts its own lines in build.local.sh beside this file, which is git-ignored and read
# right after these defaults, rather than editing a tracked file.

set -euo pipefail

CMAKE=
GENERATOR=
DEFAULT_CONFIG=Release
BUILD=

ROOT=$(cd "$(dirname "$0")" && pwd)

if [ -e "$ROOT/build.local.sh" ]; then
  echo "[build] reading build.local.sh"
  # shellcheck source=/dev/null
  . "$ROOT/build.local.sh"
fi

SRC="$ROOT/GeneralsMD/Code"
: "${BUILD:=$ROOT/build-mac}"

CONFIG="${1:-}"
ARG2="${2:-}"

if [ "$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')" = "clean" ]; then
  echo "[build] removing $BUILD"
  rm -rf "$BUILD"
  CONFIG="$ARG2"
  ARG2=
fi
[ -n "$CONFIG" ] || CONFIG="$DEFAULT_CONFIG"

RUNTESTS=
TARGET=
if [ "$(printf '%s' "$ARG2" | tr '[:upper:]' '[:lower:]')" = "test" ]; then
  RUNTESTS=1
elif [ -n "$ARG2" ]; then
  TARGET="$ARG2"
fi

fail() { echo "[build] ERROR: $1" >&2; exit 1; }

# --- the compiler. build.bat has Visual Studio to fall back on; here the toolchain is either
# installed or it is not, and cmake's own failure ("No CMAKE_CXX_COMPILER could be found") sends
# people looking in the wrong place. ---
if ! xcode-select -p >/dev/null 2>&1; then
  fail "the Xcode command line tools are not installed. Run: xcode-select --install"
fi

# --- locate cmake: the override, then PATH, then where the installers put it ---
if [ -n "$CMAKE" ] && [ ! -x "$CMAKE" ]; then
  fail "CMAKE is set to \"$CMAKE\" (build.local.sh, or the top of this file), but that file does not exist."
fi
if [ -z "$CMAKE" ]; then
  CMAKE=$(command -v cmake 2>/dev/null || true)
fi
if [ -z "$CMAKE" ]; then
  for try in /Applications/CMake.app/Contents/bin/cmake /opt/homebrew/bin/cmake /usr/local/bin/cmake; do
    if [ -z "$CMAKE" ] && [ -x "$try" ]; then CMAKE="$try"; fi
  done
fi
if [ -z "$CMAKE" ]; then
  fail "cmake not found on PATH or in the usual places.
[build]        Install it (brew install cmake, or the CMake.app disk image) and run this again."
fi

# --- Ninja if it is there, Unix Makefiles otherwise. Ninja is faster and is not a prerequisite. ---
if [ -z "$GENERATOR" ]; then
  if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; else GENERATOR="Unix Makefiles"; fi
fi

echo "[build] cmake:  $CMAKE"
echo "[build] config: $CONFIG"
echo "[build] tree:   $BUILD ($GENERATOR)"

# --- third-party sources the repository does not carry (zlib, LZH-Light, the GameSpy SDK) and the
# upscaled art. Fetches whatever is missing and is a no-op once it is there, so one build.sh on a
# fresh clone is enough. The DirectX 8 SDK is Windows-only and vendor.sh says so and skips it. ---
"$SRC/Tools/vendor.sh" || fail "fetching the third-party sources failed."

# --- configure ---
# Both generators here are single-config, so unlike build.bat the chosen configuration is baked in
# at configure time and --config is ignored at build time. That makes "the cache exists" the wrong
# question on its own: switching from Release to Debug in an existing tree has to configure again,
# or it silently rebuilds the configuration that is already there.
cached_config=
if [ -e "$BUILD/CMakeCache.txt" ]; then
  cached_config=$(sed -n 's/^CMAKE_BUILD_TYPE:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
fi
if [ ! -e "$BUILD/CMakeCache.txt" ] || [ "$cached_config" != "$CONFIG" ]; then
  echo "[build] configuring $SRC -> $BUILD"
  "$CMAKE" -S "$SRC" -B "$BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG" \
    || fail "configure failed."
fi

# --- build ---
if [ -n "$TARGET" ]; then
  echo "[build] building target $TARGET"
  "$CMAKE" --build "$BUILD" --target "$TARGET" || fail "build failed."
else
  "$CMAKE" --build "$BUILD" || fail "build failed."
fi

# --- tests ---
if [ -n "$RUNTESTS" ]; then
  # ctest lives beside cmake, and the copy on PATH need not be the same one when cmake came from
  # the override or from CMake.app.
  CTEST="$(dirname "$CMAKE")/ctest"
  [ -x "$CTEST" ] || CTEST=$(command -v ctest 2>/dev/null || true)
  [ -n "$CTEST" ] || fail "ctest not found beside $CMAKE or on PATH."
  echo "[build] running $CTEST"
  "$CTEST" --test-dir "$BUILD" --output-on-failure || fail "tests failed."
fi

# --- stage into GeneralsMD/Run ---
# Deliberately inert. On Windows CMakeLists.txt copies the exe and the FFmpeg DLLs into Run/ as a
# post-build step; on macOS there is nothing to stage until C2 produces an executable, and what
# gets staged is Generals.app rather than a pile of files beside the .big archives. The step is
# here rather than absent so that whoever lands M2 has the obvious place to put it.
# TODO(C2): stage the built app into GeneralsMD/Run once there is one.

echo "[build] done. Nothing runnable yet - M2 is the milestone that produces an executable."
