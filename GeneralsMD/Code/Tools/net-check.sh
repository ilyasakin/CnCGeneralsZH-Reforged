#!/usr/bin/env bash
#
# net-check.sh: net_check.py's POSIX twin (L1).  Two headless copies of the game on this machine play
# one seeded LAN match against each other (-netgame, no lobby), each with its own user data; then each
# copy's replay is played back alone.  The match passes when:
#   - both copies started the network game and neither log holds "CRC Mismatch" (the copies compare
#     their CRCs over the network every interval, so this is the game's own desync detector);
#   - both stop on the same HEADLESS CRC at the same frame, --frames;
#   - the AI built something after frame 0 on both (an idle world agrees with itself and proves little);
#   - each copy's replay, played back alone, stops on that same CRC at that frame.
#
# WHAT THIS PROVES, AND WHAT IT DOES NOT.  Two processes of POSIX builds on ONE machine keep one world
# over the real network code (Transport, the lockstep, the CRC exchange) through the kernel's own
# delivery between two local addresses.  With --peer1 naming an x86_64 build run under Rosetta, the
# two copies are two architectures of one compiler.  It says NOTHING about:
#   - a Windows peer (MSVC's code generation, its 32-bit long, its CRT);
#   - a real network: traffic between two addresses of one host never leaves the kernel, so there is no
#     latency, loss, reordering or MTU to meet (the game's -latAvg/-latNoise/-packetloss simulate some);
#   - the LAN lobby: -netgame skips it, so discovery, joining, map transfer and chat are not exercised;
#   - more than two humans.
#
# THE ADDRESSES.  One port (8088, and the lobby's 8086) serves every address and the game finds its own
# slot by address, so two copies need two addresses.  macOS configures only 127.0.0.1 on loopback (an
# alias needs root), so the second is one of the machine's own: the first up, non-loopback IPv4 address
# that a probe finds delivering to and from 127.0.0.1.  None: exit 77.  Both ports must be free on both.
#
# THE FIREWALL.  A copy listening on a non-loopback address makes the macOS application firewall ask
# "accept incoming connections?" in a dialog, when it is on.  This harness must never put that up, so on
# macOS it reads socketfilterfw's global state, stealth mode and block-all first and skips (77) unless
# all three are off - before the probe, which listens too.  What that check cannot see: third-party
# network filters (Little Snitch, LuLu and the like) that ask on their own, pf rules, and a firewall
# switched on between the check and the run.  Off macOS there is no such dialog and nothing is read.
#
# ONE AT A TIME, MACHINE-WIDE.  The game's ports are the machine's, and two net-checks from two worktrees
# (ctest's RESOURCE_LOCK holds only within one ctest) once collided: the copy that could not bind 8088
# spun in Transport::init's bind retry, leaking a socket per try, until the machine's file table was full
# (2026-09-26).  So before its probe the harness takes an exclusive lock on one fixed path,
# /tmp/zhr-net-check.lock (NET_CHECK_LOCK_FILE), and waits for it (NET_CHECK_LOCK_WAIT, 900 s, then skips).
# The lock is flock(2), taken through python3's fcntl (macOS has no flock(1)) on descriptor 9, which the
# harness and both copies of the game inherit: the kernel holds it until every one of them has exited -
# a copy that outlives a killed harness keeps it - and releases it however they end, so it cannot go
# stale.  And every copy runs under `ulimit -n` of NET_CHECK_FD_LIMIT (4096): a leak ends in EMFILE for
# that copy instead of the machine.  A watchdog stops both copies once either holds three quarters of
# that, and fails the run saying so.
#
# RULE 9: the game never runs with the real install as its root: a farm of links, the overlay staged
# as the app ships it, the user data in the work folder, the install hashed before and after
# (Tools/install-guard.sh), exit 99 if it changed or could not be checked.
#
# Usage: net-check.sh --generals <path> [--peer1 <path>] [--data <dir>] [--seed 3] [--frames 3000]
#          [--netai 2] [--map "Maps\Golden Oasis\Golden Oasis.map"] [--control] [--corrupt-at <frame>] [--legacy-replay] [--keep]
#   --peer1     the executable for the second copy (default: --generals); an x86_64 build runs under
#               Rosetta as it is
#   --corrupt-at <frame>
#               the playback check's control: before copy 0's replay is played back, every CRC it
#               recorded at the first CRC frame at or after <frame> is changed by one bit.  Copy 0's
#               playback must then report "out of sync" at exactly that frame (so the check is live
#               to the end of the replay), and copy 1's untouched replay must still play back clean
#   --legacy-replay
#               the other alignment: before copy 1's replay is played back, the records of the first
#               frame that carries CRCs are removed, which leaves it shaped like a replay recorded
#               before d9eccdda or by the retail game (no frame 0 CRC).  Its playback must say
#               "legacy: frame 0 missing" and stay in sync
#   --control   the armed control: the second copy is given the next seed, so the two copies start
#               different worlds from one command stream; the match must be reported FAILED with a CRC
#               mismatch, so a pass means the check can see one
# Exit status: 0 the match agrees; 1 it does not; 77 skipped (no data, no second address, a port in
# use for NET_CHECK_PORT_WAIT seconds (600), the firewall on, or no python3 for the probe); 99 the install
# changed or could not be checked.

