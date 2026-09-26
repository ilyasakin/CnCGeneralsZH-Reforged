#!/usr/bin/env bash
#
# L1's POSIX harness (Tools/net-check.sh) checked on itself, with the game's data.
#   0. On macOS, the firewall gate's control: socketfilterfw fakes reporting the firewall, stealth mode
#      or block-all on each make the harness skip (77) before any copy or the probe listens.
#   1. Two headless copies on this machine play seed 3 on Golden Oasis with two AIs to frame 1800 over
#      127.0.0.1 and a second local address: neither logs a CRC mismatch, both stop on one CRC, the AI
#      built something, and each copy's replay plays back to that CRC.
#   2. The armed control: the same with the second copy on the next seed.  The harness must report the
#      match FAILED with the game's own CRC mismatch, and exit 1 - so a pass in 1 is a comparison that
#      can fail.
#
# WHAT THIS PROVES: two processes of this build keep one world over the real network code on ONE
# machine.  Nothing about a Windows peer, a real network, or the LAN lobby: see the harness's header.
#
# Skips (77) as the harness does: no ZH_DATA_DIR, no second local address, a game port in use, the
# macOS application firewall on (it would ask in a dialog), no python3.
# Usage: run_net_check.sh <generals>
set -u
GENERALS="$1"
HARNESS="$(cd "$(dirname "$0")/../Tools" && pwd)/net-check.sh"
if [ -z "${ZH_DATA_DIR:-}" ]; then
	echo "skip: no game data (ZH_DATA_DIR)"
	exit 77
fi
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

# 0. The firewall gate's control (macOS): fakes of socketfilterfw reporting the firewall on, stealth mode on
#    and block-all on must each make the harness skip before anything listens - no copy, no probe.
if [ "$(uname -s)" = "Darwin" ]; then
	FAKE="$(mktemp -d "${TMPDIR:-/tmp}/net-check-fw.XXXXXX")"
	fake() {	# fake <name> <global> <stealth> <blockall>
		printf '#!/bin/sh\ncase "$1" in --getglobalstate) echo "%s";; --getstealthmode) echo "%s";; --getblockall) echo "%s";; esac\n' \
			"$2" "$3" "$4" > "$FAKE/$1"; chmod +x "$FAKE/$1"
	}
	fake on "Firewall is enabled. (State = 1)" "Firewall stealth mode is off" "Firewall has block all state set to disabled."
	fake stealth "Firewall is disabled. (State = 0)" "Firewall stealth mode is on" "Firewall has block all state set to disabled."
	fake blockall "Firewall is disabled. (State = 0)" "Firewall stealth mode is off" "Firewall has block all state set to enabled."
	for f in on stealth blockall; do
		out="$(NET_CHECK_FIREWALL_TOOL="$FAKE/$f" bash "$HARNESS" --generals "$GENERALS" --frames 30 2>&1)"
		status=$?
		check '[ $status -eq 77 ] && printf "%s" "$out" | grep -q "^skip: .*firewall" && ! printf "%s" "$out" | grep -q "^net-check:"' \
			"a firewall reported $f makes the harness skip before any copy starts (exit $status: $out)"
	done
	rm -rf -- "${FAKE:?}"
fi

out="$(bash "$HARNESS" --generals "$GENERALS" --frames 1800 2>&1)"
status=$?
printf '%s\n' "$out"
if [ $status -eq 77 ]; then
	exit 77		# the harness said why
fi
check '[ $status -eq 0 ]' "two copies agree over the network to frame 1800 (exit $status)"
check 'printf "%s" "$out" | grep -q "^AGREED: .* to frame 1800"' "and say so"

out="$(NET_CHECK_LIVE_TIMEOUT=300 bash "$HARNESS" --generals "$GENERALS" --frames 1800 --control 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 1 ]' "the control (the second copy on another seed) fails the harness (exit $status)"
check 'printf "%s" "$out" | grep -q "logged [1-9][0-9]* CRC mismatches"' "through the game's own CRC mismatch"

exit $failed
