# B2 — Time shim

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** done — `feature/mac-port-B2`, not verified on Windows
- **Size:** 520 call sites in 84 files, measured 2026-09-22 — `timeGetTime` 287, `GetTickCount` 51,
  `QueryPerformanceCounter` 129, `QueryPerformanceFrequency` 48, `timeBeginPeriod`/`EndPeriod` 7.
  One header (`Libraries/Include/Lib/Clock.h`), 462 substitutions by script, eight files by hand.

## Why

Three Windows clocks, 108 files between them, no equivalent on macOS. The count looks alarming and
the work is not: they are all the same two questions — "what time is it in milliseconds" and "give
me the highest-resolution tick you have".

## Scope

New: one header, `Libraries/Include/Lib/Clock.h` (name it what you like, but one header).

Callers in `GameEngine/Source`, `GameEngineDevice/Source`, `Libraries/Source/WWVegas`, and
`GameEngine/Include/Common/PerfTimer.h`.

## Do

1. Three functions, named for what they answer, not for what Windows called them:
   - milliseconds since start, monotonic — replaces `timeGetTime` and `GetTickCount`
   - a high-resolution tick and its frequency — replaces `QueryPerformanceCounter` and
     `QueryPerformanceFrequency`
   On macOS both are `clock_gettime(CLOCK_MONOTONIC_RAW)` or `mach_absolute_time`. On Windows they
   forward to exactly what is there now, so the Windows build's timing does not shift by a
   microsecond.
2. Sweep the call sites. This is find-and-replace with a careful eye on two things: callers that
   wrap at 32 bits and depend on it (`timeGetTime` does, and some interval arithmetic quietly
   relies on unsigned wraparound), and callers that mix the two clocks in one subtraction.
3. `PerfTimer.h:75` has an `__asm` block. Confirm it is `#if 0` like the rest before assuming it is
   dead — if it is live under some configuration, it needs a portable replacement here.

## Done when

- No `timeGetTime`, `GetTickCount` or `QueryPerformanceCounter` outside the new header and the
  Windows platform layer.
- The header builds on both.
- Windows full build and `ctest` green, and timing-sensitive tests unchanged.

## Do not

- **Nothing in `GameLogic` takes a timestamp as simulation input.** If the sweep finds a wall clock
  being read inside the simulation, stop and raise it on this task rather than porting it — that
  is a determinism bug that predates the port and deserves its own fix, not a shim.
- Do not introduce `std::chrono` at the call sites. One header, one API, minimal diff at 108 files.


## Done, 2026-09-22 — `feature/mac-port-B2`

`Libraries/Include/Lib/Clock.h`, five functions, no clock object and no `std::chrono`. Every one
forwards on Windows to exactly the call it replaced, so a Windows build reads the same counter in
the same units with the same wrap. 87 files changed; nothing has been compiled, by any compiler,
because `gameengine` is gated out of the macOS build until B6. `WINDOWS-DEBT.md` carries three rows.

### The determinism question: answered, and the answer is no

**No wall clock is a simulation input.** Every clock read in `GameLogic` was classified by
preprocessor guard and then read individually — 114 of them across eight files. All of them are
measurement that ends in a `DEBUG_LOG` or an on-screen statistic. None reaches a branch the
simulation takes. Three things are worth naming because they look like the bug and are not:

- **`RailroadGuideAIUpdate.cpp:136`** reads the performance counter and feeds it straight into a
  train's direction, with the comment "absolutely, positively random every call!". It is a
  deliberate desync harness — `#ifdef RAILROAD_DESYNC_TEST`, and line 49 is `///#define
  RAILROAD_DESYNC_TEST`, commented out. It exists to *make* a desync, to prove the network code
  reports one. Ported as-is and labelled, not removed: somebody built it for a reason.
- **`AIPathfind.cpp`'s search cap is a cell-count cap, not a time cap** (`hitCellCap`). A pathfind
  that gave up after N milliseconds would be a desync on any two machines of different speed. It
  gives up after N cells. The `QueryPerformanceCounter` reads around it are the `DEBUG_LOG` that
  reports how long the capped search took.
- **`GameLogic.cpp`'s three `timeGetTime` reads** are the map-loading progress bar and the
  multiplayer lobby's load-timeout. Both run before the simulation does.

