#!/usr/bin/env bash
#
# ci-matrix.sh <branch> [options]: the per-merge gate (E2), run from this Mac.  One branch goes to the three
# worker hosts at once - finer (macOS arm64, clang), thinkerer (Linux x86_64, GCC) and the Windows VM (MSVC
# x64) - and each builds it, runs its ctest and E1's replay runs.  One verdict comes back, and the exit status
# is 0 only when every host passed and every host's E1 CRCs agree with each other and with the pins.
#
#   ./ci-matrix.sh feature/mac-port --vm-known-hosts <file> \
#       --expect "0@1200:0x0177BEF6,0@12000:0x5273770F,1@12000:0x830467DB"
#
#   --expect "seed@frames[:0xCRC],..."  the E1 runs, and the CRC each must give.  A run with no CRC is only
#                                     compared across the hosts.  Pins belong to a commit: upstream gameplay
#                                     data moves them.  Default: 0@1200 and 1@12000, unpinned.
#   --hosts finer,thinkerer,windows   a subset (default all three)
#   --vm-known-hosts <file>           the VM's host key (the VM is port 2222 on thinkerer, reached by ProxyJump);
#                                     default $ZH_VM_KNOWN_HOSTS
#   --out <dir>                       where each host's full log goes (default: a new temporary folder)
#
#   ./ci-matrix.sh --verdict <dir>    judge a finished run again from its logs, as the run did at its end
#
# WHERE IT RUNS.  Nothing is built or run on this Mac: it bundles the branch, copies the bundle out, and reads
# back.  finer and thinkerer each check out into ~/zhr-worker/ci (the worktree ci/wt, the build ci/build, the
# logs ci/*.log).  Configure, build, ctest and every E1 run there are ONE zheavy job: the gate queues once behind
# whatever else runs there, and says how long it queued.
# The VM runs windows-ci.ps1 from C:\zhr-worker\wt-pm-win (build, ctest, the desktop GPU tests, E1 on a farm).
# Two gates never share a host: the POSIX hosts hold ~/zhr-worker/ci/lock for the whole run, and the VM waits
# for any other windows-ci.ps1 there to finish.
#
# WHAT A PASS MEANS.  On every host: the build succeeded, ctest passed (without the audio and video tests: no
# sound on the workers), each E1 run played back to the same world, the game data was unchanged afterwards
# (rule 9: replay-check's install guard, windows-ci's listing), and the CRC was the pinned one.  Across hosts:
# the same CRC for each run - Windows, macOS and Linux agreeing is the point of the matrix.  A host that could
# not run a step fails; it is never a pass.  Skipped ctest tests are listed by name under each host, so a test
# that stopped running is seen; they do not fail the gate by themselves (each host skips some by design, see
# docs/mac-port/tasks/E2-ci-matrix.md).
#
# WHAT IT DOES NOT DO: arm anything.  A green matrix says the branch builds and agrees with itself and the pins
# on three platforms.  A fix is still proven by putting its bug back and watching its test fail.
set -u

