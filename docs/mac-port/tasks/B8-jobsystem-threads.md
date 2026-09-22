# B8 — JobSystem thread pool

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** in review
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

## What was done, and what it was verified against

**Status: the rewrite is done and proved; the integration is not, and cannot be yet.**

`JobSystem.cpp` now contains no Windows API at all and needs nothing from `PreRTS.h`. It still
`#include`s it, because every GameEngine `.cpp` does, but the file would compile with that line
removed. The public interface is unchanged and none of the five consumers was touched.

### The two wakeups, which is the whole of the difficulty

Written down before a primitive was picked, as the task asked, and then checked by building the
wrong version on purpose:

- **`s_workReady` was a counting semaphore, and the count is load-bearing.** Its permits are
  *stored*. A worker that has finished the previous fork is not necessarily back at the wait —
  it is somewhere between decrementing `s_workersBusy` and looping round — and the next fork can
  start before it gets there. A permit released into that gap has to still be there when the
  worker arrives. A bare `notify_all` has no memory: it wakes whoever is waiting at that instant
  and is lost on everyone else. So: an integer permit count under the mutex, with the wait keyed
  on a predicate over it.
- **`s_allDone` was an auto-reset event, which also stores its signal.** If the last worker
  finishes before the forking thread reaches the wait, the wait must return at once rather than
  block forever. A predicate wait over `s_workersBusy == 0` has that property for free, because
  the predicate *is* the condition. One waiter, so `notify_one`.

`s_workersBusy` deliberately did **not** become an atomic even though it was an
`InterlockedDecrement`. It is a condition-variable predicate now, and a predicate has to be read
and written under the waiter's mutex regardless; making it atomic as well would not remove that
requirement, only make the lock look optional. `s_nextIndex` is the one genuinely lock-free thing
left, and its relaxed `fetch_add` is the only ordering weaker than the Win32 original.

### Verification

There is no Windows machine, and on macOS the engine does not build yet — `JobSystem.cpp` sits
behind `PreRTS.h`, which B5 has not guarded, and its three dependencies (`Lib/BaseType.h`'s
`__int64`, `EarlyCommandLine.h`'s `<windows.h>`, `FPUControl.h`'s `_controlfp`) belong to B1, B5
and B3. Waiting for all of that before testing a concurrency rewrite would have been the wrong
order, so the **real, unmodified `JobSystem.cpp`** was compiled and run on arm64 macOS against
four shim headers standing in for exactly those dependencies, driven by a harness carrying the
same assertions as the five `JobSystem` `TEST`s in `test_gameengine.cpp`.

| | Result |
|:--|:--|
| AppleClang 21, `-std=c++17 -Wall -Wextra -O2` | compiles clean, no warnings |
| All five existing test bodies | pass |
| 20,000 back-to-back forks on a 31-worker pool, 96 items at granularity 1 | pass |
| ThreadSanitizer, full iteration counts | **clean** |
| AddressSanitizer + UndefinedBehaviorSanitizer | clean |

TSan being clean is worth more than it looks: `test_gameengine.cpp`'s own comment notes there is
no TSan on MSVC/Win32 x86, so this pool has never had a race detector pointed at it in its life.

Two controls, because a green sanitizer run that cannot go red proves nothing:

- **TSan is armed.** The same pool running a job that breaks `JobSystem.h`'s rule 3 — every item
  writing one shared slot — is reported as a data race. So TSan is seeing through the pool's own
  synchronisation rather than being confused by it.
- **The permit count is load-bearing.** A variant of the file with the permits removed and a bare
  `notify_all` in their place — the mistake the header comment warns about — **hangs** the
  back-to-back fork test. Killed at 25s; the correct build finishes the whole harness in 1.7s.
  This is the "put the old behaviour back once and watch the new test go red" rule, and it is the
  single thing that justifies the permit count over the obvious `notify_all`.

To reproduce: the harness is not committed, because its four shim headers would rot the moment
B1, B3 and B5 land and make them unnecessary. It is ~150 lines and consists of the five test
bodies from `test_gameengine.cpp` with `CHECK`/`CHECK_EQ` reimplemented, plus stubs for
`setFPMode`/`getFPMode`/`expectedFPMode` and `findEarlyCommandLineValue`.

### Not covered

- **No throughput measurement.** The task asks for the particle path before and after. That needs
  a running game, so it is deferred to M4 rather than skipped. Nothing in the rewrite adds a lock
  to the hot path — the claim is still one atomic `fetch_add` per chunk, and the mutex is taken
  twice per worker per fork, which is at most 62 acquisitions against thousands of claims.
- **`GameLogic` does not use `JobSystem`.** Verified independently of the B5 survey by grepping
  both `Source/GameLogic` and `Include/GameLogic`: zero hits. The rewrite does not make it any
  easier to put simulation work on a worker — the interface is unchanged, so the only way in is
  still `parallel_for`, and `JobSystem.h`'s rule 2 still says not to.
- **The tests cannot run on macOS yet for reasons outside B8.** `test_gameengine.cpp`'s
  `an_allocation_inside_a_job_is_counted` needs `CriticalSection`, which is `CRITICAL_SECTION`,
  and every FPU test needs `FPUControl.h`. Both belong to other tasks. The JobSystem test helpers
  themselves were ported off `volatile LONG`/`InterlockedExchange`/`GetTickCount`/`Sleep` here,
  since leaving Win32 primitives in the tests for a task that removes them from the code would
  have been half a job.

## Do not

- Do not stub it. The allocator reaches it.
- Do not let any simulation work onto a worker thread while you are here. It does not happen today
  and it must not start now.
