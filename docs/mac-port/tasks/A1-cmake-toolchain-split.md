# A1 — CMake toolchain split

- **Milestone:** M1
- **Depends on:** nothing
- **Blocks:** B1 B2 B3 B4 B5
- **Status:** done: configure on arm64; the four libraries its first acceptance named now build (was -95)
- **Size:** one file, `GeneralsMD/Code/CMakeLists.txt` (~810 lines), plus a new toolchain include

## Why

`CMakeLists.txt` assumes MSVC from its first line to its last, and every other task in tracks A, B
and C is blocked until `cmake -S GeneralsMD/Code -B build` gets past configure on a Mac. This task
does not make anything compile. It makes the configure step finish and the *portable* targets
start compiling, so the other tasks have somewhere to land.

## Scope

`GeneralsMD/Code/CMakeLists.txt`. The MSVC-specific things in it, by line at time of writing:

| Line | What |
|:--|:--|
| 12 | `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded..."` — static CRT, Windows-only concern |
| 40 | `add_compile_definitions(WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)` |
| 53–81 | the whole `if(MSVC)` block: `/W3 /permissive /EHa /Oy- /Zi /FS /MP`, `add_link_options(/DEBUG)` |
| 373 | `COMPILE_OPTIONS /MP1` on `dx8webbrowser.cpp` |
| 609 | `target_link_options(generals PRIVATE /MAP)` |
| 719 | `target_link_options(test_wwlib PRIVATE /LARGEADDRESSAWARE)` |
| 21 | the `CMAKE_SIZEOF_VOID_P` guard — arm64 passes it, leave it alone |

Read the comments above each before you move them. Several of them are load-bearing and explain
themselves: `/EHa` exists because `_set_se_translator` is dead without it, `/Oy- /Zi` exist because
`StackDump.cpp` walks the frame pointer chain by hand, `/FS` exists because `/MP` and one PDB do
not mix. Those reasons are Windows reasons. They do not transfer, and the comments should say so
after you are done rather than being deleted.

## Do

1. Introduce a platform variable near the top — `ZH_PLATFORM_WINDOWS` / `ZH_PLATFORM_MACOS` — and
   use it rather than `if(MSVC)` where the question is really "is this Windows", not "is this the
   Microsoft compiler". They are different questions and the file currently conflates them.