# ---- one verdict, from the hosts' logs in $OUT: judge <how long it took> --------------------------------------
judge() {
	local fail h log verdict pair run want seen line crc state missing
	echo
	echo "ci-matrix: $REF at $COMMIT, $1"
	fail=0
	for h in $(printf '%s' "$HOSTS" | tr ',' ' '); do
		log="$OUT/$h.log"
		verdict="$(grep -a '^CI-VERDICT' "$log" 2> /dev/null | tail -1)"
		[ -n "$verdict" ] || verdict="CI-VERDICT $h FAIL (it gave no verdict: see $log)"
		echo "== ${verdict#CI-VERDICT }"
		grep -a '^CI ' "$log" 2> /dev/null | sed 's/^CI /   /'
		case "$verdict" in *" PASS") ;; *) fail=1;; esac
	done

	# every host's CRC for each run: the same, and the pinned one where there is a pin
	echo
	echo "== E1 across the hosts"
	for pair in $(printf '%s' "$EXPECT" | tr ',' ' '); do
		run="${pair%%:*}"; want=""; [ "$pair" != "$run" ] && want="${pair#*:}"
		seen=""; line=""; missing=""
		for h in $(printf '%s' "$HOSTS" | tr ',' ' '); do
			crc="$(grep -a "^CI e1 $run " "$OUT/$h.log" 2> /dev/null | tail -1 | awk '{print $4}')"
			case "$crc" in 0x*) ;; *) crc="none"; missing="$missing $h";; esac
			line="$line $h $crc,"
			[ "$crc" = none ] || case " $seen " in *" $crc "*) ;; *) seen="$seen $crc";; esac
		done
		set -- $seen
		if [ -n "$missing" ]; then state="NO CRC FROM${missing}"; fail=1
		elif [ $# -ne 1 ]; then state="THE HOSTS DISAGREE"; fail=1
		elif [ -n "$want" ] && [ "$(printf '%s' "$1" | tr 'a-f' 'A-F')" != "$(printf '%s' "$want" | tr 'a-f' 'A-F')" ]; then state="ALL $1, EXPECTED $want"; fail=1
		elif [ -n "$want" ]; then state="all $1, as pinned"
		else state="all $1 (not pinned)"; fi
		echo "   $run:${line%,}: $state"
	done
	echo
	[ $fail -eq 0 ] && echo "CI-MATRIX PASSED" || echo "CI-MATRIX FAILED"
	return $fail
}

# --verdict <dir>: judge a finished run's logs again (they and its run.env are all it reads)
if [ "${1:-}" = "--verdict" ]; then
	OUT="${2:?--verdict needs the log folder of a run}"
	[ -s "$OUT/run.env" ] || { echo "ci-matrix: no run.env in $OUT" >&2; exit 2; }
	while IFS='=' read -r k v; do
		case "$k" in REF) REF="$v";; COMMIT) COMMIT="$v";; HOSTS) HOSTS="$v";; EXPECT) EXPECT="$v";; esac
	done < "$OUT/run.env"
	judge "judged again from $OUT"
	exit $?
fi

REF="${1:-}"; [ $# -gt 0 ] && shift
EXPECT="0@1200,1@12000"
HOSTS="finer,thinkerer,windows"
VMKH="${ZH_VM_KNOWN_HOSTS:-}"
OUT=""
while [ $# -gt 0 ]; do
	case "$1" in
		--expect) EXPECT="$2"; shift 2;;
		--hosts) HOSTS="$2"; shift 2;;
		--vm-known-hosts) VMKH="$2"; shift 2;;
		--out) OUT="$2"; shift 2;;
		*) echo "ci-matrix: unknown option $1" >&2; exit 2;;
	esac
done
if [ -z "$REF" ] || ! git show-ref -q --verify "refs/heads/$REF"; then
	echo "usage: ci-matrix.sh <local branch> [--expect ...] [--hosts ...] [--vm-known-hosts file] [--out dir]" >&2
	exit 2
fi
for h in $(printf '%s' "$HOSTS" | tr ',' ' '); do
	case "$h" in finer|thinkerer|windows) ;; *) echo "ci-matrix: unknown host $h" >&2; exit 2;; esac
done
case ",$HOSTS," in *,windows,*)
	[ -n "$VMKH" ] && [ -s "$VMKH" ] || { echo "ci-matrix: the windows host needs --vm-known-hosts (or ZH_VM_KNOWN_HOSTS)" >&2; exit 2; };;
esac
if ! printf '%s' "$EXPECT" | grep -q -E '^[0-9]+@[0-9]+(:0x[0-9A-Fa-f]{8})?(,[0-9]+@[0-9]+(:0x[0-9A-Fa-f]{8})?)*$'; then
	echo "ci-matrix: --expect wants seed@frames or seed@frames:0x12345678, comma-separated" >&2; exit 2
fi

COMMIT="$(git rev-parse --short=9 "refs/heads/$REF")"
OUT="${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/ci-matrix.XXXXXX")}"
mkdir -p "$OUT"
printf 'REF=%s\nCOMMIT=%s\nHOSTS=%s\nEXPECT=%s\n' "$REF" "$COMMIT" "$HOSTS" "$EXPECT" > "$OUT/run.env"
NAME="ci-matrix-$(date +%Y%m%d-%H%M%S)-$$.bundle"
git bundle create "$OUT/$NAME" "refs/heads/$REF" > "$OUT/bundle.log" 2>&1 || { echo "ci-matrix: could not bundle $REF" >&2; exit 2; }
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=20 -o ServerAliveInterval=60 -o ServerAliveCountMax=10)
echo "ci-matrix: $REF at $COMMIT on $HOSTS; the logs go to $OUT"

