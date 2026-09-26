#!/usr/bin/env bash
#
# replay-check.sh: replay-check.ps1's POSIX twin (E1).  For each seed, plays a headless skirmish, plays
# its own replay back, plays the same seed a second time, and compares the three runs' CRC of every
# object in the world at the frame they stopped on: the last "HEADLESS CRC: 0x... at frame N" line,
# which any build writes at the end of an unattended run.
#
# WHAT THIS PROVES, AND WHAT IT DOES NOT.  Same-machine determinism only: a recording and its playback
# walk the same frames, and two runs from one seed agree, on this machine and this build.  That catches
# uninitialised memory, iteration order that depends on addresses, and anything else that differs
# between two runs of one binary.  It says NOTHING about whether this build agrees with a Windows build:
# that needs a replay recorded on Windows, played back here, and none exists yet.  Do not call a pass
# "parity" (E1's task file).
#
# A mismatch names the frame the CRC was taken at, not the frame the runs parted: it says a divergence
# happened, not where.  -DebugCRCFromFrame narrows it down afterwards.
#
# RULE 9: the game never runs with the real install as its root.  The root is a farm in a temporary
# folder: the install's directories made anew and every file a symbolic link, with the fork's own data
# (Code/Data) laid over it as generals' post-build lays it over Run/ on Windows.  A farm file is a link
# into the install, so each overlaid one is unlinked before its copy is written.  The user data
# (ZH_USER_DATA_DIR) is in the same folder, and the folder is removed at the end.  The logs go next to
# the executable, as on Windows, each under its own -logPrefix, and are removed once read.
#
# Usage: replay-check.sh --generals <path> [--data <dir>] [--seeds "0 1"] [--players 2]
#          [--aidiff brutal] [--maxframes 12000] [--cells <n>] [--extra "<args>"]
#          [--control] [--keep]
#   --data            a folder holding zerohour/ (with the base game in zerohour/ZH_Generals, as the
#                     install has it); default $ZH_DATA_DIR
#   --cells           playable cells a side of the generated map.  Default: none given, so the
#                     generator sizes the map for the player count (normal).  replay-check.ps1 passes
#                     128, below even the generator's small size for two players, and at 128 seed 1
#                     puts the two starts 124 units apart: both AIs judge every build site unsafe and
#                     the match stays idle, so a CRC over it proves little
#   --extra           switches for every run of every seed.  One that reaches the match has to be on
#                     for the recording and the playback alike
#   --control         the harness's own armed control: before the playback, the kept replay's game
#                     seed (SD= in its header) has its last digit changed, so the playback plays a
#                     different world from the same commands, and every seed must be reported DIVERGED
#   --keep            leave the temporary folder and the logs, and say where
# Exit status: the number of seeds that failed (0 when all agree).  77 when there is no game data.

set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
SEEDS="0 1"
PLAYERS=2
AIDIFF=brutal
MAXFRAMES=12000
CELLS=""
EXTRA=""
CONTROL=0
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--seeds) SEEDS="$2"; shift 2;;
		--players) PLAYERS="$2"; shift 2;;
		--aidiff) AIDIFF="$2"; shift 2;;
		--maxframes) MAXFRAMES="$2"; shift 2;;
		--cells) CELLS="$2"; shift 2;;
		--extra) EXTRA="$2"; shift 2;;
		--control) CONTROL=1; shift;;
		--keep) KEEP=1; shift;;
		*) echo "replay-check: unknown argument $1" >&2; exit 2;;
	esac
done

if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "replay-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"		# GeneralsMD/Code: its Data/ is the overlay
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"		# absolute: every run starts in the root
WORK="$(mktemp -d "${TMPDIR:-/tmp}/replay-check.XXXXXX")"
ROOT="$WORK/root"
USERDATA="$WORK/user"
TAG="rc$$_"		# every log this run writes starts with it

