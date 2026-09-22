# B13 — AsciiString's refcount

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** not started
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

## Do not

- Do not change `AsciiString`'s interface. Same reasoning as B8's `JobSystem.h` and B11's
  `CriticalSection`: the consumers are everything.
