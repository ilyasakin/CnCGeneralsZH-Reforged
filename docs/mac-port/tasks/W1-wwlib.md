# W1 — wwlib under clang

- **Milestone:** M1
- **Depends on:** B3 (the shim), B14 (`mutex.h`), B5 (the Windows types)
- **Blocks:** B6, and every other WWVegas target, all of which link `wwlib`
- **Status:** done (closed 2026-09-26 by -47 with evidence): `wwlib` builds under clang on macOS and every POSIX executable links it (`libwwlib.a`; test_posixpath and the other ctest rows that link it pass)

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
| after the CRT and conformance work below | 49 | 33 |
| after guarding the Win32-only wide strings | 56 | 26 |
| after the tail that revealed | **58** | 24 |

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

**A correction, because this task file said something false.** It claimed that `WideStringClass`'s
width "reaches the replay CRC through `Xfer.cpp`", and it does not. Agent 3a checked the chain and I
verified it independently: `WideStringClass` is named nowhere in `GameEngine` or `GameEngineDevice`
(zero files), `INIClass::Get_Wide_String` has no callers anywhere in the tree, `RegistryClass`'s
overload is Win32-only, and `Xfer.cpp:209` is `xferUnicodeString`, which xfers a `UnicodeString`
using `sizeof(WideChar)`. Two wide-string classes exist; **`UnicodeString` is on the lockstep path
and `WideStringClass` is not**, and the consequence had been attached to the wrong one. Nothing
`WideStringClass` holds reaches a file, a socket or a checksum.

So the Win32-API argument above is the whole reason, and it is sufficient on its own. It never needed
the CRC claim bolted onto it — and that claim would have made the next person treat a cosmetic
question as a lockstep one.

### What was done instead

3a confirmed that nothing in B1 constrains these files and that the 14 should not wait for the flip:
the wide half of `wwstring.h` and `widestring.h` has no portable caller. So it is behind
`#if defined(_WIN32)` — `StringClass`'s three wide declarations and their two inline definitions,
`Copy_Wide`'s body, the whole of `WideStringClass`, and `INIClass::Get_Wide_String`, which has no
callers at all. That unblocked 7 files directly and the rest of the tail behind them.

**WW3D2's `WCHAR` is deliberately untouched.** 3a's survey had said WWVegas' wide strings were "not
reached by `WideChar`", which is true of `wwstring.h` and `widestring.h` and false of WW3D2:
`render2dsentence.h`, `render2d.h` and `font3d.h` carry a `WCHAR` interface that the engine reaches
at five sites in `W3DDisplayString.cpp` and `W3DGameWindow.cpp` — one of which declares `WideChar ch`
and passes it to `Get_Char_Spacing(WCHAR ch)` on the next line. Those are five real conversions for
B1 to meet as compile errors, and guarding them would hide them. That correction came out of asking
this question, which is the argument for asking rather than choosing.

## Notes

`bool.h` is the second instance today of an undefined `_MSC_VER` being read as 0 and silently
selecting a branch written for a different compiler. It is worth grepping for `_MSC_VER <` and
`_MSC_VER >=` across the tree on that basis alone — a version comparison against an undefined macro
is a platform predicate in disguise, which is the family 21 tabled in the plan's rules.
