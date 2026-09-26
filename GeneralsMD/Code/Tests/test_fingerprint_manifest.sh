#!/usr/bin/env bash
#
# Tools/fingerprint-manifest.sh checked on itself, in a scratch git repository laid out as the real one
# (a copy of the script at Code/Tools/, which finds its Code folder from its own place):
#   1. the control: the manifest it writes passes its own --check;
#   2. a line written twice fails --check (so build_fingerprint_manifest) with a message naming it;
#   3. during a merge stopped on a conflict, where `git ls-files` names the unmerged path once per
#      stage (shown first, so the refusal guards a real case), both writing and --check refuse with a
#      message naming the path, and the manifest is left as it was.
# What it cannot see: the real tree's manifest (build_fingerprint_manifest checks that one).
# Exit status: 0 all hold, 1 one does not, 77 without git.
set -u
if ! command -v git >/dev/null 2>&1; then
	echo "skip: no git"
	exit 77
fi
SCRIPT="$(cd "$(dirname "$0")/../Tools" && pwd)/fingerprint-manifest.sh"
T="$(mktemp -d "${TMPDIR:-/tmp}/fingerprint-manifest-check.XXXXXX")"
trap 'rm -rf -- "${T:?}"' EXIT
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

mkdir -p "$T/Code/Tools"
cp "$SCRIPT" "$T/Code/Tools/fingerprint-manifest.sh"
g() { git -C "$T" -c user.name=check -c user.email=check@invalid -c init.defaultBranch=main "$@"; }
g init -q
printf 'one\n' > "$T/Code/a.txt"
printf 'two\n' > "$T/Code/b.txt"
g add -A && g commit -q -m base
M="$T/Code/BuildFingerprint.manifest"

# 1. the control
out="$(bash "$T/Code/Tools/fingerprint-manifest.sh" 2>&1)"; status=$?
check '[ $status -eq 0 ] && [ -s "$M" ]' "it writes a manifest ($out)"
out="$(bash "$T/Code/Tools/fingerprint-manifest.sh" --check 2>&1)"; status=$?
check '[ $status -eq 0 ]' "and that manifest passes --check (exit $status: $out)"

# 2. a line twice
cp "$M" "$T/manifest.good"
printf 'a.txt\n' >> "$M"
LC_ALL=C sort -o "$M" "$M"
out="$(bash "$T/Code/Tools/fingerprint-manifest.sh" --check 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "more than once" && printf "%s" "$out" | grep -qx "a.txt"' \
	"a line written twice fails --check and is named (exit $status: $out)"
cp "$T/manifest.good" "$M"

# 3. a merge stopped on a conflict
g add -A && g commit -q -m manifest
g checkout -q -b other
printf 'other\n' > "$T/Code/a.txt"; g commit -q -am other
g checkout -q main
printf 'main\n' > "$T/Code/a.txt"; g commit -q -am main
g merge -q other >/dev/null 2>&1
stages="$(git -C "$T" ls-files | grep -c '^Code/a.txt$')"
check '[ "$stages" -ge 2 ]' "mid-merge, git ls-files names the conflicted path $stages times (the case the refusal guards)"
cp "$M" "$T/manifest.before"
out="$(bash "$T/Code/Tools/fingerprint-manifest.sh" 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "unmerged" && printf "%s" "$out" | grep -qx "a.txt"' \
	"writing refuses during the conflict and names the path (exit $status: $out)"
check 'cmp -s "$M" "$T/manifest.before"' "and leaves the manifest as it was"
out="$(bash "$T/Code/Tools/fingerprint-manifest.sh" --check 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "unmerged"' "--check refuses during the conflict too (exit $status)"

exit $failed
