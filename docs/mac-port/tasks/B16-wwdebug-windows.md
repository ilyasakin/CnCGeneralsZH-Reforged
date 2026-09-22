# B16 — wwdebug's Windows dependency

- **Milestone:** M1
- **Depends on:** nothing
- **Blocks:** six tests, including the determinism evidence
- **Status:** not started
- **Size:** one file, `Libraries/Source/WWVegas/WWDebug/wwdebug.cpp`

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

It still does not run. **Clearing this file turns the first real determinism evidence on arm64 —
which E1 wants and nobody has — from an unbuildable target into a running one.** That makes it the
highest-leverage single file in the M1 build.

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
