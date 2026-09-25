#!/usr/bin/env bash
#
# Builds and tests the tree on Linux, in containers, from a Mac: {gcc, clang} x {arm64, amd64}.
# Configure, build and ctest for each, then one table, then an exit status that is nonzero if any
# of the four did not pass.
#
# =============================================================================================
# WHAT THIS PROVES, AND WHAT IT DOES NOT
#
# It proves: the tree configures, builds and passes ctest under glibc and libstdc++, with GCC and
# with Clang, on both Linux architectures. That is where the non-Windows code stops leaning on
# whatever Apple's headers happened to include, and where GCC's stricter reading of the standard
# gets a say.
#
# It does NOT prove:
#   - Anything about Windows. No MSVC, no Windows SDK.
#   - Anything about a Linux the game runs on. Ubuntu 24.04's toolchain and glibc only. The
#     container has no display, no GPU and no sound, and nothing here launches the game.
#   - Anything the host filesystem hides.  The bind mount of the Mac's volume is case-insensitive
#     even inside the container, so each row builds from a copy on the container's own
#     case-sensitive filesystem, and checks that a wrongly-cased name really fails to resolve
#     before it trusts that copy.  Without the copy, this check could not see include-case bugs.
#   - A real amd64 machine. The amd64 rows run under Rosetta inside OrbStack's VM. That is the
#     same translation layer E3 relies on, and it has the same blind spots.
#   - Anything ctest does not cover. Targets kept out of the default build (EXCLUDE_FROM_ALL,
#     ZH_NOT_YET_PORTABLE) are not built at all, and a DISABLED test is not a passing one.
#   - Parity with macOS by itself. The table shows each row's disabled and skipped tests so the
#     comparison can be made. arch_differential skips on Linux by design (it needs Darwin's
#     -arch and Rosetta), and d3dx_oracle skips unless ZH_D3DX9_X64 is set, which it never is
#     here.
#
# It fails, rather than passes, when it cannot run: no docker, no daemon, or vendored sources
# missing. A check that cannot run and says "ok" is the failure this project has hit five times.
# =============================================================================================
#
#   linux-check.sh                     all four rows
#   linux-check.sh arm64/gcc ...       only the named rows (arm64|amd64 / gcc|clang)
#
# Logs go to $ZH_LINUX_CHECK_WORK (default ${TMPDIR:-/tmp}/zhr-linux-check), one per row.
# Run Tools/vendor.sh on the host first: the repository is mounted read-only and the vendored
# sources live inside it.

set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
work="${ZH_LINUX_CHECK_WORK:-${TMPDIR:-/tmp}/zhr-linux-check}"
mkdir -p "$work"

say()  { echo "[linux-check] $1"; }
fail() { echo "[linux-check] ERROR: $1"; exit 2; }

# --- can this machine do the job at all? Not being able to is a failure, not a skip. ----------
command -v docker >/dev/null 2>&1 || fail "docker is not on PATH. This check cannot run, so it has not passed."
docker info >/dev/null 2>&1 || fail "docker is installed but its daemon does not answer (is OrbStack running?)."

# The vendored sources are fetched, not tracked, and an empty directory builds nothing and says so
# cheerfully. Check the ones configure needs; a missing one is the host's job to fix.
for d in GeneralsMD/Code/Libraries/Source/Compression/ZLib GeneralsMD/Code/Libraries/Source/GameSpy; do
  [ -n "$(ls -A "$root/$d" 2>/dev/null | grep -v '^\.gitignore$')" ] \
    || fail "$d is empty. Run GeneralsMD/Code/Tools/vendor.sh on the host first."
done

# The file list for the containers' copy, computed here, where git works.  Inside the container it
# does not: a worktree's .git is a file naming the main repository's .git/worktrees/<name> by a
# host path the container cannot see.  Tracked files by git's names, minus any deleted in the
# working tree, then untracked files (the vendored sources) by the disk's.
filelist="$work/files.list"
git -C "$root" ls-files -z --cached -- GeneralsMD/Code > "$work/cached.list" \
  && git -C "$root" ls-files -z --deleted -- GeneralsMD/Code > "$work/deleted.list" \
  && git -C "$root" ls-files -z --others -- GeneralsMD/Code > "$work/others.list" \
  || fail "git could not list the tree at $root"