# ---- a POSIX host's part, copied there and run as a file (a script on ssh's stdin would lose its rest to the
#      first command that reads stdin) ----------------------------------------------------------------------
cat > "$OUT/ci-host.sh" <<'HOST'
#!/usr/bin/env bash
# ci-matrix.sh's part on a POSIX worker: <this script> <leg> <branch> <bundle name> <expect> <clone command>
# Each run copies it under a name of its own and it removes itself at the end, so a second gate's copy never
# overwrites a script a first one's bash is still reading.
set -u -o pipefail
LEG="$1"; REF="$2"; NAME="$3"; EXPECT="$4"; CLONE="$5"; PHASE="${6:-light}"; ASKED="${7:-}"
Z="$HOME/zhr-worker"; CI="$Z/ci"
export PATH="$Z/bin:$PATH" ZH_AGENT=ci-matrix
say() { echo "CI $*"; }
verdict() { echo "CI-VERDICT $LEG $*"; rm -f "$0"; exit 0; }
mkdir -p "$CI"
if [ "$PHASE" = light ]; then
# One gate at a time: the lock is held by this shell's descriptor 9 (flock's lock belongs to the open file,
# so it outlives the python that took it), and freed however this script ends.
exec 9> "$CI/lock"
if ! python3 -c 'import fcntl; fcntl.flock(9, fcntl.LOCK_EX | fcntl.LOCK_NB)' 2> /dev/null; then
	echo "waiting for the other ci-matrix run on this host ($CI/lock)"
	python3 -c 'import fcntl; fcntl.flock(9, fcntl.LOCK_EX)'
fi

cd "$Z/repo" || verdict "FAIL no repository at $Z/repo"
git fetch -q -f "$Z/bundles/$NAME" "refs/heads/$REF:refs/ci/head" < /dev/null || verdict "FAIL could not fetch $REF from the bundle"
rm -f "$Z/bundles/$NAME"
if [ ! -d "$CI/wt" ]; then
	git worktree add -q --detach "$CI/wt" refs/ci/head < /dev/null || verdict "FAIL could not make the worktree $CI/wt"
	$CLONE "$Z/vendor/." "$CI/wt/" && $CLONE "$Z"/art/Reforged*.big "$CI/wt/GeneralsMD/Run/" || verdict "FAIL could not copy the vendored sources and the art in"
fi
cd "$CI/wt" || verdict "FAIL no worktree"
git checkout -q -f --detach refs/ci/head < /dev/null || verdict "FAIL could not check out $REF"
say "checked out: $(git log --oneline -1 | cut -c1-100)"
bash GeneralsMD/Code/Tools/vendor.sh > "$CI/vendor.log" 2>&1 < /dev/null || verdict "FAIL vendor.sh (see $CI/vendor.log)"
# Everything heavy - configure, build, ctest, every E1 run - is ONE zheavy job, so the gate queues once.  A
# ticket per step re-entered the back of the queue each time (finer, run 1: 35 of 61 minutes queueing).
# Nothing inside may call zheavy again: a nested call takes a second ticket and waits behind this one's.
# The lock (descriptor 9) stays held through the exec.
exec zheavy bash "$0" "$LEG" "$REF" "$NAME" "$EXPECT" "$CLONE" heavy "$(date +%s)"
fi

say "queued: $(( $(date +%s) - ASKED )) s"
start=$SECONDS

if [ ! -f "$CI/build/CMakeCache.txt" ]; then
	cmake -S "$CI/wt/GeneralsMD/Code" -B "$CI/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DZH_GAME_DATA="$Z/data" \
		> "$CI/configure.log" 2>&1 < /dev/null || verdict "FAIL configure (see $CI/configure.log)"
