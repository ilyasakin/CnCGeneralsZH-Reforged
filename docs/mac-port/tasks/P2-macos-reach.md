# P2 — The app runs on the Macs players have

- **Milestone:** M5
- **Depends on:** P1 (the bundle)
- **Status:** deployment target done and merged (-47, 2026-09-26). Universal2: option A chosen by the
  PM. Its path exists in `macos_app` and is tested with stand-ins; a real universal bundle is E2's,
  at release time.

## Why

With no deployment target set, the tools stamp the build machine's own macOS (27.0) into every binary.
The bundle's `LSMinimumSystemVersion` followed, so the app refused every Mac not on the newest macOS.

## 1. The deployment target: macOS 13.0

`CMAKE_OSX_DEPLOYMENT_TARGET` defaults to 13.0, set before `project()` so that every target inherits
it: SDL3, freetype, glslang, SPIRV-Cross, litehtml and GameSpy through `add_subdirectory`, and FFmpeg
through its `-mmacosx-version-min` flag. An explicit `-DCMAKE_OSX_DEPLOYMENT_TARGET` still wins.

**Why 13.0 and not another.** Nothing here forces a newer one:
- SDL3 deploys to 10.13 (`docs/README-macos.md`), and its Metal GPU code guards the newer pixel formats
  with `@available`.
- The shaders are translated to Metal Shading Language 1.2, SDL_shadercross's default and macOS
  10.12's.
- The whole tree built at 13.0 with no availability error, and no "built for newer macOS" link warning.

Nothing forces an older one either. 13.0 (Ventura, 2022) is the PM's default: every Apple silicon Mac
and Intel Macs from 2017 on run it. Going lower would widen what has to be proven without a machine
to prove it on. A lower target stays a one-line change, which this build would check.

**Guarded or an error.** Every target defined in `GeneralsMD/Code/CMakeLists.txt` (the game, the port's
libraries, the tests) compiles with `-Werror=unguarded-availability-new`. So an API newer than the target
without `@available` / `__builtin_available` is a build error. The vendored projects are left to guard
their own calls. The compile commands carry both flags; SDL3's carries the target but not our `-Werror`.
- Armed, with NetPacket.cpp's own compile command: an unguarded `os_sync_wait_on_address` (macOS 14.4)
  fails with "is only available on macOS 14.4 or newer"; guarded by `__builtin_available` it compiles.