python3 - "$work" <<'PY' || fail "could not build the file list"
import sys, os
w = sys.argv[1]
read = lambda n: [p for p in open(os.path.join(w, n), 'rb').read().split(b'\0') if p]
deleted = set(read('deleted.list'))
paths = [p for p in read('cached.list') if p not in deleted] + read('others.list')
open(os.path.join(w, 'files.list'), 'wb').write(b'\0'.join(paths) + b'\0')
print(f"[linux-check] copying {len(paths)} files into each container ({len(deleted)} deleted, skipped)")
PY
[ "$(tr -cd '\0' < "$filelist" | wc -c)" -gt 1000 ] || fail "the file list is nearly empty; refusing to test nothing"

rows=("$@")
[ ${#rows[@]} -gt 0 ] || rows=(arm64/gcc arm64/clang amd64/gcc amd64/clang)

# --- one image per architecture, built on first use --------------------------------------------
# The X11, Wayland, EGL, DRM and Vulkan headers are SDL3's: without them its configure stops with
# "could not find X11 or Wayland development libraries".  spirv-tools is for test_shader_sdl, which
# validates every generated program's SPIR-V with spirv-val and says SKIPPED where there is none.  The image tag carries a hash of this text,
# so changing the list builds a new image rather than silently reusing an old one.
DOCKERFILE='FROM ubuntu:24.04
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      cmake ninja-build g++ clang python3 git pkg-config \
      libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev \
      libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols libegl-dev libdrm-dev \
      libgbm-dev libvulkan-dev spirv-tools >/dev/null && rm -rf /var/lib/apt/lists/*'
IMAGE_HASH=$(printf '%s' "$DOCKERFILE" | shasum | cut -c1-12)

ensure_image() { # arch
  local image="zhr-linux-check:ubuntu24.04-$1-$IMAGE_HASH"
  if ! docker image inspect "$image" >/dev/null 2>&1; then
    say "building $image (first run with this package list)" >&2
    printf '%s\n' "$DOCKERFILE" | docker build -q --platform "linux/$1" -t "$image" - >/dev/null \
      || fail "could not build $image"
  fi
  echo "$image"
}

# --- one row: configure, build, ctest inside a throwaway container ----------------------------
# The container prints @@ marker lines; everything else is the log.
run_row() { # arch compiler logfile
  local arch="$1" compiler="$2" log="$3" image cc cxx
  image=$(ensure_image "$arch") || exit 2
  case "$compiler" in
    gcc)   cc=gcc   cxx=g++ ;;
    clang) cc=clang cxx=clang++ ;;
    *) fail "unknown compiler $compiler" ;;
  esac
  # -i: without it docker does not forward stdin, bash reads an empty script, and the row does
  # nothing at all.  The first version of this script had exactly that bug; @@ran below catches it.
  docker run -i --rm --platform "linux/$arch" -v "$root":/src:ro -v "$work":/lists:ro \
    -e CC="$cc" -e CXX="$cxx" "$image" \
    bash -s > "$log" 2>&1 <<'INSIDE'
set -u
# The repository arrives through a bind mount of the Mac's volume, which stays case-INSENSITIVE
# inside the container (measured: Vector.H and vector.h are one inode there).  A real Linux is
# case-sensitive, so build from a copy on the container's own filesystem.
#
# And copy TRACKED files under git's names, not the disk's.  On a case-folding host a file can be
# buff.h on disk and BUFF.H in the index - a reset of a staged case-only rename leaves exactly that,
# and it happened while this script was written - and a copy of the disk would build a tree that
# git does not hold.  Untracked files (the vendored sources) have no index name and keep the disk's.
#
# The control: git stores WWLib's header as Vector.H, so vector.h must NOT resolve in the copy.  If
# it does, this row is not testing Linux and says so.
mkdir -p /tmp/src
cd /src && xargs -0 cp --parents -t /tmp/src < /lists/files.list || { echo "@@copyfailed"; exit 0; }
cd /
if [ -e /tmp/src/GeneralsMD/Code/Libraries/Source/WWVegas/WWLib/vector.h ]; then
  echo "@@casefold yes"
  exit 0
fi
echo "@@ran $(uname -m) $($CXX --version | head -1), case-sensitive copy"
B=/tmp/build
cmake -S /tmp/src/GeneralsMD/Code -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "@@configure $?"
[ -e $B/build.ninja ] || exit 0
cmake --build $B -- -k 0 2>&1 | tee /tmp/build.log
status=${PIPESTATUS[0]}
echo "@@build $status $(grep -c '^FAILED:' /tmp/build.log)"
grep '^FAILED:' /tmp/build.log | sed 's/^/@@failed /'
cd $B && ctest --output-on-failure 2>&1 | tee /tmp/ctest.log
echo "@@ctest ${PIPESTATUS[0]}"
sed -n 's/^.*tests passed, \([0-9]*\) tests failed out of \([0-9]*\).*$/@@counts \1 \2/p' /tmp/ctest.log
grep -E 'Not Run \(Disabled\)' /tmp/ctest.log | sed -E 's/^.*Test +#[0-9]+: ([^ ]+).*/@@disabled \1/'
grep -E '\*\*\*Skipped' /tmp/ctest.log | sed -E 's/^.*Test +#[0-9]+: ([^ ]+).*/@@skipped \1/'
grep -E '\*\*\*(Failed|Not Run)( |$)|\*\*\*Exception' /tmp/ctest.log | grep -v Disabled \
  | sed -E 's/^.*Test +#[0-9]+: ([^ ]+).*/@@testfail \1/'
INSIDE
}

field() { sed -n "s/^@@$2 //p" "$1" | head -1; }
list()  { sed -n "s/^@@$2 //p" "$1" | tr '\n' ' ' | sed 's/ $//'; }

overall=0
table=()
for row in "${rows[@]}"; do
  arch="${row%/*}"; compiler="${row#*/}"
  case "$arch" in arm64|amd64) ;; *) fail "unknown architecture in '$row' (arm64|amd64)";; esac
  log="$work/$arch-$compiler.log"
  say "linux/$arch $compiler ... (log: $log)"
  start=$(date +%s)
  run_row "$arch" "$compiler" "$log"
  secs=$(( $(date +%s) - start ))

  cfg=$(field "$log" configure); cfg=${cfg:-none}
  read -r bstat bfailed <<<"$(field "$log" build)"; bstat=${bstat:-none}; bfailed=${bfailed:-?}
  ctest_status=$(field "$log" ctest); ctest_status=${ctest_status:-none}
  read -r tfailed ttotal <<<"$(field "$log" counts)"; tfailed=${tfailed:-?}; ttotal=${ttotal:-?}
  disabled=$(list "$log" disabled); skipped=$(list "$log" skipped); testfail=$(list "$log" testfail)

  ran=$(field "$log" ran)
  verdict=PASS
  if [ "$cfg" != 0 ] || [ "$bstat" != 0 ] || [ "$ctest_status" != 0 ]; then verdict=FAIL; overall=1; fi
  # A row that ran nothing, or ran ctest over nothing, is a failure however the fields read.
  if [ -n "$(field "$log" casefold)" ]; then
    say "linux/$arch $compiler: the source copy is case-insensitive, so this row cannot test Linux"
  fi
  if [ -z "$ran" ]; then verdict=FAIL; overall=1; say "linux/$arch $compiler: the container ran nothing"; fi
  if [ "$ttotal" = "?" ] || [ "$ttotal" = 0 ]; then verdict=FAIL; overall=1; fi
  if [ "$ttotal" != "?" ] && [ "$tfailed" != "?" ]; then passed="$((ttotal - tfailed))/$ttotal"; else passed="-"; fi
  table+=("$(printf '%-12s %-6s %-9s %-15s %-8s %-5s %4ss' "linux/$arch" "$compiler" \
      "$( [ "$cfg" = 0 ] && echo ok || echo "FAIL($cfg)")" \
      "$( [ "$bstat" = 0 ] && echo ok || echo "FAIL $bfailed edges")" \
      "$passed" "$verdict" "$secs")")
  table+=("$(printf '             ran: %s' "${ran:-NOTHING}")")
  table+=("$(printf '             failing: %s' "${testfail:-none}")")
  table+=("$(printf '             disabled: %s' "${disabled:-none}")")
  table+=("$(printf '             skipped: %s' "${skipped:-none}")")
done

echo
printf '%-12s %-6s %-9s %-15s %-8s %-5s %5s\n' platform cc configure build ctest verdict time
for line in "${table[@]}"; do echo "$line"; done
echo
if [ $overall -eq 0 ]; then
  say "all rows passed - and read the header of this file before calling that more than Linux"
else
  say "FAIL: at least one row did not configure, build or pass ctest. Logs: $work"
fi
exit $overall
