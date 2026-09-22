# W1 — wwlib under clang

- **Milestone:** M1
- **Depends on:** B3 (the shim), B14 (`mutex.h`), B5 (the Windows types)
- **Blocks:** B6, and every other WWVegas target, all of which link `wwlib`
- **Status:** in progress — mac-port-wwlib

## Why

`wwlib` is 82 sources that every other WWVegas target links, and until now nobody had compiled any
of it: `wwmath` reaches five of its headers and stops. B3 measured the CRT surface across the whole
tree, but measuring an include is not the same as compiling the file behind it.

## Measured, per file, first error only

A sweep with `-fsyntax-only -ferror-limit=1` over the 82 sources CMake names, taking the first real
`error:` line rather than grepping the output for filenames — that grep matches the
`In file included from` chain instead of the error, which is how two different people got two
different attributions for `wwmath` earlier today.

| | compile | fail |
|:--|--:|--:|
| at the start | 24 | 58 |
| after `bool.h` | 35 | 47 |
| after `point.h` | 40 | 42 |

## What is left, and whose it is

| Cause | Files | Owner |
|:--|--:|:--|
| `mutex.h:142` `_interlockedbittestandset` | **18** | B14 |
| `ddraw.h` via `dsurface.h`, `misc.h` | 3 | B5 / D-track |
| `data.h:54` `LPCSTR` | 2 | B5 |
| `systimer.h:42` `windows.h` | 2 | B2 + B5 |
| `windows.h` direct: `LaunchWeb.cpp`, `refcount.cpp`, `verchk.h` | 3 | B5 |
| `HWND`/`HANDLE`/`HMODULE`/`HINSTANCE`: `keyboard.h`, `msgloop.cpp`, `mono.h`, `rcfile.h`, `win.cpp` | 5 | B5 |
| `oaidl.h` (COM) via `WWCOMUtil.h` | 1 | B5 |
| `<intrin.h>` for x86 `__cpuid`/`__rdtsc` in `mpu.cpp` | 1 | B14 / B5 |
| `<process.h>` in `thread.cpp`, `srandom.cpp` | 2 | B14 (`_beginthreadex`) |
| **CRT and conformance, this task** | **5** | here |

So after B14 and B5 land, `wwlib` is five files away, and those five are the ones below.

## Done here

- **`bool.h`** — one line, eleven files. A 1994 workaround for compilers without `bool`, whose guard
  is `(_MSC_VER < 1100)`. That is false on every MSVC since Visual C++ 5, so the file has been inert
  on Windows for twenty-five years — but on a compiler that does not define `_MSC_VER` at all the
  preprocessor reads the undefined name as 0, `(0 < 1100)` passes, and clang walks into
  `typedef int bool` over a keyword. Requiring `defined(_MSC_VER)` says what was meant and changes
  the answer for no compiler that previously reached the test.
- **`point.h`** — `this->` on `X` and `Y` inside `TPoint3D`, which derives from the dependent base
  `TPoint2D<T>`. Five files. Same class of fix as `Vector.H`, `simplevec.h` and `v3_rnd.h` in B3.

## Notes

`bool.h` is the second instance today of an undefined `_MSC_VER` being read as 0 and silently
selecting a branch written for a different compiler. It is worth grepping for `_MSC_VER <` and
`_MSC_VER >=` across the tree on that basis alone — a version comparison against an undefined macro
is a platform predicate in disguise, which is the family 21 tabled in the plan's rules.