cleanup() {
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

# ---- the farm ------------------------------------------------------------------------------------
mkdir -p "$ROOT" "$USERDATA"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done

overlay() {	# overlay <file or folder under Code/Data> <where under the root>
	local src="$CODE/Data/$1" dst="$ROOT/$2"
	if [ -d "$src" ]; then
		( cd "$src" && find . -type f ) | while IFS= read -r f; do
			mkdir -p "$(dirname "$dst/$f")"
			rm -f -- "$dst/$f"
			cp -- "$src/$f" "$dst/$f"
		done
	elif [ -f "$src" ]; then
		mkdir -p "$(dirname "$dst")"
		rm -f -- "$dst"
		cp -- "$src" "$dst"
	fi
}
# generals' post-build list on Windows (CMakeLists.txt), in its order
overlay INI Data/INI
overlay Patch.str Data/Patch.str
overlay Scripts Data/Scripts
overlay Turkish Data/Turkish
overlay Install_Final.bmp Install_Final.bmp
overlay Art/Textures Art/Textures
overlay Window Window
overlay Scenarios Scenarios
overlay Cinema Cinema

# ---- a run ---------------------------------------------------------------------------------------
# Sets RUN_CRC and RUN_FRAME from the run's last HEADLESS CRC line, RUN_RESULT from its HEADLESS
# RESULT line, and RUN_BUILT to the number of structures the AI put up after frame 0; empty when the
# run wrote none.
run_game() {	# run_game <log prefix> <switches...>
	local prefix="$TAG$1"; shift
	local log="$EXEDIR/${prefix}DebugLogFile.txt"
	rm -f -- "$log"
	# -noFPSLimit as replay-check.ps1 has it: nothing paces a headless run, and it cannot touch the
	# logic.  -multiInstance so that a run does not wait on another copy's lock.
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" "$GENERALS" -headless -root "$ROOT" -quickstart -noshellmap \
		-multiInstance -noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$prefix" "$@" $EXTRA \
		> "$WORK/${prefix}.out" 2> "$WORK/${prefix}.err" )
	RUN_STATUS=$?
	RUN_CRC=""; RUN_FRAME=""; RUN_RESULT=""; RUN_BUILT=0
	[ -f "$log" ] || return
	local line
	line="$(grep -a 'HEADLESS CRC: 0x' "$log" | tail -1)"
	RUN_CRC="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1/p')"
	RUN_FRAME="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\2/p')"
	RUN_RESULT="$(grep -a 'HEADLESS RESULT: ' "$log" | tail -1 | sed 's/.*HEADLESS RESULT: //')"
	RUN_BUILT="$(grep -a -c 'AI BUILT frame [1-9]' "$log")"
}

REPLAYS="$USERDATA/Replays"
failures=0
nseeds=0
for seed in $SEEDS; do
	nseeds=$((nseeds + 1))
	printf 'seed %s: recording ... ' "$seed"
	rm -f -- "$REPLAYS/00000000.rep"
	# -observer, so both sides are AI and the command stream is the AI's own decisions
	run_game "det${seed}_live" -randommap "$seed" "$PLAYERS" $CELLS -autoskirmish "$PLAYERS" \
		-aidiff "$AIDIFF" -seed "$seed" -observer
	LIVE_CRC="$RUN_CRC"; LIVE_FRAME="$RUN_FRAME"; LIVE_BUILT="$RUN_BUILT"
	if [ -z "$LIVE_CRC" ]; then echo "no result from the live run (exit $RUN_STATUS)"; failures=$((failures + 1)); continue; fi
	if [ ! -f "$REPLAYS/00000000.rep" ]; then echo "the live run wrote no replay"; failures=$((failures + 1)); continue; fi
	# out of the way of the next recording, and under a name -replay can be given
	mv -- "$REPLAYS/00000000.rep" "$REPLAYS/determinism${seed}.rep"
	if [ "$CONTROL" -eq 1 ]; then
		# the same length, so nothing after it in the header moves
		perl -0777 -pi -e 's/SD=(\d*)(\d);/"SD=".$1.(($2+1)%10).";"/e' "$REPLAYS/determinism${seed}.rep"
	fi
	printf 'frame %s, CRC %s, %s structures built after frame 0 (%s); playing back ... ' "$LIVE_FRAME" "$LIVE_CRC" "$LIVE_BUILT" "$RUN_RESULT"

	run_game "det${seed}_back" -replay "determinism${seed}"
	BACK_CRC="$RUN_CRC"; BACK_FRAME="$RUN_FRAME"

	printf 'again ... '
	rm -f -- "$REPLAYS/00000000.rep"
	run_game "det${seed}_again" -randommap "$seed" "$PLAYERS" $CELLS -autoskirmish "$PLAYERS" \
		-aidiff "$AIDIFF" -seed "$seed" -observer
	AGAIN_CRC="$RUN_CRC"; AGAIN_FRAME="$RUN_FRAME"

	bad=""
	# An idle match agrees with itself and proves nothing: a divergence in movement, combat or the AI
	# cannot show in a world where nothing happens.
	if [ "$LIVE_BUILT" -eq 0 ]; then
		bad="${bad} IDLE: the AI built nothing after frame 0;"
	fi
	if [ "$BACK_CRC" != "$LIVE_CRC" ] || [ "$BACK_FRAME" != "$LIVE_FRAME" ]; then
		bad="${bad} playback ${BACK_CRC:-none} at frame ${BACK_FRAME:-none};"
	fi
	if [ "$AGAIN_CRC" != "$LIVE_CRC" ] || [ "$AGAIN_FRAME" != "$LIVE_FRAME" ]; then
		bad="${bad} second run ${AGAIN_CRC:-none} at frame ${AGAIN_FRAME:-none};"
	fi
	if [ -z "$bad" ]; then
		echo "the playback and the second run agree at frame $LIVE_FRAME"
		echo "  seed $seed: HEADLESS CRC $LIVE_CRC at frame $LIVE_FRAME"
	else
		echo "FAILED, live $LIVE_CRC at frame $LIVE_FRAME:$bad"
		failures=$((failures + 1))
	fi
done

echo
if [ "$failures" -eq 0 ]; then
	echo "$nseeds of $nseeds seeds: each replay played back to the same world, and each seed played the same twice (this machine only)."
else
	echo "$failures of $nseeds seeds did not agree with themselves on this machine."
fi
exit "$failures"