set -u

GENERALS=""
PEER1=""
DATA="${ZH_DATA_DIR:-}"
SEED=3
FRAMES=3000
NETAI=2
MAP='Maps\Golden Oasis\Golden Oasis.map'
CONTROL=0
CORRUPT_AT=""
LEGACY=0
KEEP=0
LIVE_TIMEOUT="${NET_CHECK_LIVE_TIMEOUT:-900}"		# seconds for the match; 3000 frames took 105 s
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--peer1) PEER1="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--seed) SEED="$2"; shift 2;;
		--frames) FRAMES="$2"; shift 2;;
		--netai) NETAI="$2"; shift 2;;
		--map) MAP="$2"; shift 2;;
		--control) CONTROL=1; shift;;
		--corrupt-at) CORRUPT_AT="$2"; shift 2;;
		--legacy-replay) LEGACY=1; shift;;
		--keep) KEEP=1; shift;;
		*) echo "net-check: unknown argument $1" >&2; exit 2;;
	esac
done
[ -n "$PEER1" ] || PEER1="$GENERALS"
for exe in "$GENERALS" "$PEER1"; do
	if [ -z "$exe" ] || [ ! -x "$exe" ]; then
		echo "net-check: --generals (and --peer1) must name a POSIX generals executable" >&2
		exit 2
	fi
done
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

# ---- the firewall, before anything listens ----------------------------------------------------------
if [ "$(uname -s)" = "Darwin" ]; then
	# NET_CHECK_FIREWALL_TOOL stands in for socketfilterfw in the gate's own control (Tests/run_net_check.sh
	# gives it fakes that report the firewall on); nothing else should set it
	FW="${NET_CHECK_FIREWALL_TOOL:-/usr/libexec/ApplicationFirewall/socketfilterfw}"
	if [ ! -x "$FW" ]; then
		echo "skip: no $FW to ask whether the application firewall is on, so a copy could put up its dialog"
		exit 77
	fi
	state="$("$FW" --getglobalstate 2>&1)"
	stealth="$("$FW" --getstealthmode 2>&1)"
	blockall="$("$FW" --getblockall 2>&1)"
	case "$state" in *"State = 0"*) ;; *) echo "skip: the application firewall is on ($state); a copy listening off loopback would ask in a dialog"; exit 77;; esac
	case "$stealth" in *"is off"*) ;; *) echo "skip: firewall stealth mode is not off ($stealth)"; exit 77;; esac
	case "$blockall" in *"disabled"*) ;; *) echo "skip: firewall block-all is not disabled ($blockall)"; exit 77;; esac
fi

# ---- one net-check on the whole machine (see the header) ------------------------------------------------
LOCK_FILE="${NET_CHECK_LOCK_FILE:-/tmp/zhr-net-check.lock}"
LOCK_WAIT="${NET_CHECK_LOCK_WAIT:-900}"
if ! exec 9>>"$LOCK_FILE"; then
	echo "skip: cannot open the machine-wide lock $LOCK_FILE"
	exit 77
fi
if ! python3 - "$LOCK_FILE" "$LOCK_WAIT" "$$" "$PWD" <<'LOCK_EOF'
import fcntl, os, sys, time
path, wait, pid, where = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4]
deadline, said = time.time() + wait, False
while True:
    try:
        fcntl.flock(9, fcntl.LOCK_EX | fcntl.LOCK_NB)	# on the harness's own descriptor: it outlives this helper
        break
    except OSError:
        try:
            holder = open(path).read().strip() or "unknown"
        except OSError:
            holder = "unknown"
        if time.time() >= deadline:
            print("skip: another net-check held the machine-wide lock for %d s (%s)" % (wait, holder))
            sys.exit(1)
        if not said:
            print("net-check: waiting up to %d s for another net-check on this machine (%s)" % (wait, holder), flush=True)
            said = True
        time.sleep(2)
