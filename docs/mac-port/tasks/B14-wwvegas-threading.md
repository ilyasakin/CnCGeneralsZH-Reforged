# B14 — WWVegas' own threading primitives

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** done — `feature/mac-port-B14`, not verified on Windows
- **Size:** three critical-section implementations, four copies of one spinlock, two `_beginthread`
  sites

## Why

Created 2026-09-22 from the threading-primitive sweep
(`docs/mac-port/THREADING-PRIMITIVE-SWEEP.md`). B8 and B11 dealt with `GameEngine`'s threading.
**WWVegas has its own, separately, and none of it is owned.** The sweep's conclusion is that this
is a bigger surface than GameEngine's was.

- `WWLib/critsection.{h,cpp}` — `InitializeCriticalSection` directly
- `WWLib/mutex.cpp` — `CriticalSectionClass` and `MutexClass`
- `WWLib/mutex.h` — `FastCriticalSectionClass`, an `<intrin.h>` spinlock
- `WWLib/thread.cpp`, `WWAudio/Threads.cpp` — `_beginthread`
- `WWLib/wwmouse.h:235-236` — an `Interlocked` pair

**The `_interlockedbittestandset` spinlock exists FOUR times, not two.** B3 guarded
`WWLib/mutex.h:135` and `WWDebug/wwmemlog.cpp:317`. The sweep found
`Libraries/Source/profile/internal.h:54` running the identical
`while (_interlockedbittestandset((volatile long *)&nFlag, 0))` loop, unreached. Do all four copies
in one commit rather than three of them.

## Do

1. `std::atomic_flag::test_and_set` for the spinlock, all four copies together.
2. `std::recursive_mutex` or `std::mutex` for the critical sections — **and settle the recursion
   question per implementation as B11 did, by building it wrong and watching it deadlock, not by
   reading.** B11 found `UnicodeString::set` re-entering on the ordinary path; do not assume
   WWVegas is different in either direction.
3. `std::thread` for `_beginthread`, following B8's shape.
4. Keep every interface unchanged. Three tasks have now established that pattern here.
5. Verify as B8 and B11 did: real source, TSan, **and a control proving TSan can go red**.

## Done when

WWVegas compiles without `<intrin.h>` or `windows.h` for threading reasons, TSan is clean and
demonstrated armed, and no interface moved.

## Do not

- Do not unify the three critical-section implementations into one while you are here, however
  tempting. They have different interfaces and different callers; merging them is a refactor with
  its own risk, and this task is already broad.
- Do not touch `Tools/`. The sweep found 79 further sites there, mostly a fourth and fifth copy of
  the same wrapper, and `Tools/` is out of the plan's scope.


## Done, 2026-09-22 — `feature/mac-port-B14`

Eleven files. No interface moved: every public class, method and member that anything outside
WWLib names is what it was, and no consumer was edited.

### What ThreadSanitizer found, which is the part that matters

`ThreadClass::running` was a **`volatile bool`**, written by `Stop()` on one thread and read by
every derived `Thread_Function` on another. TSan called it a data race on the first armed run of
the real source, and it is one: `volatile` orders nothing and guarantees no atomicity.

It worked on Windows by an accident of the compiler. MSVC's default `/volatile:ms` gives every
volatile access acquire/release semantics, so that one write and those eight reads were correctly
ordered there and nowhere else. Under clang on arm64 they are not, and the failure mode is the one
that costs: a worker that never observes `running` go false runs until `Stop()`'s timeout, every
shutdown, on a thread that has just been told to quit.

It is a `std::atomic<bool>` now — a drop-in at all ten call sites. **This is a bug fix, not a
port artifact**, and it was invisible until something was watching.

### The recursion questions, settled by building it wrong

| Lock | Was | Is | Recursive? | How that was established |
|:--|:--|:--|:--|:--|
| `FastCriticalSectionClass` | `_interlockedbittestandset` spin | `std::atomic_flag` | **no**, and no decision needed | The class comment always said "can't be locked multiple times from the same thread", and the old spin enforced it by spinning on a flag only the holder could clear. `atomic_flag` does exactly the same. Re-entry deadlocked before and deadlocks now, identically. |
| `CriticalSectionClass` (mutex.h) | `CRITICAL_SECTION` | `std::recursive_mutex` | **yes** | Built against a `std::mutex` and run: deadlocks on the second acquire, first time, every time. |
| `MutexClass` | Win32 mutex | `std::recursive_timed_mutex` | **yes** | Same control against `std::timed_mutex`: deadlocks on the second acquire. |
| `CriticalSectionClass` (critsection.h) | `CRITICAL_SECTION` | `std::mutex` | **no**, deliberately | `Enter()` asserted `inside==false`, i.e. the class forbids it. See the defect note below. |

