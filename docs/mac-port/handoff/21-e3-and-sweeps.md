# Handoff — agent 21: E3, the vendored sweeps, and three fixes

Written 2026-09-22 at wind-down. Factual; for a stranger.

## 1. State

| Thread | Branch | State |
|:--|:--|:--|
| A2 POSIX vendor script + A3 `build.sh` | `feature/mac-port-A2` | **merged** |
| B9 backslash includes (147 directives, 15 files) | `feature/mac-port-B9` | **merged** |
| zlib `zutil.h` Classic Mac branch | `feature/mac-port-zutil` | **merged** (`0a2dba01`) |
| persistfactory pointer token | `feature/mac-port-persist` | **merged** |
| E3 differential harness | `feature/mac-port-E3` | **half merged — see below** |
| GameSpy sweep | `feature/mac-port-gssweep` | **complete, committed, NOT merged** |

**E3 is half merged and this matters.** The harness itself (`92ca63c7`) is on `feature/mac-port`
and runs green. The second commit, **`2812bf7b` "say what the differential harness cannot see"**, is
**not merged**. That commit is the section documenting the harness's blind spots. Without it the
harness reads as though a green run means more than it does. Merging it is a documentation-only
change to `run_arch_diff.sh`. Do it.

**The GameSpy sweep completed. It was not interrupted.** 564 `.c`/`.h`/`.cpp` files, all read,
control pattern verified non-zero. Result recorded in `docs/mac-port/VENDORED-PREDICATE-SWEEP.md`
§5, integrated into agent 8d's existing document rather than a competing one.

**Result, stated as a result:** GameSpy has **no defect reaching this port**. That is not "no hits"
— 44 hits, each read and accounted for. An unswept library and a swept-clean library look identical
in a repository and are not the same thing.

## 2. Uncommitted

Nothing. Both open worktrees (`../zhr-gssweep`, `../zhr-E3`) are clean. The vendored sources and
`build-mac/` trees in each worktree are gitignored build state, not work.

## 3. Next step, per thread

- **E3:** merge `2812bf7b`. Nothing else pending.
- **GameSpy sweep:** merge `feature/mac-port-gssweep`. One commit, documentation only.
- **persistfactory:** the committed test `persist_factory_writes_a_pointer_token_load_can_read`
  has **never been executed by ctest** — `test_wwsaveload` is `PENDING_MACOS` and blocked on
  `wwstring.h`'s `WCHAR` (agent 95). Run it the day that target links. It was verified by a
  standalone harness instead, which is weaker.
- **zlib / vendor.sh:** nothing pending.
- **Case sensitivity:** 16 more sites found in the GameSpy sweep, on top of the 7 C1 already holds.
  Nobody owns fixing them. They break on any case-sensitive filesystem, which is any Linux runner.

## 4. What I know that is not written down

### 4a. Operating E3 — the only tool here that can answer "does this agree with x86"

Rosetta 2 runs x86_64 binaries on Apple Silicon and clang cross-compiles to x86_64, so one Mac
builds and runs the same source on both architectures. This recovers the *architecture* half of the
determinism question without a Windows machine.

**Run it:**
```console
ctest --test-dir build-mac -R arch_differential --output-on-failure   # ~1-2 s
GeneralsMD/Code/Tests/arch_diff/run_arch_diff.sh                      # or directly
```
It **skips** (exit 77, ctest reports `***Skipped`) if clang cannot target either architecture or if
an x86_64 binary will not run. The Rosetta check is a real execution, not a version string.

**Add a probe** — edit `arch_probe.cpp`, add a function that prints `section<TAB>key<TAB>value`, and
call it from `main`. Rules that are not obvious:
- **Print floats as bit patterns, never decimal.** `row_f`/`row_d` do this. A decimal rendering
  loses exactly the last-bit differences the harness exists to find.
- **Put no expected values in the probe.** The two architectures are each other's reference. An
  expected value would be a third opinion and defeats the design.
- Guard anything that may not compile yet with `#if __has_include(...)`. `DetRound.h` is wired this
  way and activated itself when B3 landed, with no edit.

**`known_differences.txt` is a contract, not a suppression list.** An unrecorded difference fails
the run **and so does a recorded difference that has stopped differing** — a stale row is worse than
none, because it makes the next reader trust a line that is no longer true. Each row carries both
values and a note. A row records *that* the architectures differ, never which is right; that
question is "what does the Windows build do" and this machine cannot answer it.

**The shims.** `run_arch_diff.sh` passes `-D__cdecl=` and `-D__int64="long long"` so it can compile
`dettrig.cpp` today. Both are B5's to remove. When B5 lands, delete the two `-D` flags; nothing else
changes.

**Named blind spots — the most important part.**
- **It is clang on both sides.** It says nothing about MSVC's codegen, its optimiser, its `/fp:`
  semantics, or its 32-bit `long`. A green run here is **not** a green run against Windows.
- **The x86_64 column is not a Windows answer.** Both targets have a 64-bit `long`; Windows has 32.
  It coincides for the float-to-int conversions only because `cvtsd2si` is a 32-bit conversion
  regardless of what it is assigned to. Do not generalise it.
