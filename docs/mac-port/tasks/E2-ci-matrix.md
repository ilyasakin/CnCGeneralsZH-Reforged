# E2 — CI matrix

- **Milestone:** M5
- **Depends on:** E1
- **Blocks:** nothing
- **Status:** not started
- **Size:** new workflow files; no application code

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** The matrix gets a Linux job (x86_64 and arm64, GCC and Clang) beside macOS arm64 and Windows. Until there is CI, the Linux container check in `docs/mac-port/README.md` decision 3 is the stand-in.

## Why

`README.md`: "There is no CI and no debugger on the machine this is built on. The game tests itself
instead." That worked for one platform and one developer. It does not survive two platforms and
several agents working in parallel, because "the Windows build never regresses" is rule one of this
plan and nobody can check it by hand on every pull request.

This is last because there is no point running CI on a build that does not exist yet.

## Scope

New: `.github/workflows/`. Nothing else.

## Do

1. Two jobs: `windows-latest` with MSVC x64, and `macos-latest` on arm64. Both configure, build and
   run `ctest`.
2. The cross-platform determinism comparison from E1 is the job that matters most. It needs both
   platforms' output in one place, so it is a third job that depends on the first two and compares
   artifacts.
3. `CONTRIBUTING.md` already describes a commit-message check that reads the first line of each
   commit and fails the ones that do not match Conventional Commits. Whatever runs that today
   should sit beside these.
4. Vendored dependencies are fetched per run by `vendor.ps1` and `vendor.sh`. Cache them; the
   GameSpy SDK and the art release are the slow parts.
5. Game data cannot be in CI. Zero Hour's `.big` files are not ours to distribute, so the suites
   that need them (`bink_smoke`, `miles_smoke`, anything reading `Run/`) already print "skip"
   without data. Make sure "skip" is not mistaken for "pass" in the report.
6. Code signing and notarisation for a Mac release is the other open question on this plan. It does
   not belong in a pull request check, but the answer belongs in this file.

## Done when

Every pull request builds and tests on both platforms, the determinism comparison runs, and a red
result blocks a merge.

## Do not

- Do not make CI the only place tests run. The project's habit of proving a fix by putting the bug
  back and watching the test fail is worth more than a green badge, and it happens locally.

## Packaging note from V1 (2026-09-26)

The POSIX build links FFmpeg 8.1.2 statically (LGPL 2.1 or later; V1's task file has why that is
compatible). Whatever packages the macOS app or a Linux build must ship FFmpeg's licence text
(`<build>/ffmpeg/LICENSE.txt`, which the build installs) and a pointer to the source: the release
tarball URL, the configure line in `Tools/ffmpeg-build-posix.sh`, and this repository for relinking.
Windows ships its DLLs with `Libraries/Source/FFmpeg/dist/LICENSE.txt` beside them already.