2. Put the AppleClang branch beside the MSVC one. Starting set, and justify any addition:
   - `-fexceptions` (the code uses them heavily; `WinMain`'s outer `catch(...)` is structural)
   - `-ffp-contract=off` — **not optional.** clang contracts `a*b+c` into an FMA on arm64 and MSVC
     does not. One contracted multiply-add in the simulation and the Mac build plays a different
     game. E1 tests this; A1 sets it.
   - `-fno-strict-aliasing` — 2003 C++, and the `.big` and `.w3d` readers cast freely
   - `-Wno-` for the noisiest of what clang finds and MSVC did not; keep the list short and
     explicit, do not reach for `-w`
   - `-g` (the Mac equivalent of why `/Zi` is there)
   - **No** `-fno-omit-frame-pointer` for `StackDump`'s sake. C5 replaces that mechanism entirely;
     do not prop up the EBP walk on a platform that never had one.
3. `WIN32` and `_WINDOWS` come out of the unconditional `add_compile_definitions` and go into the
   Windows branch. Expect fallout: things in the tree test `WIN32` to mean "Windows" and things
   test it to mean "not 16-bit". Fix the ones the compiler finds; do not go hunting.
4. Gate the Windows-only targets out on Mac rather than trying to build them:
   `eabrowserdispatch` (MIDL), `dx9_smoke`, `dx11*` and their tests, `ffshader`/`ffvertex` (they
   link `d3d9`), `binkvideo`, `milesaudio`, `generals` itself, `bink_smoke`, `miles_smoke`,
   `eip_sampler`. Gate them, leave them intact. Tracks C and D bring them back one at a time.
5. Leave the `foreach(vendored ...)` guard working. On Mac it should not demand
   `Libraries/DirectX/Include/d3d8.h`, which nothing portable needs — A2 decides what a Mac fetch
   actually includes, so gate the DirectX entry on `ZH_PLATFORM_WINDOWS`.

## Done when

On an arm64 Mac with CMake and Xcode command line tools:

```console
cmake -S GeneralsMD/Code -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --target wwdebug wwutil wwmath compression
ctest --test-dir build-mac -R 'wwmath_selfcheck|compression_selfcheck' --output-on-failure
```

configures clean, builds those four libraries, and both self-checks pass. `wwmath` is the
meaningful one: it is `dettrig.cpp` and the vector maths, and it passing on arm64 is the first
evidence the determinism story holds.

And on Windows, unchanged:

```console
cmake -S GeneralsMD/Code -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Same targets built, same tests green, same flags on the MSVC command lines as before. Diff a
verbose build log from before and after if you want to be sure; the MSVC branch should be byte
identical in effect.

## Do not

- Do not attempt `wwlib`, `ww3d2`, `gameengine` or any test beyond the two self-checks. They need
  B1–B6 and will bury you in errors that are not this task's.
- Do not delete the MSVC comments. Move them, and add why they are Windows-only.
- Do not add `-w` or `-Wno-everything`. The warnings clang finds in this tree are worth reading
  once; B3 and B5 will act on some of them.

## What was done, and what turned out to be wrong

`cmake -S GeneralsMD/Code -B build-mac -DCMAKE_BUILD_TYPE=Release` configures clean on an arm64 Mac
(CMake 4.4.3, AppleClang 21.0.0), which is what B1-B5 were waiting for. The flag set went in as
specified, plus `-fsigned-char`: MSVC's plain `char` is signed and this code compares one against
zero, and although Darwin/arm64 already signs it — unlike the ARM EABI — pinning a default that
matters to the simulation costs one line and stops it being a discovery.

`-ffp-contract=off` was measured rather than assumed. Compiling `a * b + c` with the exact flags
CMake generates for `wwmath`:

| Flags | arm64 output |
|:--|:--|
| as configured | `fmul` then `fadd` — two roundings, as MSVC does |
| same, minus `-ffp-contract=off` | `fmadd` — one rounding |

Two contractions in a two-line file, so the flag is doing the only job it exists for. E1 still owns
proving it holds across the whole simulation.

**The "Done when" build cannot be reached from this task.** The four libraries it names are not
portable, and the plan has A1 -> B3/B5 while this half of A1 needs B3 and B5. Measured:

| Target | What stops it | Owner |
|:--|:--|:--|
| `wwdebug` | all three sources `#include <windows.h>`; `wwmemlog.cpp` also wants `<intrin.h>`; `wwprofile.cpp` also wants `systimer.h`, `cpudetect.h`, `rawfile.h`, `ffactory.h` — i.e. the whole of `wwlib` | B2 B3 B5 B6 |
| `wwutil` | `miscutil.cpp` includes `win.h`, `mmsys.h`, `rawfile.h`, `ffactory.h` | B3 B6 |
| `wwmath` | nothing of its own. Two `.cpp` reach a `wwlib` Windows header; the rest is clean, and every `<float.h>` in it is standard | — |
| `compression` | `BaseType.h`'s `__int64`, `#include <windef.h>` and `#include <emmintrin.h>` (SSE2 on arm64) | B5 |
| shared by all four | `always.h` uses `size_t` with no `<stddef.h>` and spells `__cdecl` 13 times; `vector.h` includes `<new.h>` rather than `<new>` | B3 |

So `wwmath` is the one that is genuinely nearly ready, and it is the one the task file says is the
meaningful one — but it cannot link `wwmath_selfcheck` until `wwdebug` compiles, and `wwdebug`
needs a `windows.h` story. The right order is A1 -> B3 + B5 -> the four libraries and the two
self-checks, not A1 -> all of it.

## Three things found on the way that belong to other tasks

- **Backslash `#include` paths.** `#include "..\..\..\..\gameengine\include\common\debug.h"`
  in `WWDebug/wwdebug.h` — MSVC takes either separator, clang takes only `/`. Fixed here because it
  was the first error out of the compiler and `wwdebug` is on this task's critical path; the real
  spelling on disk went in with it, so a case-sensitive volume works too. **There are about 150
  more**, almost all in `GameEngine/Source/GameLogic/Object/Update/` (`SpectreGunshipUpdate.cpp`
  alone has 30), plus `WWAudio/WWAudio.cpp` and `WW3D2/textdraw.h`. No task in this plan owns them
  and every one of them is a hard error under clang. B1 will hit them first.
- **A 64-bit truncation in `Compression/EAC/huffencode.cpp:1053`.** `((int) bptr1 - (int) EC->buffer)`
  casts two 64-bit pointers to `int` and subtracts. MSVC makes that warning C4311 and the Windows
  x64 build gets the right answer by accident, because truncating both before subtracting is correct
  modulo 2^32 for a small difference. clang makes it an error. `(int)(bptr1 - EC->buffer)` is the
  same value on both. Belongs to B5.
- **zlib 1.1.4 does not typedef `Byte` on a Mac.** `zconf.h:213` is
  `#if !defined(MACOS) && !defined(TARGET_OS_MAC)`, because Apple's `MacTypes.h` owns that name —
  and `TARGET_OS_MAC` arrives transitively through the system headers. Under `Z_PREFIX` the typedef
  would have been `z_Byte`, so Apple's `Byte` does not stand in for it and every zlib translation
  unit fails. It cannot be fixed in-repo: `zconf.h` is vendored and gitignored. **A2 has to patch it
  as it fetches**, and no compile definition can substitute, because the guard tests `defined()`.

## What the macOS configuration builds today

`wwdebug wwutil wwmath compression` and the two self-checks are still in the default build and still
fail — deliberately, so the gap stays visible. What does build and pass: `benchmark`, and all seven
of the `Tools/*.py` selfchecks under `ctest`. Defined but held out of the default build until their
tasks land: `wwlib`, `wwsaveload`, `gameengine`, `test_wwmath`, `test_wwlib`, `test_wwsaveload`,
`test_wwutil`, `test_gameengine`. Not defined on macOS at all: the renderer, both Direct3D backends,
`gameenginedevice`, `generals`, the Miles and Bink backends, MIDL's browser dispatch, the GameSpy
SDK, `debug`, `profile`, `wwdownload` and every test belonging to them.

## Notes

`CXX_STANDARD 17` is set per target on `gameengine`, `gameenginedevice` and `test_gameengine`.
Leave that alone — hoisting it to a global is a separate change and would quietly alter how the
older libraries compile.
