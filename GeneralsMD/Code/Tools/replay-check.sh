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
#          [--control] [--extended] [--keep]
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
#   --extended        the wider backstop, not in ctest: seeds 2 to 5, each at 2 and at 4 players (eight
#                     matches), at --maxframes (default 12000).  --seeds and --players are ignored
#   --keep            leave the temporary folder and the logs, and say where
# Exit status: the number of matches that failed (0 when all agree).  77 when there is no game data.
# 99 when the install changed during the run: every file of it is hashed before the farm is built and
# again at the end, and any difference is reported and fails the run whatever the matches said.

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
EXTENDED=0
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
		--extended) EXTENDED=1; shift;;
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

# The install, hashed before anything else happens (RULE 9: a farm entry is a link into it, so a write
# through one would change it).  Checked again on the way out, however the run ends.
install_hashes() { ( cd "$INSTALL" && find . -type f ! -name '._*' -print0 | sort -z | xargs -0 shasum -a 256 ); }
INSTALL_VERIFIED=0
verify_install() {
	[ "$INSTALL_VERIFIED" -eq 1 ] && return 0
	INSTALL_VERIFIED=1
	install_hashes > "$WORK/install-after.sha"
	if ! cmp -s "$WORK/install-before.sha" "$WORK/install-after.sha"; then
		echo "INSTALL CHANGED during this run (rule 9) - the files that differ:" >&2
		diff "$WORK/install-before.sha" "$WORK/install-after.sha" >&2
		return 1
	fi
	return 0
}

cleanup() {
	if ! verify_install; then
		KEEP=1
		trap - EXIT
		echo "kept for inspection: $WORK" >&2
		exit 99
	fi
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

install_hashes > "$WORK/install-before.sha"
# The check's own armed control, which never touches the install: it spoils the saved snapshot, so the
# comparison at the end must find a difference.  Used by Tests/run_replay_check.sh.
if [ -n "${REPLAY_CHECK_CONTROL_INSTALL:-}" ]; then
	echo "0000000000000000000000000000000000000000000000000000000000000000  ./replay-check-control" >> "$WORK/install-before.sha"
fi

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
	RUN_CRC=""; RUN_FRAME=""; RUN_RESULT=""; RUN_BUILT=0; RUN_STATS=""
	[ -f "$log" ] || return
	local line
	line="$(grep -a 'HEADLESS CRC: 0x' "$log" | tail -1)"
	RUN_CRC="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1/p')"
	RUN_FRAME="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\2/p')"
	RUN_RESULT="$(grep -a 'HEADLESS RESULT: ' "$log" | tail -1 | sed 's/.*HEADLESS RESULT: //')"
	RUN_BUILT="$(grep -a -c 'AI BUILT frame [1-9]' "$log")"
	# each player's fight: units built, units lost, kills, peak units, buildings built, buildings lost
	RUN_STATS="$(grep -a 'HEADLESS PLAYER' "$log" | sed -E 's/.*units ([0-9]+) built ([0-9]+) lost ([0-9]+) killed peak ([0-9]+) \| buildings ([0-9]+) built ([0-9]+) lost.*/units \1 lost \2 kills \3 peak \4 buildings \5 lost \6/' | paste -sd ';' - | sed 's/;/; /g')"
}

REPLAYS="$USERDATA/Replays"
# The matches, as seed:players.
MATCHES=""
if [ "$EXTENDED" -eq 1 ]; then
	for seed in 2 3 4 5; do MATCHES="$MATCHES $seed:2 $seed:4"; done
else
	for seed in $SEEDS; do MATCHES="$MATCHES $seed:$PLAYERS"; done
fi
failures=0
nseeds=0
for match in $MATCHES; do
	seed="${match%%:*}"; players="${match##*:}"; name="det${seed}_${players}p"
	nseeds=$((nseeds + 1))
	printf 'seed %s, %s players: recording ... ' "$seed" "$players"
	rm -f -- "$REPLAYS/00000000.rep"
	# -observer, so every side is AI and the command stream is the AI's own decisions
	run_game "${name}_live" -randommap "$seed" "$players" $CELLS -autoskirmish "$players" \
		-aidiff "$AIDIFF" -seed "$seed" -observer
	LIVE_CRC="$RUN_CRC"; LIVE_FRAME="$RUN_FRAME"; LIVE_BUILT="$RUN_BUILT"; LIVE_STATS="$RUN_STATS"
	if [ -z "$LIVE_CRC" ]; then echo "no result from the live run (exit $RUN_STATUS)"; failures=$((failures + 1)); continue; fi
	if [ ! -f "$REPLAYS/00000000.rep" ]; then echo "the live run wrote no replay"; failures=$((failures + 1)); continue; fi
	# out of the way of the next recording, and under a name -replay can be given
	mv -- "$REPLAYS/00000000.rep" "$REPLAYS/determinism${seed}_${players}.rep"
	if [ "$CONTROL" -eq 1 ]; then
		# the same length, so nothing after it in the header moves
		perl -0777 -pi -e 's/SD=(\d*)(\d);/"SD=".$1.(($2+1)%10).";"/e' "$REPLAYS/determinism${seed}_${players}.rep"
	fi
	printf 'frame %s, CRC %s, %s structures built after frame 0 (%s); playing back ... ' "$LIVE_FRAME" "$LIVE_CRC" "$LIVE_BUILT" "$RUN_RESULT"

	run_game "${name}_back" -replay "determinism${seed}_${players}"
	BACK_CRC="$RUN_CRC"; BACK_FRAME="$RUN_FRAME"

	printf 'again ... '
	rm -f -- "$REPLAYS/00000000.rep"
	run_game "${name}_again" -randommap "$seed" "$players" $CELLS -autoskirmish "$players" \
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
		echo "  seed $seed, $players players: HEADLESS CRC $LIVE_CRC at frame $LIVE_FRAME"
		echo "    the fight, per player: $LIVE_STATS"
	else
		echo "FAILED, live $LIVE_CRC at frame $LIVE_FRAME:$bad"
		failures=$((failures + 1))
	fi
done

echo
if [ "$failures" -eq 0 ]; then
	echo "$nseeds of $nseeds matches: each replay played back to the same world, and each seed played the same twice (this machine only)."
else
	echo "$failures of $nseeds matches did not agree with themselves on this machine."
fi
if ! verify_install; then
	KEEP=1
	exit 99
fi
exit "$failures"
