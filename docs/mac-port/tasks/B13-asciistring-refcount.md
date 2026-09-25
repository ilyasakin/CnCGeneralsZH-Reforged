# B13 — AsciiString's refcount

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** done: merged (was -83)
- **Size:** four calls in one header — and that header is `AsciiString.h`

## Why

Created 2026-09-22 by the threading-primitive sweep, and it is the fourth threading primitive found
sideways.

`GameEngine/Include/Common/AsciiString.h` has four live Win32 calls on the string refcount:

```
:382  InterlockedIncrement((long *)&m_data->m_refCount)
:394  InterlockedDecrement((long *)&m_data->m_refCount)
:459  InterlockedDecrement(...)
:466  InterlockedIncrement(...)
```

In a header. On the most-included class in the engine.

It is small and it is on a path everything depends on, which is the worst combination to discover
late — B6 would have met it while trying to link, with no obvious owner and five other things
failing at the same time.

**It is also a hole in the B5 survey, and the reason is worth keeping.** That survey grepped
`HWND|HRESULT|DWORD|windows.h`. `Interlocked*` is none of those, so `AsciiString.h` appeared there
only for its stray `#include "windows.h"`, classified as "a delete with nothing behind it, worth
doing early". That was wrong: there are four intrinsics behind it, and deleting the include without
replacing them breaks the build in a way that looks like the deleter's fault. Anyone acting on that
line of the B5 survey should read this instead.

The generalisable part: **`CreateThread` and `CRITICAL_SECTION` announce themselves.
`InterlockedIncrement` inside a one-line inline accessor does not.** All four sideways finds have
been in that second category.

## Do

1. `std::atomic<int>` for `m_refCount`. Same shape as B8, much smaller.
2. Note the `(long *)` casts — the field is being punned to `long` to satisfy the Win32 signature.
   That cast goes away with the intrinsic, and it is worth checking what `m_refCount` is actually
   declared as before assuming the new type matches.
3. **Memory ordering matters here and the default is not obviously right.** A refcount increment
   can be `relaxed`; the decrement that may reach zero needs `acq_rel`, or the destructor can run
   before another thread's last write to the object is visible. `InterlockedIncrement`/`Decrement`
   are full barriers, so the safe move is `seq_cst` everywhere first and a considered weakening
   after, with the reasoning written down. Do not weaken it silently to look clever.
4. `UnicodeString` has the same refcount shape but reaches it under `TheUnicodeStringCriticalSection`
   (see B11) rather than through intrinsics. Check whether it needs the same treatment or is
   already covered; they should end up consistent, and B11's lock is now load-bearing on the shape
   of `set()`/`releaseBuffer()`.

## Done when

The four intrinsics are gone, `AsciiString.h` compiles without `windows.h`, and a test exercises
concurrent copy and destruction of a shared string hard enough for TSan to have an opinion —
**with a control proving TSan goes red**, per the standard B8 set.

`WINDOWS-DEBT.md` at high severity: this is the string class, it is in almost every translation
unit, and a refcount ordering mistake presents as a use-after-free a long way from the cause.

## What was found, and what was done

The four intrinsics were the entry point, not the whole of it. Three things were wrong behind them;
one is a real bug, one is undefined behaviour that was not actually corrupting anything, and one is
just the port.

### 1. The refcount is sixteen bits and the atomic was thirty-two

`m_refCount` is `unsigned short`, and `m_numCharsAllocated` is the `unsigned short` immediately
after it at offset 2. `long` is 4 bytes on both Win32 and Win64. So
`InterlockedIncrement((long *)&m_refCount)` was a 32-bit read-modify-write covering both fields —
every refcount operation also read and wrote back its neighbour.

The original comment is the author noticing exactly this and deciding it was fine:

> `// yes, I know it's not a DWord but we're incrementing so we're safe`

**Being accurate about how bad it was, because overclaiming would be worse than not finding it:**
it is type-punning UB, and it was not corrupting anything reachable. The access was correctly
aligned (`MEM_BOUND_ALIGNMENT` is 4), and `m_numCharsAllocated` is written in exactly one place —
`AsciiString.cpp:148`, on a buffer that has just been allocated and is not shared yet — so no
thread can be writing it while another increments the count. The fix is free, so it is worth taking
regardless, but this is not a bug that was biting anyone.

`std::atomic<unsigned short>` keeps the field 16 bits. That matters more than it looks:
`AsciiStringData` is pool-allocated and the block sizes come from `sizeof`. Verified identical on
arm64 and x86-64, in both the debug layout (with `m_debugptr`) and the release one — size 16/16 and
4/4, alignment 8/8 and 2/2, every offset unchanged. It is lock-free everywhere this builds
(`ATOMIC_SHORT_LOCK_FREE == 2`).

### 2. The decrement-then-reread is a double free, and this one is real

