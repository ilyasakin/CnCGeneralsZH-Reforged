# A1 — CMake toolchain split

- **Milestone:** M1
- **Depends on:** nothing
- **Blocks:** B1 B2 B3 B4 B5
- **Status:** not started
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

## Notes

`CXX_STANDARD 17` is set per target on `gameengine`, `gameenginedevice` and `test_gameengine`.
Leave that alone — hoisting it to a global is a separate change and would quietly alter how the
older libraries compile.
