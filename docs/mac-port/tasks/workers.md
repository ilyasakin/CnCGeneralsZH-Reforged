# Workers: finer (macOS) and thinkerer (Linux)

The user's direction (2026-09-26): this Mac is for editing and git; builds, ctest, sweeps and game runs
go to two worker machines. Windows are fine there; **sound is not**: every run is `-headless` or
`-noaudio`, and the audio and video tests are excluded or on the null device.

**This file is the revert list.** The user wants every change on the workers undone at the end: the
accounts, sudo, the installs and the data copies. Every install and change is logged below, with what,
where and how to undo it.
- The accounts (`zhr` on both machines, with passwordless sudo) and the permission rules in this Mac's
  `~/.claude/settings.json` were made by the user and the PM, not by an agent. They are listed so that
  the revert covers them.

## finer (`ssh zhr@finer.local`): the macOS build and test host

M3 Pro, 11 cores, 36 GB, macOS 26.5.2 (older than this Mac's 27), 133 GB free at the start. The
firewall is off. Only the `finer` user has a GUI login; `zhr` has none.

Everything is under `/Users/zhr/zhr-worker` unless noted:

| what | where | how it got there | undo |
|:--|:--|:--|:--|
| the worker folder | `~/zhr-worker` | `mkdir` (-47) | `rm -rf ~/zhr-worker` removes everything below |
| CMake 3.31.6 | `~/zhr-worker/tools/cmake-3.31.6-macos-universal`, linked from `bin/cmake` and `bin/ctest` | Kitware's release tarball, `curl` from github.com/Kitware/CMake/releases (-47) | in the folder |
| Ninja 1.12.1 | `~/zhr-worker/bin/ninja` | `ninja-mac.zip` from github.com/ninja-build/ninja/releases (-47) | in the folder |
| `zheavy`, the one-heavy-job lock | `~/zhr-worker/bin/zheavy`, lock file `~/zhr-worker/.heavy.lock` | written by -47 | in the folder |
| the game data (the user's own install, 4.6 GB: `zerohour` and `generals`) | `~/zhr-worker/data` | `rsync -a` from this Mac's `/Volumes/External/Games/cnc` (-47) | in the folder |
| the vendored sources git ignores (4,114 files, SDL3, freetype, GameSpy, …) | `~/zhr-worker/vendor` | `rsync --files-from` of the ignored files of the zhr2-B17 worktree (-47) | in the folder |
| the art archives (`Reforged*.big`, 1.65 GB) | `~/zhr-worker/art` | `rsync -a` (-47) | in the folder |
| the repository | `~/zhr-worker/repo` (a clone of `feature/mac-port`), from `~/zhr-worker/zhr.bundle` | `git bundle` here, `scp` there, `git clone` (-47). Nothing was pushed to any public remote | in the folder |
| worktree for -47 | `~/zhr-worker/wt-47`, branch `agent-47` | `git worktree add` (-47) | in the folder |
| build dir for -47 | `~/zhr-worker/build-47` (Release, `ZH_GAME_DATA=~/zhr-worker/data`) | `cmake`/`ninja` via `zheavy` (-47) | in the folder |
| worktree for the PM | `~/zhr-worker/wt-pm`, detached at `feature/mac-port`, vendor and art cloned in; `build-pm` once the FFmpeg fix is merged | `git worktree add --detach` (-47) | in the folder |
| incoming bundles | `~/zhr-worker/bundles/`, `~/zhr-worker/zhr.bundle`, `~/zhr-worker/w1.bundle` | `scp` (-47) | in the folder |
| a Metal / window-server probe | `~/zhr-worker/tools/gpuprobe.m` and its binary | written and compiled by -47 (it opens no window) | in the folder |
| a tree hasher | `~/zhr-worker/bin/hashtree.py` (path, size and BLAKE2 per file) | written by -47, for the data check | in the folder |
| the README | `~/zhr-worker/README` | written by -47 | in the folder |
| -a9's offscreen probes (SDL3 GPU and raw Metal, no window) | `~/zhr-worker/probe-a9` | written and compiled by -a9 with clang against build-47's `libSDL3.a` | in the folder; to be deleted once `-offscreen` is validated there |
| -a9's PERF1 folder: bundles, scripts, logs, captures, the install's hash list, and a symlink farm of `data/zerohour` (510 links, real directories) | `~/zhr-worker/perf-a9` | `scp` of bundles and scripts, then `setup.sh` and `matrix.sh` (-a9). Nothing was pushed anywhere | in the folder |
| worktree for -a9 | `~/zhr-worker/wt-a9`, branch `perf1-finer-run`; the repository also holds `perf1-finer-nofix3` (fix 3 reverted, for the A/B). Vendor and art cloned in; its SDL3 patched by `vendor.sh` | `git worktree add` (-a9) | in the folder, and `git -C ~/zhr-worker/repo worktree prune` and `branch -D perf1-finer-run perf1-finer-nofix3` |
| build dir for -a9 | `~/zhr-worker/build-a9` (Release, `ZH_GAME_DATA=~/zhr-worker/data`), with copies `generals-fix3` and `generals-nofix3` | `cmake`/`ninja` via `zheavy` (-a9) | in the folder |

Nothing has been installed outside `~/zhr-worker` so far, and nothing system-wide.

### finer: what was measured

- **The data copy:** all 1,786 entries identical to this Mac's `/Volumes/External/Games/cnc`, by path,
  size and BLAKE2 (`hashtree.py` on both machines).
- **The first build found a defect in our CMake.** On finer, CMake chose
  `/Library/Developer/CommandLineTools/usr/bin/cc` rather than the `/usr/bin` shim. Only the shim finds an
  SDK by itself, so FFmpeg's configure failed to link ("library 'System' not found") and then to build
  its host tools ("ctype.h not found"). The fix, on `feature/mac-port-workers`: the FFmpeg step runs
  with `SDKROOT` set to CMake's sysroot, and the build script passes its flags as `--extra-ldflags` as
  well.
- **The build:** `build-47` from 9020fd14 builds green: 179 s first time (with the FFmpeg failure),
  44 s after the fix.
- **ctest on finer (macOS 26.5.2),** `-E` the four audio and video tests, 1,215 s: 83 tests, 0 failed,
  11 skipped.
  - Six SDL GPU selfchecks skip: no window server as zhr (below).
  - `replay_check_app`: no bundle built there.
  - `ffprogram_values_check`: no MinGW.
  - `arch_differential`: its x86_64 side needs Rosetta or a cross toolchain there.
  - The two D3DX oracles: opt-in, as everywhere.
- **The first runs on an older macOS agree with this Mac's exactly:**
  - `replay_check`: seed 0 at frame 1200 is 0x0177BEF6, and seed 1 at frame 12000 is 0x7C7DBA69 (E1's).
  - `net_check`: 0x3453DF90 at frame 1800, with its controls.
  - `test_lan_broadcast` and `macos_app_check` pass.
- **Windows and Metal as zhr over ssh** (`tools/gpuprobe`):
  - Metal works: the M3 Pro, a command buffer committed and completed.
  - There is no window server: no display, no session, no GUI launchd domain. So an SDL window, even
    hidden, cannot be made as zhr, and SDL's GPU device cannot either: its Metal backend needs a Cocoa
    view (-a9).
  - The PM chose -a9's `-offscreen` mode (option 3). A GUI login for zhr (option 1) is with the user;
    running in finer's own session (option 2) was declined.

