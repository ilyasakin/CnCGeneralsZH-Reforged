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

Nothing has been installed outside `~/zhr-worker` so far, and nothing system-wide.