os.ftruncate(9, 0)
os.write(9, ("pid %s, %s, since %s" % (pid, where, time.strftime("%H:%M:%S"))).encode())
LOCK_EOF
then
	exit 77
fi

# ---- the second address, found by a probe ------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
	echo "skip: no python3 for the address probe"
	exit 77
fi
probe() {
	python3 - <<'PROBE_EOF'
import re, socket, subprocess, sys

def candidates():
    """Up, non-loopback IPv4 addresses: ifconfig's (macOS, BSD), else ip's (Linux)."""
    found = []
    try:
        text = subprocess.run(["ifconfig", "-a"], capture_output=True, text=True, timeout=10).stdout
        up = False
        for line in text.splitlines():
            if line and not line[0].isspace():
                up = bool(re.search(r"flags=\w*<([^>]*\b)?UP\b", line)) or " UP " in line
            m = re.match(r"\s+inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", line)
            if m and up:
                found.append(m.group(1))
    except (OSError, subprocess.SubprocessError):
        pass
    if not found:
        try:
            text = subprocess.run(["ip", "-4", "-o", "addr", "show", "up"], capture_output=True, text=True, timeout=10).stdout
            found = re.findall(r"inet (\d+\.\d+\.\d+\.\d+)/", text)
        except (OSError, subprocess.SubprocessError):
            pass
    return [a for a in found if not a.startswith("127.")]

def delivers(a, b):
    """A datagram each way between a and b, on ports the kernel picks."""
    try:
        s1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s1.bind((a, 0)); s2.bind((b, 0)); s1.settimeout(0.5); s2.settimeout(0.5)
        s1.sendto(b"net-check a", s2.getsockname())
        d1, f1 = s2.recvfrom(64)
        s2.sendto(b"net-check b", s1.getsockname())
        d2, f2 = s1.recvfrom(64)
        return d1 == b"net-check a" and f1[0] == a and d2 == b"net-check b" and f2[0] == b
    except OSError:
        return False
    finally:
        s1.close(); s2.close()

def port_free(addr, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind((addr, port)); return True
    except OSError:
        return False
    finally:
        s.close()

tried = []
for c in candidates():
    if delivers("127.0.0.1", c):
        busy = [f"{a}:{p}" for a in ("127.0.0.1", c) for p in (8086, 8088) if not port_free(a, p)]
        if busy:
            print("BUSY " + " ".join(busy)); sys.exit(0)
        print("ADDR " + c); sys.exit(0)
    tried.append(c)
print("NONE " + " ".join(tried))
PROBE_EOF
}
# A copy of this harness in another checkout, or a game someone is playing, holds the ports: wait for
# them (NET_CHECK_PORT_WAIT seconds, default 600) rather than skip, so two ctest runs on one machine
# queue instead of one of them passing as Skipped.
PORT_WAIT="${NET_CHECK_PORT_WAIT:-600}"
port_deadline=$(( $(date +%s) + PORT_WAIT ))
while :; do
	PROBE="$(probe)"
	case "$PROBE" in "BUSY "*) ;; *) break;; esac
	[ "$(date +%s)" -ge "$port_deadline" ] && break
	[ -n "${said_waiting:-}" ] || echo "net-check: waiting up to ${PORT_WAIT} s for the game's ports (${PROBE#BUSY })"
	said_waiting=1
	sleep 5
done
case "$PROBE" in
	"ADDR "*) SECOND="${PROBE#ADDR }";;
	"BUSY "*) echo "skip: the game's ports stayed in use for ${PORT_WAIT} s (${PROBE#BUSY }): another copy of the game is running"; exit 77;;
	*) echo "skip: no up, non-loopback IPv4 address delivers to and from 127.0.0.1 (tried: ${PROBE#NONE })"; exit 77;;
esac
HOSTS="127.0.0.1,$SECOND"

CODE="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
abs() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
EXE[0]="$(abs "$GENERALS")"; EXE[1]="$(abs "$PEER1")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/net-check.XXXXXX")"
ROOT="$WORK/root"
TAG="nc$$_"