fi
ninja -C "$CI/build" -k 0 -j "${ZHEAVY_JOBS:-5}" > "$CI/build.log" 2>&1 < /dev/null	# zheavy caps only a bare ninja
if [ $? -ne 0 ]; then
	grep -E 'error:|FAILED:' "$CI/build.log" | head -5 | sed 's/^/CI   /'
	verdict "FAIL the build (see $CI/build.log)"
fi
say "build: ok"

failed=0
ctest --test-dir "$CI/build" -j4 --output-on-failure \
	-E 'test_milesaudiomanager|miles_smoke|test_miles_miniaudio|test_binkvideo' > "$CI/ctest.log" 2>&1 < /dev/null
if [ $? -eq 0 ]; then say "ctest: passed"; else say "ctest: FAILED (see $CI/ctest.log)"; failed=1; fi
grep -E 'tests passed|\*\*\*(Failed|Exception|Timeout|Not Run)' "$CI/ctest.log" | sed 's/^/CI   /'
say "skipped: $(sed -n -E 's/^[[:space:]]*[0-9]+ - (.*) \((Skipped|Disabled)\)$/\1 (\2)/p' "$CI/ctest.log" | paste -sd ',' - | sed 's/,/, /g')"

for pair in $(printf '%s' "$EXPECT" | tr ',' ' '); do
	run="${pair%%:*}"; seed="${run%@*}"; frames="${run#*@}"
	ZH_DATA_DIR="$Z/data" bash "$CI/wt/GeneralsMD/Code/Tools/replay-check.sh" --generals "$CI/build/generals" \
		--seeds "$seed" --maxframes "$frames" > "$CI/e1-$run.log" 2>&1 < /dev/null
	status=$?
	crc="$(sed -n 's/.*players: HEADLESS CRC \(0x[0-9A-Fa-f]*\) at frame.*/\1/p' "$CI/e1-$run.log" | tail -1)"
	case $status in
		0) state="played back the same";;
		77) state="COULD NOT RUN (no game data)"; failed=1;;
		99) state="THE DATA CHANGED OR COULD NOT BE CHECKED"; failed=1;;
		*) state="FAILED (exit $status, see $CI/e1-$run.log)"; failed=1;;
	esac
	say "e1 $run ${crc:-none} $state"
done
say "time: $((SECONDS - start)) s"
[ $failed -eq 0 ] && verdict PASS || verdict FAIL
HOST

# ---- the VM's part: waits for any other windows-ci.ps1, runs it, and says what the POSIX part says -----------
cat > "$OUT/ci-host.ps1" <<'VM'
# ci-matrix.sh's part on the Windows VM: runs windows-ci.ps1 from the PM's worktree and restates its summary
param([string] $Bundle, [string] $Ref, [string] $Runs, [string] $ExpectCrc = "")	# PowerShell 5.1 drops an empty '' argument, so no -ExpectCrc at all means none
$ErrorActionPreference = "Continue"
$root = "C:\zhr-worker\wt-pm-win"; $work = "C:\zhr-worker\ci-pm"
$start = Get-Date
$parent = (Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId
function Get-OtherCi {
	Get-CimInstance Win32_Process | Where-Object {
		$_.ProcessId -ne $PID -and $_.ProcessId -ne $parent -and $_.CommandLine -and
		($_.CommandLine -like '*windows-ci.ps1*' -or $_.CommandLine -like '*ci-matrix-*.ps1*') }
}
if (Get-OtherCi) { Write-Host "waiting for the other windows-ci.ps1 run on this VM" }
while (Get-OtherCi) { Start-Sleep -Seconds 30 }

Set-Location $root
$ciArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ".\windows-ci.ps1", "-Bundle", $Bundle, "-Ref", $Ref,
	"-DataDir", "C:\zhr-worker\data", "-WorkDir", $work, "-Runs", $Runs)
if ($ExpectCrc -ne "") { $ciArgs += @("-ExpectCrc", $ExpectCrc) }
& powershell.exe @ciArgs 2>&1 | ForEach-Object { "$_" } | Tee-Object -Variable lines
$code = $LASTEXITCODE
$lines = @($lines)
Remove-Item $Bundle -ErrorAction SilentlyContinue

