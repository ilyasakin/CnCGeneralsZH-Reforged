# B11 — CriticalSection

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** done: merged (was -83)
- **Size:** `GameEngine/Include/Common/CriticalSection.h` + `Source/Common/System/CriticalSection.cpp`

## Why

Created 2026-09-22, found sideways by B8. Same shape as B8 — a threading primitive, not a type
leak — and nothing owned it.

`CriticalSection.h:42` holds a raw `CRITICAL_SECTION` with `InitializeCriticalSection`,
`EnterCriticalSection`, `LeaveCriticalSection` and `DeleteCriticalSection`. There is also a
`ScopedCriticalSection` RAII wrapper at `:79`.

Its consumers are not peripheral. It is used by **`GameMemory.cpp`** (it is what makes the
allocator safe to call from a worker — THREADING-ROADMAP 1.1), **`AsciiString.h`**,
**`UnicodeString.cpp`**, `Debug.cpp`, `PerfTimer.h`, and others. The allocator and both string
classes, in other words: B6 will trip over this, and so will anything else that compiles
`GameEngine`.

## Scope

- `GameEngine/Include/Common/CriticalSection.h`, `Source/Common/System/CriticalSection.cpp`
- Consumers only if the interface must change — prefer that it does not

## Do

1. `std::recursive_mutex` is the honest mapping and the choice needs stating: a Win32
   `CRITICAL_SECTION` **is recursive**, and `std::mutex` is not. If any consumer re-enters — the
   allocator is the one to check — a plain `std::mutex` is undefined behaviour rather than a
   deadlock you would notice. Establish whether recursion actually happens; if it does not,
   `std::mutex` is better and the comment should say why it is safe.
2. Keep `CriticalSection` and `ScopedCriticalSection`'s interfaces unchanged, for the same reason
   B8 kept `JobSystem.h` byte-identical — the consumers are load-bearing and should not be in this
   diff.
3. Spin count: if `InitializeCriticalSectionAndSpinCount` is used anywhere, note that the standard
   library has no equivalent and say what is lost.
4. **Follow B8's verification method.** It compiled and ran the real unmodified source on macOS
   against shim headers for its dependencies, ran ThreadSanitizer over it, and then proved the
   sanitizer could go red by deliberately breaking the code. That is the standard for threading
   work on this project now. A green TSan run with no control proves nothing.

## Done when

It builds and runs on macOS, TSan is clean **and demonstrated to be armed**, consumers are
unmodified, and the recursion question is answered in writing rather than assumed.

`WINDOWS-DEBT.md` rows describing the failure *shape*, as B8's do: a lock that silently stops being
recursive does not fail as a wrong answer, it deadlocks or corrupts the heap a long way from the
cause.

## The recursion question, settled

**Recursion happens. `std::mutex` is not available to us. `std::recursive_mutex` is required, not
preferred.**

The task file asked for this to be established rather than assumed, so it was established twice —
once by reading, once by building the wrong version and watching it fail.

### By reading

```
UnicodeString::set( const UnicodeString &stringSrc )          UnicodeString.cpp:155
    ScopedCriticalSection lock( TheUnicodeStringCriticalSection );   :157   <- acquire
    validate();
    if (&stringSrc != this)
    {
        releaseBuffer();                                             :162
            ScopedCriticalSection lock( TheUnicodeStringCriticalSection );   :130   <- again
```

The lock taken at `:157` is still in scope at `:162`. The branch it sits in is
`&stringSrc != this`, which is the ordinary case for an assignment, not an edge one. So this is
not a rare path — it is every `UnicodeString::set(const UnicodeString&)` call in the game.

### By building it wrong

The same header with `std::recursive_mutex` swapped for `std::mutex`, running **one**
`set()`-shaped call on a single thread: killed at 10s. Not intermittent, not load-dependent —
the first call, deterministically. That is the control that makes the choice a measurement rather
than an argument.

### The other three locks do not recurse

Checked, not assumed. All ten live `ScopedCriticalSection` sites in the tree:

| Lock | Sites | Recurses? |
|:--|:--|:--|
| `TheUnicodeStringCriticalSection` | `UnicodeString.cpp` 73, 130, 157 | **yes**, 157 → 130 |
| `TheMemoryPoolCriticalSection` | `GameMemory.cpp` 1644, 1734, 1793, 1814 | no — `createBlob`, `freeBlob`, `init`, `sysAllocateDoNotZero`, `sysFree` take no lock |
| `TheDmaCriticalSection` | `GameMemory.cpp` 2182, 2298 | no — nests into the pool lock, a different object |
| `TheDebugLogCriticalSection` | `Debug.cpp` 451 | no — `doLogOutput` is a leaf: `fprintf` and `OutputDebugString`, no allocation, no way back into `DebugLog` |