. "$(dirname "$0")/install-guard.sh"	# install_snapshot, install_verify
INSTALL_VERIFIED=0
verify_install() {
	[ "$INSTALL_VERIFIED" -eq 1 ] && return 0
	INSTALL_VERIFIED=1
	install_verify "$INSTALL" "$WORK/install-before.list" "$WORK/install-after.list" > "$WORK/install-verdict.txt" 2>&1
	local verdict=$?
	[ "$verdict" -eq 0 ] && return 0
	cat "$WORK/install-verdict.txt" >&2 2>/dev/null || echo "FAIL: COULD NOT VERIFY the install: its work folder $WORK is gone" >&2
	return 1
}
PIDS=""
cleanup() {
	local p
	for p in $PIDS; do kill "$p" 2>/dev/null; done
	if ! verify_install; then
		trap - EXIT
		echo "kept for inspection: $WORK" >&2
		exit 99
	fi
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs ${TAG}*DebugLogFile.txt beside the executables"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$(dirname "${EXE[0]}")/${TAG}"*DebugLogFile*.txt "$(dirname "${EXE[1]}")/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT
# a signal (a ctest timeout, ^C, a closed pipe) exits through the EXIT trap too, so the copies are
# stopped, the work folder removed and the install checked however the run ends
trap 'exit 130' INT TERM HUP PIPE
install_snapshot "$INSTALL" "$WORK/install-before.list"

# ---- the farm, and the overlay as it ships ------------------------------------------------------------
mkdir -p "$ROOT"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
OVERLAY="$WORK/overlay"
"$(dirname "$0")/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$OVERLAY" > "$WORK/stage.out" 2>&1 || {
	echo "net-check: stage-overlay.sh failed:" >&2; cat "$WORK/stage.out" >&2; exit 1; }

# ---- a copy of the game ---------------------------------------------------------------------------------
FD_LIMIT="${NET_CHECK_FD_LIMIT:-4096}"
FD_ALARM=$(( FD_LIMIT * 3 / 4 ))
fd_count() {	# open descriptors of process $1
	if [ -d "/proc/$1/fd" ]; then ls "/proc/$1/fd" 2>/dev/null | wc -l | tr -d ' '
	else lsof -n -P -p "$1" 2>/dev/null | awk 'NR > 1' | wc -l | tr -d ' '; fi
}
# start_copy <exe index> <name> <switches...>: runs it in the background, its pid in LAST_PID
start_copy() {
	local exe="${EXE[$1]}" name="$2"; shift 2
	mkdir -p "$WORK/user-$name"
	rm -f -- "$(dirname "$exe")/${TAG}${name}DebugLogFile.txt"
	( cd "$ROOT" && ulimit -n "$FD_LIMIT" && exec env ZH_HIDDEN_WINDOW=1 ZH_USER_DATA_DIR="$WORK/user-$name" "$exe" -headless -root "$ROOT" \
		-overlay "$OVERLAY" -quickstart -noshellmap -multiInstance -noFPSLimit -maxframes "$FRAMES" \
		-logPrefix "${TAG}${name}" "$@" > "$WORK/$name.out" 2> "$WORK/$name.err" ) &
	LAST_PID=$!
	PIDS="$PIDS $LAST_PID"
}
log_of() { echo "$(dirname "${EXE[$1]}")/${TAG}$2DebugLogFile.txt"; }
crc_of() { grep -a 'HEADLESS CRC: 0x' "$1" 2>/dev/null | tail -1 | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1 \2/p'; }
# wait_all <seconds> <pids...>: 0 when every copy ended by itself; 1 when the time ran out; 2 when a log
# in WATCH_LOGS showed a CRC mismatch first; 3 when a copy held FD_ALARM descriptors or more (FD_REPORT
# says which).  In the last three cases the copies are killed: a copy that has seen a mismatch waits on
# its disconnect screen for good, and the verdict is already known.
WATCH_LOGS=""
wait_all() {
	local limit=$1; shift
	local deadline=$(( $(date +%s) + limit )) p alive why=0 l n
	while :; do
		alive=0
		for p in "$@"; do kill -0 "$p" 2>/dev/null && alive=1; done
		[ "$alive" -eq 0 ] && break
		for l in $WATCH_LOGS; do grep -a -q 'CRC Mismatch' "$l" 2>/dev/null && why=2; done
		for p in "$@"; do
			n=$(fd_count "$p")
			if [ "${n:-0}" -ge "$FD_ALARM" ]; then
				FD_REPORT="process $p held $n descriptors (the alarm is $FD_ALARM of a $FD_LIMIT limit): a descriptor leak"
				why=3
			fi
		done
		[ "$why" -eq 0 ] && [ "$(date +%s)" -ge "$deadline" ] && why=1
		if [ "$why" -ne 0 ]; then
			for p in "$@"; do kill "$p" 2>/dev/null; done
			for p in "$@"; do wait "$p" 2>/dev/null; done
			return $why
		fi
		sleep 1
	done
	for p in "$@"; do wait "$p" 2>/dev/null; done
	return 0
}

SEED1="$SEED"
[ "$CONTROL" -eq 1 ] && SEED1=$((SEED + 1))
what="seed $SEED"
[ "$CONTROL" -eq 1 ] && what="seed $SEED (the control: the second copy on seed $SEED1)"
echo "net-check: $HOSTS, $what, $NETAI AI, $FRAMES frames, map $MAP"
[ "${EXE[0]}" != "${EXE[1]}" ] && echo "  copy 0: ${EXE[0]}" && echo "  copy 1: ${EXE[1]}"

started=$(date +%s)
start_copy 0 live0 -netgame "$HOSTS" -netslot 0 -netai "$NETAI" -map "$MAP" -seed "$SEED"; P0=$LAST_PID
start_copy 1 live1 -netgame "$HOSTS" -netslot 1 -netai "$NETAI" -map "$MAP" -seed "$SEED1"; P1=$LAST_PID
WATCH_LOGS="$(log_of 0 live0) $(log_of 1 live1)"
wait_all "$LIVE_TIMEOUT" "$P0" "$P1"
ended=$?
WATCH_LOGS=""
case $ended in
	1) how=", STOPPED after $LIVE_TIMEOUT s";;
	2) how=", stopped at the first CRC mismatch";;
	3) how=", STOPPED: $FD_REPORT";;
	*) how="";;
