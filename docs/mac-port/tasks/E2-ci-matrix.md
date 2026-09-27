# E2 — CI matrix

- **Milestone:** M5
- **Depends on:** E1
- **Blocks:** nothing
- **Status:** the fleet gate exists (`ci-matrix.sh`, below); the hosted CI this file first planned does not
- **Size:** new workflow files; no application code

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** The matrix gets a Linux job (x86_64 and arm64, GCC and Clang) beside macOS arm64 and Windows. Until there is CI, the Linux container check in `docs/mac-port/README.md` decision 3 is the stand-in.

## What exists: the fleet gate, `ci-matrix.sh` (2026-09-27, -18)

The PM's per-merge gate, run by hand until now, is one command on this Mac. It builds and runs nothing here
(the user's rule: this Mac edits and runs git; the workers build and test):

    ./ci-matrix.sh feature/mac-port --vm-known-hosts <file> \
        --expect "0@1200:0x0177BEF6,0@12000:0x5273770F,1@12000:0x830467DB"

- It bundles the branch and sends it to the three workers at once: finer (macOS arm64, clang), thinkerer
  (Linux x86_64, GCC) and the Windows VM (MSVC x64, reached through thinkerer).
- finer and thinkerer check it out into `~/zhr-worker/ci` (worktree `ci/wt`, build `ci/build`, logs
  `ci/*.log`), build, run ctest and each E1 run through `replay-check.sh`. Every heavy step goes through
  `zheavy`, so the gate queues behind other agents' jobs rather than competing with them.
- The VM runs `windows-ci.ps1` from `C:\zhr-worker\wt-pm-win`: build, ctest, the GPU tests in the desktop
  session, and E1 on a farm of the data.
- Two gates never share a host. The POSIX hosts hold `~/zhr-worker/ci/lock` (an flock) for the whole run,
  and the VM's part waits while any other `windows-ci.ps1` runs there.
- One verdict, exit 0 only when all of these hold:
  - each host built and passed ctest;
  - each E1 run played back to the same world and left the data unchanged (rule 9);
  - every host gave the same CRC for each run, and the pinned one where `--expect` pins it.
- The cross-host comparison is item 2 of the plan below: it is where Windows, macOS and Linux have to
  agree. A host that could not run a step fails, never passes.
- Skipped and disabled ctest tests are listed by name under each host, so a test that stops running is
  seen. They do not fail the gate by themselves, since each host skips some by design:
  - finer: `replay_check_app` (no bundle built), `ffprogram_values_check` (no MinGW), `arch_differential`
    (no Rosetta cross side), the two D3DX oracles (opt-in).
  - thinkerer: `data_gone_check` (it needs macOS's hdiutil), `ffprogram_values_check`, `arch_differential`
    (Darwin only), the two D3DX oracles.
  - The VM skips none. Its GPU tests are left out of session 0's ctest and run in the desktop part instead.
- Pins belong to a commit: upstream gameplay data moves them (E1's baselines in `docs/mac-port/README.md`).
  From 62440ae8 (defect #34's fix) seed 0 at 12000 frames is 0x5273770F. Before it, that run crashed and
  was left out.
- The audio and video tests are excluded on every host: no sound on the workers.

What it is not: the hosted CI planned below. It runs when the PM runs it, on machines the user lent for the
port, and `docs/mac-port/tasks/workers.md` lists what it leaves on them (`~/zhr-worker/ci` on the two POSIX
hosts; the bundles and `ci-host.ps1` in `C:\zhr-worker\bundles` on the VM). It arms nothing: a fix is still
proven by putting its bug back (the "Do not" below).

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