```c
InterlockedDecrement((long *)&m_data->m_refCount);
if (!m_data->m_refCount)          // a separate, unsynchronised load
    freeBytes();
```

Two threads dropping the last two references can both observe zero and both call `freeBytes()`.
`InterlockedDecrement` *returns* the new value and the code threw it away.

**Reproduced, not argued.** A control built from the real header with only that shape restored
fails under AddressSanitizer with a heap-use-after-free — the re-read itself touches the block the
other thread has already freed. The fix is to use what `fetch_sub` returns, which is the value from
*before* the subtraction, so exactly one caller sees 1.

This is a behaviour change and the only one in the task. On a single thread the two shapes are
identical; they differ only under the race the interlocked calls exist to handle. Flagged at high
severity in `WINDOWS-DEBT.md` — preserving a known double-free to keep the diff behaviour-free
looked like the wrong trade, but it is a two-line revert if the call is disagreed with.

### 3. Memory ordering

`relaxed` on the increment, `acq_rel` on the decrement, both written out at the call site rather
than left as a constant someone has to look up.

The increment is `relaxed` because the caller already holds a reference to the string being copied,
so the buffer cannot be destroyed underneath it and nothing is being published. That is what every
standard library does for a `shared_ptr` use count. The decrement must be `acq_rel`: the release
half publishes this thread's writes before anyone can free the buffer, and the acquire half means
the thread that observes the last reference going away sees every other thread's writes before it
frees.

This is a weakening from `Interlocked*`, which was a full barrier on every operation, and it was
done deliberately rather than by default — but on x86-64 both still compile to `lock`-prefixed
instructions, so MSVC codegen should be unchanged and the weakening is only observable on arm64.

### 4. `#include "windows.h"` is gone

Which is what the B5 survey said to do, for the wrong reason. It was not "a delete with nothing
behind it" — the four intrinsics were behind it. It is a delete *now*.

## `UnicodeString`: checked, no change needed

`UnicodeStringData` has the identical shape — `unsigned short m_refCount` followed by
`unsigned short m_numCharsAllocated` — but it never used intrinsics. It does plain `++`/`--` under
`TheUnicodeStringCriticalSection`, which B11 made a `std::recursive_mutex`. The lock supplies both
the atomicity and the ordering, so it is already correct and there is nothing here to fix.

The two classes therefore solve the same problem two different ways: one lock-free, one locked.
That is worth knowing and is **not** worth unifying in this task — doing so would mean either
putting a lock into `AsciiString`'s hot path or taking `UnicodeString`'s lock away, and B11's lock
is now load-bearing on the shape of `set()`/`releaseBuffer()`. Left alone deliberately.

## Verification

B8's method.

The **real, unmodified `AsciiString.h`** included by absolute path on arm64 macOS, with shims for
`Lib/BaseType.h`, `Common/Debug.h` and `Common/Errors.h` only. The inline code under test — the
copy constructor, `releaseBuffer()` and `set(const AsciiString&)` — is the real thing. `freeBytes()`
in the harness counts, so "freed exactly once" is an assertion rather than a hope.

| | Result |
|:--|:--|
| AppleClang 21, `-std=c++17 -Wall -Wextra -O2` | compiles clean, no warnings |
| 8 threads x 50,000 copy/destroy cycles on one shared string | pass, and exactly one free at the end |
| 4,000 rounds of 8 threads racing the *last* release | pass, exactly one free every round |
| 8 threads calling `set()` between two shared buffers, 50,000 each | pass |
| ThreadSanitizer | clean |
| AddressSanitizer + UndefinedBehaviorSanitizer | clean |

Two controls, both built from the real header with one thing changed:

- **The old decrement-then-reread**: heap-use-after-free under ASan. This is what proves item 2 is
  a real bug rather than a theoretical one.
- **A non-atomic `unsigned short` refcount with `++`/`--`**: TSan reports races in both the copy
  constructor and `releaseBuffer()`. This is what makes the clean TSan run above mean something.

Harness not committed, same reasoning as B8 and B11 — its three shim headers become unnecessary the
moment B3 lands. It is ~120 lines: the three tests, a counting `freeBytes`, and a local
`ensureUniqueBufferOfSize` over `malloc`.

## Not covered

- **The `_DEBUG`/`_INTERNAL` layout is verified by `sizeof` on clang, not by building it.** That
  configuration adds `m_debugptr` and compiles `validate()`, which reads `m_refCount`. Nobody here
  can build it; the debt row says to.
- `AsciiString.cpp`'s `_stricmp` and the rest of the MSVC CRT names are B3's and were shimmed for
  the harness rather than touched.

## Do not

- Do not change `AsciiString`'s interface. Same reasoning as B8's `JobSystem.h` and B11's
  `CriticalSection`: the consumers are everything.