No live consumer of `CriticalSectionClass` or `MutexClass` actually re-enters — all nine
`CriticalSectionClass` sites (`ffactory.cpp` ×5, `lzo.cpp` ×2, `saveloadstatus.cpp` ×2) and all
nineteen `MutexClass` sites (the GameSpy threads) were read and every one is a leaf. They are
recursive anyway, because `CRITICAL_SECTION` and the Win32 mutex both are, and because these are
public classes in a library whose other consumers — WWAudio, and `Tools/` — are outside this
task's reach. A `std::mutex` would be faster by an owner check that `CRITICAL_SECTION` was already
paying, and would turn any re-entry a future consumer introduces from a visible deadlock into
undefined behaviour.

**Lock ordering, recorded because nothing else records it:** `SimpleFileFactoryClass::Mutex`
(a `CriticalSectionClass`) is held while `StringClass` operations take `StringClass::m_Mutex`
(a `FastCriticalSectionClass`). One direction only. Nothing takes them the other way; keep it
that way.

### Three defects found on the way, all pre-existing

1. **`critsection.h`'s assert was backwards.** `Enter()` read the plain `inside` bool *before*
   acquiring, so a second thread arriving while the first held the lock failed the assert — which
   is ordinary contention, and the whole point of the class. It is taken after the acquire now,
   where it catches what it was written to catch.
2. **Every `#ifdef _UNIX` arm in `mutex.cpp` was a hole, not a port.** Each one returned success
   having taken no lock: `MutexClass::Lock` answered true, `Unlock` did nothing, the constructors
   built nothing, and a commented-out `assert(0)` sat where the implementation should have been.
   Anything built with `_UNIX` defined ran all of WWVegas' locking as no-ops and would have looked
   fine until two threads met. `thread.cpp` had the same shape, including a
   `_Get_Current_Thread_ID` that returned 0 for every thread — which makes every main-thread
   assert in `TextureLoader` and `DX8_THREAD_ASSERT` pass from any thread. All gone: one
   implementation, every platform.
3. **`Execute()` set the running flag on the wrong side of the thread start.** `_beginthread`'s
   return value landed in `handle` *after* the new thread was already running, so a thread that
   finished immediately zeroed the handle and then had it overwritten with a stale value —
   `Is_Running()` answered true forever. Set before the thread starts now.

### What is lost, and it is not nothing

- **`TerminateThread`.** `Stop(ms)` killed the thread when its timeout expired. There is no
  portable equivalent and no safe one — a killed thread never releases what it holds. `Stop()` now
  gives up and returns, leaving the thread running, and `~ThreadClass` joins it. The existing
  regression test (`threadclass_stop_deadlocks_if_the_caller_holds_the_workers_lock`) asks only
  that `Stop` runs out its timeout and returns, which it still does — measured at 301ms for
  `Stop(300)`. What changes is that a thread which never returns now hangs the shutdown instead of
  corrupting it.
- **Thread priority off Windows.** `SetThreadPriority` has no standard equivalent, and
  `pthread_setschedparam` needs a policy the process usually may not ask for. The value is still
  stored, so no caller loses its intent; macOS would want `pthread_set_qos_class_self_np`, which is
  a different model. Two callers set one, both raising a loader thread.
- **Named, cross-process mutexes.** `MutexClass(const char*)` could make one visible to other
  processes. Nothing in the tree passes a name — every construction takes the `NULL` default — so
  the parameter is kept and asserts if it is ever used.
- **Structured exception handling off Windows.** `ThreadClass`'s `ExceptionHandler` is an
  `_EXCEPTION_POINTERS` callback; a thread that sets one simply runs without it. The handlers in
  this tree write a crash dump, which C5 owns.

### Verification

B8 and B11's method, and the same standard: a green sanitizer with no control proves nothing.

The **real, unmodified `mutex.{h,cpp}` and `thread.{h,cpp}`** were copied byte-for-byte into a
sandbox (verified with `cmp`) beside shims for the five headers they reach — `always.h`,
`wwdebug.h`, `vector.h`, `except.h`, `systimer.h` — and built on arm64 macOS. Eight tests:
mutual exclusion under 8 threads × 20,000 iterations on a deliberately non-atomic counter for both
lock classes; the release/acquire publication the spinlock has to provide; re-entrancy for both
recursive locks under contention; `MutexClass`'s poll and timed acquire; the `ffactory` nesting
shape; thread lifecycle; the `Stop()` timeout shape; and thread-id distinctness.

| | Result |
|:--|:--|
| AppleClang, `-std=c++17 -Wall -Wextra -O1` | both files compile clean, no warnings |
| 8 tests, 160,186 checks | pass, 0 `WWASSERT`s fired |
| ThreadSanitizer | clean |
| AddressSanitizer + UndefinedBehaviorSanitizer | clean |

And the controls:

- **TSan is armed.** The same mutual-exclusion test with the lock removed is reported as a data
  race, and the counter lands on 40,000 of an expected 160,000 — so the lock was doing real work
  and TSan sees through `FastCriticalSectionClass::LockClass` rather than being confused by it.
- **TSan found the `volatile bool` by itself**, before any control was written. Putting `volatile
  bool running` back makes it red again; `std::atomic<bool>` makes it clean. That is the strongest
  form of the control, because nobody arranged it.
