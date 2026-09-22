# A3 — build.sh

- **Milestone:** M1
- **Depends on:** A2
- **Blocks:** nothing
- **Status:** claimed
- **Size:** one new script, ~120 lines, mirroring `build.bat`

## Why

`build.bat` is the whole build story on Windows — double-click it on a clone that has never been
built and it finds cmake, vendors, configures, builds and stages `Run/`. A Mac contributor should
get the same, one command, no prerequisites to read about.

## Scope

New: `build.sh` at the repository root, beside `build.bat`. Read `build.bat` (127 lines) and match
its argument shape exactly so the docs can describe one thing:

```console
./build.sh                   # Release
./build.sh Release test      # build, then ctest
./build.sh Release generals  # one target
./build.sh clean             # throw the build tree away first
```

## Do

1. Call `Tools/vendor.sh` before configuring, as `build.bat` calls `vendor.ps1`.
2. Build tree at `build-mac/`, not `build64/`. The two must be able to coexist in one clone —
   someone will be testing both from the same checkout over a network share or a VM.
3. Honour `build.local.sh` beside it, git-ignored, read after the defaults, for the same reason
   `build.local.bat` exists: a machine that needs an override does not edit a tracked file. Add it
   to `.gitignore`.
4. Ninja if present, `Unix Makefiles` otherwise. Do not require Ninja.
5. Staging into `GeneralsMD/Run/` is a no-op until M2 produces something to stage. Leave the step
   present and obviously inert rather than absent.

## Done when

On a clean clone on an arm64 Mac with Xcode command line tools and CMake and nothing else,
`./build.sh` vendors, configures and builds whatever the tree currently supports without the user
setting anything first, and `./build.sh Release test` runs `ctest`. Running it twice is fast.

On Windows, `build.bat` is unchanged and `build64/` still works from the same clone.

## Do not

- Do not make `build.sh` the documented path in `README.md` yet. The README says "Windows x86" and
  a platform badge; it gets updated when there is a Mac build worth telling people about, which is
  M4 at the earliest.