esac
echo "  the match: $(( $(date +%s) - started )) s$how"

bad=""
[ "$ended" -eq 3 ] && bad=" DESCRIPTOR LEAK: $FD_REPORT;"
LIVE_CRC0=none; LIVE_FRAME0=none; LIVE_CRC1=none; LIVE_FRAME1=none
for s in 0 1; do
	log="$(log_of "$s" "live$s")"
	if [ ! -f "$log" ]; then bad="$bad copy $s wrote no log;"; continue; fi
	grep -a -q -e '-netgame: .* we are slot' "$log" || bad="$bad copy $s did not start the network game;"
	mism=$(grep -a -c 'CRC Mismatch' "$log")
	built=$(grep -a -c 'AI BUILT frame [1-9]' "$log")
	read -r crc frame <<< "$(crc_of "$log")"
	eval "LIVE_CRC$s=\${crc:-none}; LIVE_FRAME$s=\${frame:-none}"
	echo "  copy $s: HEADLESS CRC ${crc:-none} at frame ${frame:-none}; $mism CRC Mismatch lines; $built AI structures after frame 0"
	[ "$mism" -eq 0 ] || bad="$bad copy $s logged $mism CRC mismatches;"
	[ "$built" -gt 0 ] || bad="$bad IDLE: copy $s saw the AI build nothing after frame 0;"
	[ "${frame:-}" = "$FRAMES" ] || bad="$bad copy $s stopped at frame ${frame:-none}, not $FRAMES;"
done
if [ "$LIVE_CRC0" != "$LIVE_CRC1" ] || [ "$LIVE_FRAME0" != "$LIVE_FRAME1" ]; then
	bad="$bad the copies ended apart ($LIVE_CRC0 at $LIVE_FRAME0 against $LIVE_CRC1 at $LIVE_FRAME1);"
fi

# edit_replay corrupt|strip <replay> <CRC message type> <frame>: the replay's MSG_LOGIC_CRC records
# (Recorder.cpp writeToFile) are u32 frame, the message type, i32 player, 2 argument groups (one
# integer, one boolean), the CRC and the boolean; the type's number is the one this build logged ("CRC
# message is N").  corrupt changes by one bit every CRC recorded at the first CRC frame at or after
# <frame>; strip removes every record of the first CRC frame.  Prints that frame, or "none".
edit_replay() {
	python3 - "$@" <<'EDIT_EOF'
import struct, sys
mode, path, msgtype, at = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
b = bytearray(open(path, "rb").read())
tag, groups, size = struct.pack("<i", msgtype), bytes([2, 0, 1, 2, 1]), 22
records = [i for i in range(len(b) - size + 1) if b[i + 4:i + 8] == tag and b[i + 12:i + 17] == groups]
frames = sorted({struct.unpack_from("<I", b, i)[0] for i in records if struct.unpack_from("<I", b, i)[0] >= at})
if not frames:
    print("none"); sys.exit(0)
frame = frames[0]
chosen = [i for i in records if struct.unpack_from("<I", b, i)[0] == frame]
if mode == "corrupt":
    for i in chosen:
        struct.pack_into("<I", b, i + 17, struct.unpack_from("<I", b, i + 17)[0] ^ 1)
else:
    for i in reversed(chosen):
        del b[i:i + size]
open(path, "wb").write(b)
print(frame)
EDIT_EOF
}

