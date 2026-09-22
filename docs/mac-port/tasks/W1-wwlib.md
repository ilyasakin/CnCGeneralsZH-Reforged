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
| after B14 merged | 43 | 39 |
| after the CRT and conformance work below | **49** | 33 |

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

## The rest of the CRT and conformance work

- **`crc.h` / `crc.cpp`** — the one that matters. `CRCEngine`'s accumulator was `long` and its
  staging buffer `char[sizeof(long)]`, so on Windows it rotated within 32 bits and blocked on four
  bytes; on LP64 it would rotate within 64 and block on eight, and **compute a different CRC from
  the same input**. Shimming `_lrotl` alone would have compiled and been silently wrong. Pinned to
  `int32_t` with an explicit 32-bit rotate, which is what Windows already did.
  `Tests/test_wwlib.cpp` gains `crcengine_accumulator_is_32_bits_wide`, asserting 0x93b0f838 for
  "Westwood Studios" — because the existing `crcengine_byte_at_a_time_matches_block` passes at
  *either* width. Both of its sides move together, so a self-consistency test cannot see this class
  of bug at all. That is the third time today a check has been unable to see a property it shares.
- **`random.cpp`** — 40 constants in `Random3Class::Mix1`/`Mix2` all have their top bit set and none
  fits in an `int`. MSVC takes them with C4305 and wraps; clang rejects the narrowing. Explicit
  `(int)` casts, which is what MSVC was doing implicitly, on the seed table of a generator the
  simulation draws from.
- **`int.cpp`** — `bignum` is `Int<MAX_UNIT_PRECISION>`, so its four static members are
  specialisations and need `template<>`.
- **`lcw.cpp`** — `(unsigned) dest_ptr & 0x3` is an alignment test that truncated a 64-bit pointer
  first. `uintptr_t` keeps the same two bits without the undefined behaviour.
- **`multilist.h`** — `this->` on `CurNode` and `Remove_Current_Object` in
  `PriorityMultiListIterator`. Note that `PriorityMultiListIterator::List` was already written out,
  so somebody met this once and qualified exactly one name.
- **`srandom.cpp`** — wants `getpid`, which is POSIX, not `<process.h>`.
- **`strtok_r`** — POSIX declares it in `<string.h>` with C linkage, so declaring it again with C++
  linkage is a hard error. See the `_UNIX` note below for why the fix is not the obvious one.

## A trap: do not define `_UNIX`

`strtok_r.h` guards its declaration with `#ifndef _UNIX`, and that looks like an invitation. It is
not. `_UNIX` appears at about sixty sites across WWVegas — `rawfile.cpp` alone has twenty — plus
`cpudetect.cpp`, `data.cpp`, `ini.cpp`, `hash.cpp`, `matrix3d.h`, `vector3.h` and `udp.h`. It is
Westwood's own never-finished UNIX port, and defining it would make a large part of `wwlib` compile
at once.

It would also be a disaster, and the tree already says so. `mutex.cpp`'s own comment records that
the `#ifdef _UNIX` arms it used to carry "were not a port; they were a hole" — anything built with
`_UNIX` defined ran the engine's synchronisation as no-ops — and `thread.cpp:201` records that the
`_UNIX` branch "used to return 0 from here for every thread". Those arms have since been removed, but
the other sixty have not been read.

So `_UNIX` is a switch that trades compile errors for silent misbehaviour, which is exactly what B5's
task file forbids for `WinTypes.h` and the same shape as the vendored platform predicates in the
plan's rules. Anything that wants one of those arms should read it first and guard on what it
actually needs.

## What is left, and it is nobody's here

| Cause | Files | Owner |
|:--|--:|:--|
| `wwstring.h:87` `WCHAR` | **14** | B1 — see below |
| Windows types and headers: `windows.h`, `ddraw.h`, `oaidl.h`, `LPCSTR`, `HWND`, `HANDLE`, `HMODULE`, `HINSTANCE`, `DWORD` | 18 | B5 |
| `<intrin.h>` for x86 `__cpuid`/`__rdtsc` in `mpu.cpp` | 1 | B14 / B5 |

### `WCHAR` is B1's, and it is not a typedef

`wwstring.h:87` is the single biggest remaining blocker, at 14 files. The tempting fix is
`typedef wchar_t WCHAR` in the shim. That would be wrong twice over, and the reading is short:

`StringClass`'s `WCHAR` surface is three declarations — a constructor, an `operator=` and
`Copy_Wide` — and all three funnel into `Copy_Wide`, which calls `WideCharToMultiByte(CP_ACP, ...)`
and a `BOOL`, converts straight into the narrow buffer, and **never stores a `WCHAR`**. So the type's
width is almost irrelevant to `StringClass` itself; what matters is that the only implementation is a
Win32 API that does not exist on macOS. A typedef would compile the header and leave a function that
cannot link, or invite a second conversion path.

Meanwhile the only in-tree callers are `widestring.h:775,782`, and `WideStringClass` genuinely does
store a `WCHAR *m_Buffer`. That is the class B1 is converting to `char16_t`, precisely because
`wchar_t` is 2 bytes on MSVC and 4 on clang and that width reaches the replay CRC through `Xfer.cpp`.
Adding `typedef wchar_t WCHAR` would reintroduce the 4-byte type into a class every WWVegas target
links — the exact bug B1 exists to prevent.

So this is not a shim. It is B1 arriving at `wwstring.h`/`widestring.h`, and the right answer is
whatever B1 decides `char16_t` narrows through.

## Notes

`bool.h` is the second instance today of an undefined `_MSC_VER` being read as 0 and silently
selecting a branch written for a different compiler. It is worth grepping for `_MSC_VER <` and
`_MSC_VER >=` across the tree on that basis alone — a version comparison against an undefined macro
is a platform predicate in disguise, which is the family 21 tabled in the plan's rules.