**The bundle checks it** (`Tools/make-macos-app.sh --min-macos`, from the build's target):
- `LSMinimumSystemVersion` is the target.
- Every object in every static library on `generals`' link line must carry a minimum of the target or
  older (`otool -l`: `LC_BUILD_VERSION` minos, or `LC_VERSION_MIN_MACOSX`). So must every Mach-O in
  the finished bundle.
- The first check catches a vendored library built for the build machine's version, which the final
  executable's own stamp would hide. Before this change FFmpeg's objects all said 27.0; now all 94 of
  libavutil's say 13.0.
- `macos_app_check`'s controls: a `--min-macos` below the build's is refused, naming the libraries, and
  a Mach-O built for macOS 26 planted in the overlay is refused by the bundle-side check, naming it.

**Rebuild:** 256 s for the whole tree (1,834 steps) at 13.0; disk 7,684,304 KB free before,
7,526,724 after.

## 2. Universal2: recon (not built)

E1 showed x86_64 computes the same world as arm64, and L1 showed the two play together live. A universal
`generals` is two slices in one file. The art is shared and only the executable doubles.

| | A: two build directories, `lipo` the executable | B: one build, `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` |
|:--|:--|:--|
| how | an x86_64 build directory (step 4's recipe) beside `build-mac`; `macos_app` runs `lipo -create` on the two `generals` and `dsymutil` on the result | every CMake-built library compiles both slices; FFmpeg, which configures one architecture at a time (`CMakeLists.txt` refuses two), is built twice and `lipo`'d in its custom command |
| disk | +1.3 GB (step 4 measured the x86_64 `generals`-only directory) | `build-mac` grows by about the same, since each object carries both slices |
| time | about one more `generals` build (the arm64 tree took 256 s; cross-compiling to x86_64 is native speed) | about twice the compile time of each full build |
| changes | small: a path to the second build and a `lipo` step | FFmpeg's custom command, and every test binary is fat too |
| the bundle | executable ≈ 24.6 MB (arm64, stripped) + the x86_64 slice ≈ 50 MB; dSYM ≈ 420 MB beside it | the same |

Recommendation: **A for now.** It costs no change to the everyday arm64 build or its tests. It adds one
directory only when a release bundle is made, and E2's CI can own it. B is the tidier end state once CI
builds release bundles on a machine with the disk for it.
- The x86_64 slice's size is an estimate from the arm64 one; step 4's build was deleted before it was
  stripped.
- Either way, `make-macos-app.sh`'s minimum-macOS check reads both slices (`otool -l` prints each), and
  `replay-check.sh --app` could be run once under Rosetta with `arch -x86_64`.

## 2b. Universal2, option A: the path (built at release time)

The PM chose A: an x86_64 build directory beside the arm64 one, with its `generals` lipo'd in. Not built
now; the path is in place.
- **How.** Configure a second build directory with `-DCMAKE_OSX_ARCHITECTURES=x86_64` (E1's recipe;
  `ninja generals` only) and pass its executable as `-DZH_X86_64_GENERALS=<that build>/generals` to the
  arm64 build. `ninja macos_app` then:
  - checks the file is an x86_64 executable, and that `--generals` has no x86_64 slice of its own;
  - checks its minimum macOS, and that of every object in its own link line's libraries, since its
    folder holds a `build.ninja`;
  - `lipo -create`s the two, runs `dsymutil` on the universal result, strips, and signs.
- **Throughout,** the minimum-macOS check reads every slice (`otool -arch all -l`).
- **Tested with stand-ins** (`macos_app_check`, 4 checks; tiny `cc -arch x86_64` executables, no second
  build):
  - a slice for 13.0 makes a universal executable (`arm64 x86_64`) that verifies, both slices stamped
    13.0;
  - a slice for macOS 26 is refused before a bundle exists;
  - an arm64 file passed as the x86_64 slice is refused.
- **Cost at release time,** from the recon above: +1.3 GB for the x86_64 directory, one more `generals`
  build, an executable of about 50 MB and a dSYM of about 420 MB.
- **What the stand-ins cannot show:** the real x86_64 game inside the bundle. E1 and L1 showed the
  architecture itself, on a separate executable.

## The kept bundle, rebuilt at 13.0 (2026-09-26)

`build-mac/Zero Hour Reforged.app`, the one the user's by-hand check uses, was rebuilt from c71f2ec9 at
the same path:
- `LSMinimumSystemVersion` 13.0, and the executable's minos 13.0;
- the dSYM beside it; the seal verifies `--deep --strict`;
- 13 MB of disk (the art clones again);
- `replay_check_app` on it: 0x0177BEF6 at frame 1200, and the seal still verifies after the run.

## What this cannot see

- **No Mac on an older macOS, and no VM of one, here.** "Runs on 13" is proven by the load commands
  (every shipped object's minimum), by the availability errors, and by the libraries' own stated
  minimums. It is not proven by launching the app on 13. A launch on a real macOS 13 machine
  (Ventura), and on an Intel Mac for a universal build, is still owed.
- Behaviour that differs by OS version at run time without an availability marking, such as Metal
  driver differences on older GPUs or a framework that changed behaviour.
- The bundle the PM kept for the user's by-hand check (`build-mac/Zero Hour Reforged.app`) predates this
  and still says 27.0. It was left untouched as asked; `ninja macos_app` would rebuild it at 13.0.