They share one class, so they get the recursive one too. That costs an owner check on an
uncontended acquire, which is what `CRITICAL_SECTION` was already doing.

### Lock ordering, since three of the four nest

`Unicode → Dma → Pool`, in that direction only. `UnicodeString::releaseBuffer` calls
`TheDynamicMemoryAllocator->freeBytes` while holding the Unicode lock, and
`DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation` calls into the pool while holding
the Dma lock. Nothing in `GameMemory.cpp` takes the Unicode lock, so there is no cycle. This was
true before and has to stay true; it is recorded here because nothing else records it.

## Spin count

Nothing is lost. `InitializeCriticalSectionAndSpinCount`, `SetCriticalSectionSpinCount` and
`TryEnterCriticalSection` appear **nowhere** in this tree, so all five of these locks were plain
`InitializeCriticalSection` with the system default. There was no tuning to preserve.

## One deletion beyond the rewrite, and why

`TheAsciiStringCriticalSection` is gone, along with the `#include "mutex.h"` that only it needed.

It is dead and has been for a long time: its three uses in `AsciiString.h` (`:378`, `:389`,
`:450`) are all commented out, nothing else in the tree names it, and `WinMain.cpp` never
assigned it — `critSec1` at `WinMain.cpp:995` is declared and never used, the other half of the
same vestige.

It is worth deleting rather than leaving because of what it drags: it is a
`FastCriticalSectionClass`, from WWVegas' `mutex.h`, which includes `<intrin.h>` and calls
`_interlockedbittestandset`. Both are MSVC-only. So a dead extern was keeping MSVC intrinsics in
the header that the allocator and both string classes compile against, and removing it is most of
what makes `CriticalSection.h` clean on macOS. This is not a change to `CriticalSection` or
`ScopedCriticalSection`'s interface, which the task asked be kept — it is an unrelated global of
a different type that happened to live in the same header.

## Verification

B8's method, which this task named as the standard.

The **real, unmodified `CriticalSection.h`** was included by absolute path from a harness on arm64
macOS, with a shim supplying only `Common/PerfTimer.h` (the real one reaches `GameLogic` and
`GlobalData`). Four tests: mutual exclusion under 8 threads × 20,000 iterations on a deliberately
non-atomic counter, the `UnicodeString::set → releaseBuffer` recursion shape under the same
contention, the `Dma → Pool` distinct-lock nesting shape, and the documented NULL-lock case that
`ScopedCriticalSection` treats as silently unlocked.

| | Result |
|:--|:--|
| AppleClang 21, `-std=c++17 -Wall -Wextra -O2` | compiles clean, no warnings |
| All four tests | pass |
| ThreadSanitizer | clean |
| AddressSanitizer + UndefinedBehaviorSanitizer | clean |

And the two controls, because a green sanitizer that cannot go red proves nothing:

- **TSan is armed.** The same test with the lock pointer left NULL is reported as a data race, and
  the counter lands on 22,011 of an expected 160,000 — so the lock in the real test was doing
  work, and TSan sees through `ScopedCriticalSection` rather than being confused by it.
- **`std::mutex` deadlocks**, as described above. This is the control that answers the task's
  central question.

The harness is not committed, for the same reason B8's was not: its one shim header becomes
unnecessary the moment B3 and B5 land, and a rotting shim in the repo is worse than reproduction
instructions. It is ~120 lines and consists of the four tests above with `CHECK`/`CHECK_EQ`
reimplemented and the four globals defined as `CriticalSection.cpp` defines them.

## Not covered

- **`CriticalSection.h` still cannot be compiled as part of `GameEngine` on macOS**, because every
  consumer sits behind `PreRTS.h` and B5 has not guarded it yet. That is the same position B8 was
  in and it is outside this task. The header itself is now clean: after the `mutex.h` removal it
  needs `<mutex>` and `Common/PerfTimer.h` and nothing else.
- **WWVegas has its own, separate Win32 critical sections** — `WWLib/critsection.cpp` and
  `WWLib/mutex.cpp` both call `InitializeCriticalSection` directly, and `mutex.h`'s
  `FastCriticalSectionClass` is an `<intrin.h>` spinlock. They are a different class hierarchy in
  a different library and no task owns them. Flagging, not fixing: B6 will meet them.

## Do not

- Do not use `std::mutex` without first establishing that nothing re-enters. The allocator is
  reached from a worker thread and from the string classes; that is exactly where a recursive
  acquire hides.