- **`std::mutex` deadlocks.** Built with `std::recursive_mutex` → `std::mutex` and
  `std::recursive_timed_mutex` → `std::timed_mutex`, the re-entrancy test hangs on the second
  acquire in both classes; a watchdog ends it at 3s. The real build takes both locks twice and
  returns.

One measurement worth recording: acquiring a contended `FastCriticalSectionClass` took anywhere
from 11ms to 11.4 seconds across runs, because `ThreadClass::Switch_Thread` — the spin's backoff —
sleeps a full millisecond per attempt. That is exactly what the Win32 version did
(`WaitForSingleObject` on a never-signalled event, timeout 1), so it is **pre-existing and not a
regression**, and it is left alone: changing how long the engine's hottest lock waits is a
performance change and this is a port. It is worth somebody's attention later.

Seven tests were added to `test_wwlib.cpp`, which is where `ThreadClass` and
`FastCriticalSectionClass` are already tested. They pin the properties a plausible simplification
would take away — the two recursive locks, the poll that must not block, the timed acquire that
must give up, ids that are distinct and never zero, the `running` flag reaching the worker, and
exclusion over a non-atomic counter. They run on Windows today; `test_wwlib` is still
`PENDING_MACOS` for reasons that are not B14's. They were extracted and run against the real
sources in the sandbox: 21 checks, 0 failed, TSan clean.

The sandbox is not committed, for the reason B8 and B11 gave: its shims become unnecessary the
moment B3 and B5 land, and a rotting shim in the repo is worse than reproduction instructions.

### Not done, and why

- **`WWAudio/Threads.cpp`'s `_beginthread`** — named in this task, deliberately left. It is one of
  five Win32 handles in a file whose *interface* (`Threads.h`) is `HANDLE`-typed, beside
  `CreateEvent`, `SetEvent`, `ReleaseMutex` and `WaitForSingleObject`; porting the `_beginthread`
  alone leaves a half-Win32 file, and porting all of it means porting `Threads.h` too.
  `Threads.cpp` is in **no target's source list** — it is not built — and the sweep's own table
  assigns WWAudio to C4. The same goes for `WWAudio/Utils.{h,cpp}`'s `CRITICAL_SECTION` and
  `WWAudio.cpp`'s two calls on it. **This is the one place B14 does less than its brief says; it
  is flagged rather than quietly dropped.**
- **`WWLib/mpu.cpp`'s four `QueryPerformance*` calls and its `<intrin.h>`** are not threading
  primitives — it measures the CPU's frequency against `__rdtsc`. Same reasoning B2 gave for
  leaving them: it wants one decision from whoever ports `cpudetect`.

### Three things that turned out to be dead, and one that is a trap

The sweep called this "a bigger surface than GameEngine's was". Measured against the build, it is
smaller than it looks, and the shape of what is left matters more than the count:

| | |
|:--|:--|
| `WWLib/critsection.{h,cpp}` | **dead and a trap.** Included by nothing but its own `.cpp`, which is in no target's source list — *and it declares a class called `CriticalSectionClass`, the same name as the live one in `mutex.h`*, with a different interface and different callers. Two definitions of one name in one library is an ODR violation waiting for the first translation unit that includes both. It has never happened only because nothing includes this one. Ported rather than deleted, because deleting one of the three implementations is close enough to "do not unify them" to ask first. **Somebody should decide.** |
| `profile/internal.h`'s `ProfileFastCS` | **unreachable.** Nothing includes `internal.h`; nothing names `ProfileFastCS`. Its spin's wait was already a no-op — `static HANDLE testEvent` is declared there and defined nowhere, so the `if (testEvent)` was always false and the first real use would have failed to link. Ported anyway, per the brief. |
| `wwmouse.{h,cpp}` | **dead.** `wwmouse.cpp` is in no source list and `wwmouse.h` is included by nothing else. Ported anyway — it is four lines — and the sweep missed half of it: there is a **second** `InterlockedIncrement`/`Decrement` pair on `MouseState` at `wwmouse.cpp:704`/`:736`, not just the `Blocked` pair the sweep lists. |
| `wwmemlog.cpp`'s spinlock | **compiled out.** `MEMLOG_USE_FASTCRITICALSECTION` is `0` in both arms of the `_UNIX` switch, so that copy never reached a compiler. The branch that is on — `MEMLOG_USE_CRITICALSECTION` — was a real `CRITICAL_SECTION` and is ported too, along with a lazy init that two threads could both pass (a function-local static fixes that for free). |

So of the four copies of the spinlock the sweep counted, **one was live** (`mutex.h`, backing
`StringClass`, `WideStringClass`, `FastAllocator` and `mempool` — the hot ones). All three
in-scope copies are done in this commit regardless, which is what the brief asked and is the right
call: three identical MSVC-only spins left in the tree is how the next sweep finds a fifth.

`TheGameSpyMutex` is declared `extern` in `GameSpyThread.h` and defined nowhere — the same kind of
dead extern B11 deleted from `CriticalSection.h`. Left alone: it is in `GameEngine`, not WWVegas.
