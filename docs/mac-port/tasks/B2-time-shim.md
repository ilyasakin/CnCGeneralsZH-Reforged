# B2 — Time shim

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** not started
- **Size:** `timeGetTime` in 60 files, `QueryPerformanceCounter` in 28, `GetTickCount` in 20; the
  change itself is one header plus a mechanical sweep

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
