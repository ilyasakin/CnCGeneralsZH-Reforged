# B16 — wwdebug's Windows dependency

- **Milestone:** M1
- **Depends on:** nothing
- **Blocks:** six tests (**not** determinism evidence — see "What was done")
- **Status:** done — `feature/mac-port-B16`, not verified on Windows. Done criterion redefined, see below.
- **Size:** was "one file". Measured: `wwdebug.cpp`, `wwprofile.h`, `wwprofile.cpp`, and `WWLib/systimer.h`

## Why

Created 2026-09-22 by the test audit, which established the dependency by building all eight
blocked targets individually rather than inferring it.

**`WWDebug/wwdebug.cpp:46`'s `#include <windows.h>` blocks six of the eight failing macOS tests**:
`test_wwmath`, `test_wwlib`, `test_wwsaveload`, `test_wwutil`, `test_gameengine` and
`wwmath_selfcheck`. (The other two are `BaseType.h`, which is B3/B5's.)

`wwmath_selfcheck` is the reason this has its own task. `CMakeLists.txt:238-245` gives it a
separate non-Windows link line, with a comment saying why somebody bothered:

> this self-check is the first evidence that determinism survives clang on arm64, so it is not
> allowed to wait for them

**That comment was wrong, and so was this paragraph** — see "What was done". `wwmath_selfcheck` is
not determinism evidence and never was; E3 is.

## Scope

`wwdebug.cpp` only. It is **not** a stray include — there is real Windows code behind it:

| Line | What |
|--:|:--|
| 69 | `FormatMessage` in `Convert_System_Error_To_String` — **already guarded by EA's own `#ifndef _UNIX`** |
| 82 | `GetLastError` in `Get_Last_System_Error` — **not guarded at all** |
| 308 | `MessageBoxA(...)` for assert failure |
| 316 | `__debugbreak()` |
| 464-498 | the DBWIN32 mechanism — `CreateFileMapping`, shared-memory debug output to a listener |

**The first two rows were missing from this table and they are the ones you hit first.** Measured:
copy the file, delete the include, compile — exactly two errors, `FORMAT_MESSAGE_FROM_SYSTEM` at
`:70` and `GetLastError` at `:82`. **Neither is one of the three this task originally listed.**
`MessageBoxA` and `__debugbreak` only surface once those clear, because clang stops at the first
pass. Two errors now is not the total.

EA anticipated a Unix port *in this exact file* — hence the `#ifndef _UNIX` at `:68`. Note that
this does **not** license defining `_UNIX` (see the plan's rule): it would dispose of the
`FormatMessage` branch for free and still leave `Get_Last_System_Error` unbuilt, so `_UNIX` alone
does not compile the file. The POSIX answers are `strerror_r` and `errno`, two lines each.

EA left the hint at line 47: `//#include "win.h" can use this if allowed to see wwlib`.

## Do

1. Guard the Windows block. On macOS:
   - `MessageBoxA` → write the assert text to `stderr`. There is no message box in a headless
     build and M1 is headless; do not invent a GUI dependency here.
   - `__debugbreak()` → `__builtin_debugtrap()` on clang, or `raise(SIGTRAP)`.
   - The DBWIN32 shared-memory block → does not exist off Windows. It is a debug-output channel to
     an external listener, and there is no equivalent worth writing. Guard it out whole.
2. **Do not reach into `BaseType.h`.** That blocks the other two tests and it belongs to B3 and B5,
   both in flight. Two agents in that header would cost more than the fix.
3. Keep the Windows path byte-identical. This is a file whose whole job is reporting failures, and
   a change to how it reports one on Windows is a change nobody will notice until they need it.

## Done when

`wwmath_selfcheck` **runs** on arm64 and passes, and the other five stop being blocked by this file.
That self-check passing is worth stating plainly in the pull request: it is the first direct
evidence in this project that `DetTrig`'s determinism survives a different compiler on a different
architecture.

`WINDOWS-DEBT.md` row for the guarded block.

## Do not

- Do not make the failing tests pass by disabling them. The test audit's whole point was that
  `wwdebug`, `wwmath` and `compression` carry no deferral marker, so they are targets this project
  expects to build here today, and a test of a target that is supposed to work and does not should
  fail loudly.

## What was done — 2026-09-25, `zhr2-B16`

**The task file was wrong a third time, in three ways.** Recorded so the next reader does not have
to rediscover it.

1. `wwdebug.cpp` has two more Windows sites than the table lists: `Is_Trying_To_Exit` and
   `ExitProcess` at `:301-302` (`Except.h` declares nothing off `_MSC_VER`). They, `MessageBoxA`,
   `__debugbreak` and the DBWIN32 block only appear with `-DWWDEBUG` — which this `CMakeLists.txt`
   never defines, so that whole path is dead in every current configuration.
2. **"`wwdebug.cpp` only" was the wrong scope. Fixing `wwdebug.cpp` alone unblocks nothing.** The
   `wwdebug` target has three sources. `wwprofile.cpp` blocked the library (its own `<windows.h>`,
   `__rdtsc`, `GetCurrentThreadId`, and `WWLib/systimer.h`, which still included `<windows.h>`), and
   `wwprofile.h:101`'s `__int64` blocked `wwmath`'s `cullsys.cpp` and `wwmath.cpp`.
3. **`wwmath_selfcheck` is not determinism evidence, for two independent reasons.** It checked
   `M * inverse(M)` against identity to a `1e-4` tolerance, which passes under any rounding on any
   compiler. And it could not fail in Release: its failure path was `assert(0)`, compiled out by
   `-DNDEBUG`, after which it printed `OK` and exited 0 — and ctest judges the exit code. Measured
   below. The "not allowed to wait for wwlib" reasoning in `CMakeLists.txt` also rested on the
   belief that `wwmath` links against `wwdebug` alone; nobody had linked it, and it does not.

**Done criterion, redefined by the PM:** `wwdebug` 3/3 and `wwmath` 36/36 building. Both archive.
`wwmath_selfcheck` linking is blocked on `wwlib` and `wwsaveload`, i.e. on B5.

| Commit | What |
|:--|:--|
| `8a84887e` | `wwdebug.cpp`: `errno`/`strerror_r`; a no-handler assert goes to stderr and takes the Abort path (no dialog, so no Retry/Ignore — hence no `__builtin_debugtrap`); DBWIN32 handler and declaration `_WIN32` only. `except.h` → `Except.h`. |
| `9e63d75c` | `systimer.h`: `<windows.h>`/`mmsys.h` behind `_WIN32`; B2 had already moved the body to `Clock_Milliseconds`. `wwlib` 58 → 61/82 as a side effect. |
| `afdb96ee` | `wwprofile`: `__int64` → `int64_t` (B3), `GetCurrentThreadId` → `ThreadClass::_Get_Current_Thread_ID` (B14's precedent in `wwmemlog.cpp`), a `Lib/Clock.h` tick off Windows, and five `StringClass`es passed to `%s` through varargs — clang rejects them as "call will abort at runtime". |
| `3861c151` | `test_inverse.cpp` returns failures and checks one exact answer; `wwmath_selfcheck` links `wwlib`/`wwsaveload` everywhere. |

**The selfcheck, measured in a Release build** (`-O3 -DNDEBUG`, the real `test_inverse.o`, the real
`libwwmath.a`/`libwwdebug.a`, and a scratch-only stub object for the 19 `wwlib`/`wwsaveload`
symbols the force-links reach — each stub aborts if called, and none was):

| `Get_Inverse` | Old test | New test |
|:--|:--|:--|
| unchanged | `OK`, exit 0 | `OK`, exit 0 |
| translation sign dropped | 12 `FAIL` lines, then **`OK`, exit 0** | 15 failures, exit 1 |
| column 0 off by one ulp | `OK`, exit 0 | 1 failure (the exact check), exit 1 |

The exact check is the scale matrix's inverse: every scale is a power of two, so every reciprocal and
product is exact and the answer is the same bits on any IEEE compiler. That also means it cannot
tell two compilers apart — the test header says so.

**Left alone, deliberately:** `wwdebug.h:146`, `WWDEBUG_BREAK` expands to `__debugbreak()`. It has
zero users; if one appears in code macOS compiles, it is a compile error, which is loud.

**Surfaced, not B16's:** with `wwdebug` building, `wwutil` is now reached and fails in `miscutil.cpp`
on `mmsys.h:48`'s `<mmsystem.h>`. It is in `all` with no deferral marker.