# ---- each copy's replay, played back alone --------------------------------------------------------------
# Only after a match that kept one world: a replay of a desynced match says nothing more.
LIVE_BAD="$bad"
for s in 0 1; do
	if [ -n "$LIVE_BAD" ]; then echo "  the replays are not played back: the match itself failed"; break; fi
	rep="$WORK/user-live$s/Replays/00000000.rep"
	if [ ! -f "$rep" ]; then bad="$bad copy $s wrote no replay;"; continue; fi
	mkdir -p "$WORK/user-back$s/Replays"
	cp -- "$rep" "$WORK/user-back$s/Replays/netcheck$s.rep"
	CORRUPTED=""
	crcmsg="$(grep -a -m1 'CRC message is' "$(log_of 0 live0)" | sed 's/.*CRC message is \([0-9]*\).*/\1/')"
	EXPECT_ALIGN="recorded from frame 0"
	if [ "$LEGACY" -eq 1 ] && [ "$s" -eq 1 ]; then
		stripped="$(edit_replay strip "$WORK/user-back$s/Replays/netcheck$s.rep" "$crcmsg" 0)"
		echo "  copy 1's replay: the CRC records of frame $stripped removed, as a legacy recording lacks them"
		EXPECT_ALIGN="legacy: frame 0 missing"
	fi
	if [ -n "$CORRUPT_AT" ] && [ "$s" -eq 0 ]; then
		CORRUPTED="$(edit_replay corrupt "$WORK/user-back$s/Replays/netcheck$s.rep" "$crcmsg" "$CORRUPT_AT")"
		echo "  copy 0's replay: every recorded CRC at frame $CORRUPTED changed by one bit (the playback check's control)"
	fi
	start_copy "$s" "back$s" -replay "netcheck$s"
	wait_all "$LIVE_TIMEOUT" "$LAST_PID"
	case $? in
		0) ;;
		3) bad="$bad DESCRIPTOR LEAK in the playback of copy $s's replay: $FD_REPORT;";;
		*) bad="$bad the playback of copy $s's replay was STOPPED after $LIVE_TIMEOUT s;";;
	esac
	log="$(log_of "$s" "back$s")"
	read -r crc frame <<< "$(crc_of "$log")"
	oos="$(grep -a -m1 'Replay has gone out of sync' "$log" 2>/dev/null)"
	aligned="$(grep -a -m1 'Replay CRCs: ' "$log" 2>/dev/null | sed 's/.*Replay CRCs: //')"
	echo "  copy $s's replay played back: HEADLESS CRC ${crc:-none} at frame ${frame:-none}; its CRCs: ${aligned:-no alignment logged}${oos:+; it logged: $oos}"
	if [ "${crc:-none}" != "$LIVE_CRC0" ] || [ "${frame:-none}" != "$LIVE_FRAME0" ]; then
		bad="$bad copy $s's replay played back to ${crc:-none} at frame ${frame:-none};"
	fi
	# a replay recorded by this build carries its frame 0 CRC (Recorder.h, replayMayLackFirstCRC), and
	# its every CRC must match
	[ "$aligned" = "$EXPECT_ALIGN" ] || bad="$bad copy $s's replay CRCs were not \"$EXPECT_ALIGN\" (${aligned:-nothing logged});"
	[ -z "$oos" ] || bad="$bad copy $s's replay: ${oos#*Replay has gone }${CORRUPTED:+ (its CRCs at frame $CORRUPTED were corrupted)};"
done

echo
if [ -z "$bad" ]; then
	echo "AGREED: two copies over $HOSTS kept one world to frame $FRAMES ($LIVE_CRC0), and both replays play back to it (this machine only)."
	status=0
else
	echo "FAILED:$bad"
	status=1
fi
if ! verify_install; then
	KEEP=1
	exit 99
fi
exit $status
