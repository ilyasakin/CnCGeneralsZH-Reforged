#!/usr/bin/env bash
# test-zheavy.sh <zheavy>: zheavy's queue with dummy jobs, in a private queue folder (never the real one).
# Run on a worker: bash test-zheavy.sh ~/zhr-worker/bin/zheavy (eight groups; "ALL OK" at the end).
set -u
Z="$1"
T="$(mktemp -d "$HOME/zhr-worker/zheavy-test.XXXXXX")"
export ZHEAVY_DIR="$T/q" ZH_AGENT=test
LOG="$T/log"; : > "$LOG"
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }
now() { python3 -c 'import time; print("%.2f" % time.time())'; }
nap() { python3 -c "import time; time.sleep($1)"; }
# job <name> <seconds> [--exclusive]: a dummy heavy job that logs its start and end
job() { local name=$1 secs=$2; shift 2; "$Z" "$@" bash -c "echo start $name \$(python3 -c 'import time; print(\"%.2f\" % time.time())') >> '$LOG'; sleep $secs; echo end $name \$(python3 -c 'import time; print(\"%.2f\" % time.time())') >> '$LOG'" 2>> "$T/err"; }
t() { awk -v k="$1" -v n="$2" '$1==k && $2==n {print $3}' "$LOG"; }
le() { python3 -c "import sys; sys.exit(0 if float('$1') <= float('$2') + 0.05 else 1)"; }
lt() { python3 -c "import sys; sys.exit(0 if float('$1') < float('$2') else 1)"; }

echo "== 1. FIFO and the two classes: A (default), B (exclusive), C and D (default), asked in that order"
job A 4 & nap 0.5; job B 3 --exclusive & nap 0.5; job C 2 & nap 0.5; job D 2 & wait
check 'le "$(t end A)" "$(t start B)"' "B, exclusive, waited for the running default job A"
check 'le "$(t end B)" "$(t start C)" && le "$(t end B)" "$(t start D)"' "C and D, asked after B, waited for it though a default slot was free"
check 'lt "$(t start D)" "$(t end C)"' "C and D ran at the same time (two default slots)"
check 'grep -q "waiting (exclusive), next; running: test, pid" "$T/err"' "B said it was next and what was running"

echo "== 2. two defaults at once, a third waits"
: > "$LOG"
job E 3 & nap 0.3; job F 3 & nap 0.3; job G 1 & wait
check 'lt "$(t start F)" "$(t end E)"' "E and F ran together"
check 'le "$(t end E)" "$(t start G)" || le "$(t end F)" "$(t start G)"' "G started only when one of them ended"

echo "== 3. a crashed holder frees its place"
: > "$LOG"
"$Z" bash -c "echo start H \$(python3 -c 'import time; print(\"%.2f\" % time.time())') >> '$LOG'; exec sleep 60" 2>> "$T/err" & holder=$!
"$Z" bash -c "echo start H2 \$(python3 -c 'import time; print(\"%.2f\" % time.time())') >> '$LOG'; exec sleep 60" 2>> "$T/err" & holder2=$!
nap 1.5
job I 1 & waiter=$!
nap 2
check '[ -z "$(t start I)" ]' "I waited while both slots were held"
killed=$(now); kill -9 $holder; nap 3.5
check '[ -n "$(t start I)" ] && lt "$(t start I)" "$(python3 -c "print($killed + 3)")"' "I started within 3 s of the holder's SIGKILL"
kill -9 $holder2 2>/dev/null; wait 2>/dev/null

echo "== 4. a killed waiter leaves the queue"
: > "$LOG"
job X 3 & nap 0.3
"$Z" --exclusive bash -c "echo start Y >> '$LOG'" 2>> "$T/err" & y=$!
nap 0.5; job W 1 & nap 0.5
kill -9 $y; wait
check '[ -z "$(t start Y)" ]' "Y, killed while waiting, never ran"
check '[ -n "$(t start W)" ] && lt "$(t start W)" "$(t end X)"' "W, behind the dead Y, started while X (default) still ran"

echo "== 5. the -j cap"
mkdir -p "$T/bin"; printf '#!/bin/sh\necho "ninja $*"\n' > "$T/bin/ninja"; chmod +x "$T/bin/ninja"
out1="$(PATH="$T/bin:$PATH" "$Z" ninja -C build -j 10 2>&1)"
out2="$(PATH="$T/bin:$PATH" "$Z" ninja -C build 2>&1)"
out3="$(PATH="$T/bin:$PATH" "$Z" --exclusive ninja -C build -j 10 2>&1)"
out4="$(PATH="$T/bin:$PATH" "$Z" ninja -C build -j4 2>&1)"
check 'printf "%s" "$out1" | grep -q "^ninja -C build -j 5$"' "a default ninja -j 10 runs at -j 5 ($out1)"
check 'printf "%s" "$out2" | grep -q "^ninja -j 5 -C build$"' "a default ninja with no -j is given -j 5 ($out2)"
check 'printf "%s" "$out3" | grep -q "^ninja -C build -j 10$"' "an exclusive ninja keeps its -j 10 ($out3)"
check 'printf "%s" "$out4" | grep -q "^ninja -C build -j 4$"' "a default ninja -j4 keeps -j 4 ($out4)"

echo "== 6. an old zheavy's hold (an exclusive flock on the machine lock) excludes a default job"
: > "$LOG"
oldstart=$(now)
python3 - "$T/.heavy.lock" <<'PY' & old=$!
import fcntl, os, sys, time
fd = os.open(sys.argv[1], os.O_RDWR | os.O_CREAT); fcntl.flock(fd, fcntl.LOCK_EX); os.write(fd, b"old job"); time.sleep(3)
PY
nap 0.5; job V 1; wait $old
check '[ -n "$(t start V)" ] && le "$(python3 -c "print($oldstart + 2.8)")" "$(t start V)"' "V ran only once the old holder let go (it held 3 s)"
check 'grep -q "running: old zheavy: old job" "$T/err"' "and said the old zheavy held the machine"

echo "== 7. ZHEAVY_WAIT gives up with 75"
"$Z" bash -c "sleep 4" 2>/dev/null & a=$!; "$Z" bash -c "sleep 4" 2>/dev/null & b=$!; nap 0.5
ZHEAVY_WAIT=1 "$Z" true 2>/dev/null; st=$?
check '[ $st -eq 75 ]' "a job that waited ZHEAVY_WAIT=1 s gave up with 75 (got $st)"
wait $a $b

echo "== 8. stdin reaches the command"
check '[ "$(echo hello | "$Z" cat 2>/dev/null)" = hello ]' "the command reads the caller's stdin"

rm -rf "$T"
[ $failed -eq 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $failed