# its summary is everything after its last empty line
$from = [array]::LastIndexOf($lines, "")
if ($from -ge 0) { $lines[($from + 1)..($lines.Count - 1)] | Where-Object { $_ -notmatch '^WINDOWS CHECK' } | Select-Object -Last 30 | ForEach-Object { "CI $_" } }
$ctestLog = Join-Path $work "ctest.log"
if (Test-Path $ctestLog) {
	$skipped = Select-String -Path $ctestLog -Pattern '^\s*\d+ - (.*) \((Skipped|Disabled)\)$' | ForEach-Object { "$($_.Matches[0].Groups[1].Value) ($($_.Matches[0].Groups[2].Value))" }
	"CI skipped: " + ($skipped -join ", ")
}
$result = Join-Path $work "desktop-result.json"
if (Test-Path $result) {
	$d = Get-Content -Raw $result | ConvertFrom-Json
	foreach ($run in $Runs.Split(',')) {
		$crc = $d.Crcs."$run"; if (-not $crc) { $crc = "none" }
		"CI e1 $run $crc $($d.E1)"
	}
}
"CI time: $([int]((Get-Date) - $start).TotalSeconds) s"
"CI-VERDICT windows " + $(if ($code -eq 0) { "PASS" } else { "FAIL" })
Remove-Item $PSCommandPath -ErrorAction SilentlyContinue	# each run's copy has a name of its own
VM

posix_leg() {	# posix_leg <leg> <ssh host> <clone command>
	local leg="$1" host="$2" clone="$3"
	scp -q "${SSH_OPTS[@]}" "$OUT/$NAME" "zhr@$host:zhr-worker/bundles/$NAME" \
		&& scp -q "${SSH_OPTS[@]}" "$OUT/ci-host.sh" "zhr@$host:zhr-worker/bundles/${NAME%.bundle}.sh" \
		|| { echo "CI-VERDICT $leg FAIL could not copy the bundle to $host"; return; }
	ssh "${SSH_OPTS[@]}" "zhr@$host" "bash ~/zhr-worker/bundles/${NAME%.bundle}.sh '$leg' '$REF' '$NAME' '$EXPECT' '$clone'" < /dev/null
}

windows_leg() {
	local cfg="$OUT/ssh_config" runs expect
	printf 'Include ~/.ssh/config\nHost ci-matrix-vm\n  HostName 127.0.0.1\n  Port 2222\n  User zhr\n  ProxyJump zhr@thinkerer\n  UserKnownHostsFile %s\n  BatchMode yes\n  ServerAliveInterval 60\n  ServerAliveCountMax 10\n' "$VMKH" > "$cfg"
	scp -q -F "$cfg" "$OUT/$NAME" "ci-matrix-vm:C:/zhr-worker/bundles/$NAME" \
		&& scp -q -F "$cfg" "$OUT/ci-host.ps1" "ci-matrix-vm:C:/zhr-worker/bundles/${NAME%.bundle}.ps1" \
		|| { echo "CI-VERDICT windows FAIL could not copy the bundle to the VM"; return; }
	runs="$(printf '%s' "$EXPECT" | tr ',' '\n' | sed 's/:.*//' | paste -sd ',' -)"
	expect="$(printf '%s' "$EXPECT" | tr ',' '\n' | grep ':' | paste -sd ',' -)"
	ssh -F "$cfg" ci-matrix-vm "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\zhr-worker\\bundles\\${NAME%.bundle}.ps1 -Bundle C:\\zhr-worker\\bundles\\$NAME -Ref '$REF' -Runs '$runs'${expect:+ -ExpectCrc '$expect'}" < /dev/null | tr -d '\r'
}

# ---- fan out ------------------------------------------------------------------------------------------------
started=$SECONDS
pids=()
for h in $(printf '%s' "$HOSTS" | tr ',' ' '); do
	case "$h" in
		finer) posix_leg finer finer.local "cp -c -R" > "$OUT/finer.log" 2>&1 & pids+=($!);;
		thinkerer) posix_leg thinkerer thinkerer "cp -a --reflink=auto" > "$OUT/thinkerer.log" 2>&1 & pids+=($!);;
		windows) windows_leg > "$OUT/windows.log" 2>&1 & pids+=($!);;
	esac
done
wait "${pids[@]}"
rm -f "$OUT/$NAME"

judge "$((SECONDS - started)) s"
exit $?