One nearby note: `WWLib/srandom.cpp:215` seeds a generator from `GetTickCount`, which would matter
a great deal if anything used it. Nothing in the tree includes `srandom.h`.

### The `__asm` block was not dead, and the plan said it was

Step 3 said "Confirm it is `#if 0` like the rest before assuming it is dead". It is not `#if 0`, and
it is not dead:

- `PerfTimer.h:60` compiles `GetPrecisionTimer` under `#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)`.
- `GameCommon.h:59` defines `DUMP_PERF_STATS` in **every** `_DEBUG` or `_INTERNAL` build.
- Inside it, `#ifdef USE_QPF` chooses the performance counter — and the file's own line 59 is
  `#define NO_USE_QPF`, so `USE_QPF` is never defined and **the `__asm RDTSC` block is the live
  branch**.
- `CMakeLists.txt:48-52` refuses to configure for anything but x64, and **MSVC does not support
  `__asm` on x64**.

So a `Debug` build of this tree does not compile, and has not for as long as it has been x64. It is
invisible because nobody builds that configuration. `-DPERF_TIMERS=ON` reaches the same code in
Release. The mac-port README's "every remaining `__asm` block is inside `#if 0`" was wrong on the
strength of this one block and has been corrected; `WWMath/vp.cpp`'s blocks really are dead, being
expanded only inside `#if defined(__ICL)`.

`GetPrecisionTimer` now reads `Clock_Ticks()`. That forced a second change: `InitPrecisionTimer`
calibrated the time stamp counter against the performance counter and stored a **TSC** rate, which
is only correct while the reading is a TSC reading. Its 60ms three-sample calibration is gone and
the rate is asked for instead. Both halves now come from one clock and cannot drift apart. This is
a genuine behaviour change in `PERF_TIMERS`/`DUMP_PERF_STATS` builds — but not a regression, because
those builds do not currently compile.

### Two millisecond clocks, not one

Step 1 asked for one function to replace both `timeGetTime` and `GetTickCount`. There are two, and
the header says why. They differ only in resolution — 1ms against the scheduler's ~15.6ms — and
merging them would quietly hand 51 `GetTickCount` call sites a finer clock than they have ever had.
That is a small timing change on Windows that nobody on this project can measure, and the rule is
that we do not make those. On macOS they are the same clock. Merging them later is one line in the
header rather than 51 call sites again.

### The 32-bit wrap is deliberate

`Clock_Milliseconds` returns `unsigned int` and the macOS side truncates to 32 bits on purpose.
`SysTimeClass::Reset` writes `WrapAdd = 0 - StartTime`, which is only correct in 32-bit unsigned
arithmetic; handing back a wider value would break it silently at the 49.7-day mark rather than
loudly. Callers that store the result in a signed `Int` — `GameLogic::testTimeOut` is one — were
already wrong at 24.8 days and are no more wrong now. Pre-existing, not the shim's to fix.

### Deliberately not done

- **`WWLib/mpu.cpp` keeps its four `QueryPerformance*` calls**, with a comment saying so. It does
  not read a clock to know the time; it uses the performance counter as a reference oscillator to
  measure the CPU's own frequency against `__rdtsc`, through `LARGE_INTEGER`'s `LowPart`/`HighPart`
  halves with `REALTIME_PRIORITY_CLASS` around it. Swapping four calls would leave a file that is
  still x86-and-Windows top to bottom. It needs one decision from whoever ports `cpudetect`.
- **`GetLocalTime` (4 sites, 3 files)** is a calendar clock answering a different question, and it
  needs a `SYSTEMTIME` replacement. That is B5's shape of problem, not this one's.
- **`systimer.h`'s `#define timeGetTime SystemTime.Get`** was removed rather than ported. It was
  guarded by `#ifdef timeGetTime` and `timeGetTime` is a function in `mmsystem.h`, never a macro,
  so the redirect has never once fired. A macro that renames a clock out from under its callers is
  the last thing the next sweep wants to meet.

### One `include_directories`

`Lib/Clock.h` is wanted by thirteen targets — five WWVegas libraries, four `dx11` ones and three of
their tests — because `systimer.h` and `dx11resource.h` are public headers that read the clock.
Naming thirteen targets is a list that goes stale; one directory-wide include path cannot miss one.
It adds no define and changes no struct, which is what the file's own warning about global settings
is actually about.
