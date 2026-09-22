# B14 — WWVegas' own threading primitives

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** not started
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
