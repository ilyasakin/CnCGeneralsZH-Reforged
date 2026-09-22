# B8 — JobSystem thread pool

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** in progress
- **Size:** `GameEngine/Source/Common/System/JobSystem.cpp` (8.5 KB) and
  `GameEngine/Include/Common/JobSystem.h` (5.2 KB)

## Why

Created 2026-09-22 from B5 recon's finding, and it is the gap that mattered most: **no task owned
this and the engine cannot run on macOS without it.**

`JobSystem.cpp` is a hand-written Win32 thread pool — `CreateThread`, `WaitForSingleObject`,
`CloseHandle`, `InterlockedExchangeAdd`, `InterlockedDecrement`, and `volatile LONG` counters
standing in for atomics. It is compiled into `gameengine` (the source list is a glob and this file
is not excluded), and its consumers are `GameEngine.cpp`, `GameMemory.cpp`, `ParticleSys.cpp`,
`W3DParticleSys.cpp` and `W3DDisplay.cpp`.

`GameMemory.cpp` is the significant one. The allocator touching the job system means this is not a
leaf subsystem that can be stubbed and revisited.

It is also not a type leak, which is why B5 could not absorb it. `DWORD` becoming `uint32_t` is a
substitution; this is a rewrite.

**`GameLogic` does not use it**, which is the one piece of good news: no simulation code runs on a
worker, so this is not a determinism surface. Verify that is still true before you rely on it.

## Scope

- `GameEngine/Source/Common/System/JobSystem.cpp`, `GameEngine/Include/Common/JobSystem.h`
- The five consumers, only if the interface has to change — prefer that it does not

## Do

1. Rewrite the internals against `std::thread`, `std::atomic`, `std::mutex` and
   `std::condition_variable`. The tree currently has **zero** uses of any of them, so you are
   setting the precedent — keep it plain and readable.
2. **Keep the public interface in `JobSystem.h` unchanged.** Five call sites depend on it and none
   of them should need editing. If the interface genuinely cannot survive, say so before changing
   it rather than after.
3. `volatile LONG` is not an atomic and never was; the Interlocked calls are what made it work.
   Map them honestly onto `std::atomic<int32_t>` with explicit memory ordering, and do not carry
   `volatile` across — it means something different in C++ and carrying it hides the intent.
4. `s_workReady` (released once per worker per fork) and `s_allDone` (auto-reset, set by the last
   worker to finish) are Win32 event semantics. Auto-reset events and condition variables are not
   the same thing; a naive `notify_all` will either wake too many or lose a wakeup. Write down
   which semantics each one needs before you pick a primitive.
5. Worker count and the `s_grain` work-stealing granularity are tuned numbers. Keep them.

## Done when

The job system builds and runs on macOS, a test exercises fork/join with contention and proves
every job runs exactly once, and the five consumers are unmodified. On Windows the same rewrite
compiles — it is standard C++ — but nobody here can run it, so a `WINDOWS-DEBT.md` row records
that the Windows threading path changed from Win32 primitives to the standard library, which is
the single largest untested behaviour change in M1.

Measure throughput on the particle path before and after if you can; a thread pool that is correct
and half the speed is a regression nobody will notice until M4.

## Do not

- Do not stub it. The allocator reaches it.
- Do not let any simulation work onto a worker thread while you are here. It does not happen today
  and it must not start now.
