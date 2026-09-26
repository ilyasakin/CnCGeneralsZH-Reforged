#!/usr/bin/env bash
#
# P1 step 3's proof that the package layout resolves every file exactly as Windows' one folder does
# (decision 9), path by path, not only in the CRCs that overlay-crc-check.sh compares.
#
#   W  the Windows shape: one folder, a farm of the install with the staged overlay copied into it;
#   P  the package shape: the farm of the install as the root, the staged overlay as -overlay.
#
# The overlay is staged by Tools/stage-overlay.sh, the script the build's zh_overlay target and the app
# bundle use. Each layout is started with -dumpFileResolution, which after GameEngine::init writes, for
# every path the game can open (the loose files under its roots and every file in the mounted
# archives), "loose" with the size and hash of its bytes, or the archive that won it with the member's
# size, then the INI and EXE CRCs (PosixFileResolutionDump.cpp). The two dumps must be identical.
#
# The armed control: P again with the overlay's ReforgedTextures.big renamed ZReforgedTextures.big. It
# then mounts after TexturesZH.big instead of before, so every path both archives hold must change
# hands, and the dump must differ in exactly such lines. That proves the comparison sees archive order,
# not only names.
#
# Rule 9, and P1 step 2's standard: the install is hashed before this script writes anything and again
# at the end, and must be unchanged; every write into a farm path removes the farm's link first.
#
# Usage: packaging-resolution-check.sh --generals <path> [--data <dir>] [--keep]
# Exit status: 0 on a pass; 1 otherwise; 77 without game data.

set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "packaging-resolution-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "packaging-resolution-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

TOOLS="$(cd "$(dirname "$0")" && pwd)"
CODE="$(cd "$TOOLS/.." && pwd)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/packaging-resolution-check.XXXXXX")"
TAG="pr$$_"

cleanup() {
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

hash_install() {	# hash_install <out>: every install entry's size, mtime and BLAKE2, read only
	python3 - "$INSTALL" > "$1" <<'EOF'
import hashlib, os, sys
root = sys.argv[1]
for base, dirs, files in os.walk(root):
    dirs.sort()
    for name in sorted(dirs + files):
        p = os.path.join(base, name)
        st = os.lstat(p)
        if os.path.isdir(p):
            print(os.path.relpath(p, root), 'dir'); continue
        h = hashlib.blake2b(digest_size=16)
        with open(p, 'rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        print(os.path.relpath(p, root), st.st_size, int(st.st_mtime_ns), h.hexdigest())
EOF
}

farm() {	# the install's folders made anew, every file a link
	mkdir -p "$1"
	( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$1/$d"; done
	( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$1/$f"; done
}

lay_over() {	# lay_over <overlay> <farm>: the overlay's entries into the farm, each farm link removed first
	( cd "$1" && find . \( -type f -o -type l \) ) | while IFS= read -r f; do
		mkdir -p "$(dirname "$2/$f")"
		rm -f -- "$2/$f"
		cp -P -- "$1/$f" "$2/$f"
	done
}

dump() {	# dump <name> <root> <out> <switches...>
	local name="$1" root="$2" out="$3"; shift 3
	mkdir -p "$WORK/user_$name"
	( cd "$root" && ZH_USER_DATA_DIR="$WORK/user_$name" "$GENERALS" -headless -root "$root" "$@" -quickstart -noshellmap \
		-multiInstance -logPrefix "$TAG$name" -dumpFileResolution "$out" \
		> "$WORK/$name.out" 2> "$WORK/$name.err" )
}

hash_install "$WORK/install.before"
"$TOOLS/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$WORK/overlay"
ARTS=$(ls "$WORK/overlay"/Reforged*.big 2>/dev/null | wc -l | tr -d ' ')

farm "$WORK/W"
lay_over "$WORK/overlay" "$WORK/W"
farm "$WORK/P"
dump W "$WORK/W" "$WORK/W.dump"
dump P "$WORK/P" "$WORK/P.dump" -overlay "$WORK/overlay"

status=0
if [ ! -s "$WORK/W.dump" ] || [ ! -s "$WORK/P.dump" ]; then
	echo "FAIL: a layout wrote no dump (see $WORK, kept)"; KEEP=1; status=1
else
	loose=$(grep -c $'\tloose\t' "$WORK/W.dump"); archived=$(grep -c $'\tarchive ' "$WORK/W.dump")
	echo "W: $loose loose paths and $archived archived; the overlay staged with $ARTS art archives"
	grep -a '^INI CRC\|^EXE CRC' "$WORK/W.dump" | sed 's/^/W: /'
	if cmp -s "$WORK/W.dump" "$WORK/P.dump"; then
		echo "ok: every path resolves the same in W and P ($(wc -l < "$WORK/W.dump" | tr -d ' ') lines, the CRCs included)"
	else
		echo "FAIL: the package layout resolves differently:"; diff "$WORK/W.dump" "$WORK/P.dump" | head -20; status=1
	fi
fi

# ---- the armed control ----------------------------------------------------------------------------
if [ "$ARTS" -gt 0 ] && [ -L "$WORK/overlay/ReforgedTextures.big" ]; then
	cp -R -P "$WORK/overlay" "$WORK/overlay-renamed"
	mv "$WORK/overlay-renamed/ReforgedTextures.big" "$WORK/overlay-renamed/ZReforgedTextures.big"
	farm "$WORK/C"
	dump C "$WORK/C" "$WORK/C.dump" -overlay "$WORK/overlay-renamed"
	# lines where the winner changed from ReforgedTextures.big to another archive (not only its name)
	moved=$(diff "$WORK/P.dump" "$WORK/C.dump" | grep -a '^> ' | grep -a $'\tarchive ' | grep -av -i 'reforgedtextures.big' | wc -l | tr -d ' ')
	if [ -s "$WORK/C.dump" ] && [ "$moved" -gt 0 ]; then
		echo "ok: the control (ReforgedTextures.big mounted after TexturesZH.big) moves $moved paths to another archive"
	else
		echo "FAIL: the control moved no path to another archive, so this check cannot see archive order"; status=1
	fi
else
	echo "FAIL: no ReforgedTextures.big in the overlay (vendor.sh's art), so the control cannot run"; status=1
fi

hash_install "$WORK/install.after"
if cmp -s "$WORK/install.before" "$WORK/install.after"; then
	echo "ok: the install is as it was (sizes, times and contents)"
else
	echo "FAIL: THE INSTALL CHANGED:"; diff "$WORK/install.before" "$WORK/install.after" | head -5; status=1
fi
exit $status