- **It cannot see anything the two targets share.** Both define `__APPLE__` and both are
  little-endian, so a bug keyed on either takes the same branch twice, they agree, and the harness
  reports green. B3's `gimex.h` bug — `#if defined(__APPLE__)` taking a PowerPC big-endian path —
  is exactly this, and adding a probe for it would not have caught it. **A differential harness can
  only see the axis it varies.** Bugs keyed on OS or endianness need a selfcheck that asserts the
  intended value, not a comparison against a twin that shares the assumption.

**What it has found:** 12 rows where arm64 and x86 genuinely differ (all float-to-int at NaN,
infinity and out-of-range), which located two real defects in `DetRound.h` that a 400,000-value
sweep had missed. And 170 rows identical, including every `DetTrig` output and an 80,000-value sweep
fingerprint, and every float-expression shape — which is `-ffp-contract=off` holding, measured.

### 4b. My own detection bug, because the shape recurs

Writing the `zutil.h` patch I detected "needs patching" by greping for the **exact pristine line**.
My own test case — respelling the predicate without spaces — made the patch silently do nothing and
report success. That is the same quiet failure the patch existed to prevent, one level up.

**The fix is to match on meaning rather than spelling:** detect any `#if` that names
`TARGET_OS_MAC` and does not already exclude `__APPLE__`, rewrite it, then **verify the rewrite took
and fail loudly if it did not**. Both patch functions in `vendor.sh` are built that way now. If you
add a third, copy the shape: loose detection, strict transform, verification that exits non-zero.

This was the third time in one day that someone's method answered a different question from the one
asked. It is worth assuming yours does too until a control says otherwise.

### 4c. The automation split — both halves, in one place

The vendored-predicate rule has an automatable half and a human half, and whoever tries to CI it
needs both facts together:

- **The predicate half can be a check.** `__APPLE__`, `TARGET_OS_`, `MACOS`, `__BIG_ENDIAN`,
  `POWERPC`, plus `__i386__` and `_M_IX86` which agent 8d added after finding an instance written
  with `__i386__`. Across zlib, LZH-Light, EAC, FFmpeg and GameSpy this stays in single figures —
  quiet enough to run on every build.
- **The width half cannot.** `unsigned long` is the pattern that found the LZH-Light defect, and
  GameSpy alone would bury it. It stays a human read.

**A correction that must travel with those numbers.** The rule was published claiming the predicate
patterns return "five hits total across zlib, LZH-Light, EAC and **all 564 GameSpy files**".
GameSpy contributed **zero** of those five — not one of its files was opened. That number was mine
and it was wrong. The real GameSpy figures are 7 on the automatable list and 44 on the wider list.

**Why it was wrong, and this bites everyone:** `grep` on this machine is **ugrep**, which honours
`.gitignore`. Every vendored directory carries a committed `.gitignore` containing `*`. So
`grep -rn <pattern> Libraries/Source/GameSpy` reads **nothing** and looks exactly like a clean
sweep, even after vendoring:

```console
$ grep -rn socket Libraries/Source/GameSpy | wc -l                            0
$ /usr/bin/grep -rn socket --include='*.c' Libraries/Source/GameSpy | wc -l  854
```

Agent 8d found the same root cause from a different angle (root-level rules hiding 466 *tracked*
files). Three distinct ways the check passes on nothing are now in
`VENDORED-PREDICATE-SWEEP.md` §3. **Any sweep needs a control pattern that must match, checked
non-zero before a null result is believed.**

### 4d. Reachability is worth more than a fix, twice over

Two findings changed shape entirely once reachability was measured, and both times the measurement
was cheap:
- **persistfactory:** the plan called it a probabilistic low-32-bit collision. It is neither
  probabilistic nor a collision — `ChunkLoadClass::Read` refuses a short read and returns 0 without
  touching the buffer, so on x64 *every* object registered under the key NULL. But the game never
  calls `SaveLoadSystemClass` at all; its save system is `SaveGame/GameState.cpp` on `Xfer`. Certain
  and total, in code that never runs.
- **GameSpy:** `add_subdirectory(GameSpy)` is inside `if(ZH_PLATFORM_WINDOWS)`, so no GameSpy `.c`
  compiles on macOS. Only its headers reach `gameengine`. That single fact bounds every predicate
  finding in the SDK.

## 5. Open questions, with names

| Question | Who |
|:--|:--|
| Merge `2812bf7b` (E3 blind spots) and `feature/mac-port-gssweep`. Both documentation-only. | PM |
| **The GameSpy human half over the SDK's 726 files.** §5 of the sweep document covers the 564 `.c`/`.h`/`.cpp` files for *platform predicates* and reads all 44 hits. It does **not** cover the SDK's non-predicate portability — struct packing, protocol widths, alignment. Unclaimed and unstarted. | unassigned |
| 16 case-sensitivity include sites from GameSpy, plus C1's 7. Break on any case-sensitive filesystem. Nobody owns the fix. | C1 / E2 |
| `GSI_MAX_INTEGRAL_BITS` is 32 on clang. The obvious fix exposes `0xffffffffffffffffui64`, an MSVC-only suffix clang rejects, and the error lands at the first *use*, not the definition. Documented; do not repair one without the other. | whoever first builds GameSpy on macOS |
| `persist_factory_writes_a_pointer_token_load_can_read` has never run under ctest. | whoever un-pends `test_wwsaveload` |
| E1 remains open. E3 covers the architecture half only; the compiler half is untouched. | E1 / E2 |
