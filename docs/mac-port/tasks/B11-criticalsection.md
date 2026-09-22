# B11 — CriticalSection

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** not started
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

## Do not

- Do not use `std::mutex` without first establishing that nothing re-enters. The allocator is
  reached from a worker thread and from the string classes; that is exactly where a recursive
  acquire hides.
