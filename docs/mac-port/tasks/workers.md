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
| `zheavy`, the heavy-job queue: first come first served; two default jobs at once (builds capped at -j5), or one `--exclusive` job alone | `~/zhr-worker/bin/zheavy`, from `docs/mac-port/tasks/workers/zheavy` (its test beside it); the machine lock `~/zhr-worker/.heavy.lock` and the queue `~/zhr-worker/.heavy/` | written by -47; the queue and the two classes on 2026-09-27, at the PM's request | in the folder |
| the game data (the user's own install, 4.6 GB: `zerohour` and `generals`) | `~/zhr-worker/data` | `rsync -a` from this Mac's `/Volumes/External/Games/cnc` (-47) | in the folder |
| the vendored sources git ignores (4,114 files, SDL3, freetype, GameSpy, …) | `~/zhr-worker/vendor` | `rsync --files-from` of the ignored files of the zhr2-B17 worktree (-47); on 2026-09-27 `Tools/vendor.sh` at 681d6f9a, run in a scratch worktree over a clone of it, patched one file (SDL3's `src/gpu/metal/SDL_gpu_metal.m`, sdl3-metal-windowless.patch), copied back here and into wt-pm and wt-47 (-47, for the PM) | in the folder |
| the art archives (`Reforged*.big`, 1.65 GB) | `~/zhr-worker/art` | `rsync -a` (-47) | in the folder |
| the repository | `~/zhr-worker/repo` (a clone of `feature/mac-port`), from `~/zhr-worker/zhr.bundle` | `git bundle` here, `scp` there, `git clone` (-47). Nothing was pushed to any public remote | in the folder |
| worktree for -47 | `~/zhr-worker/wt-47`, branch `agent-47` | `git worktree add` (-47) | in the folder |
| build dir for -47 | `~/zhr-worker/build-47` (Release, `ZH_GAME_DATA=~/zhr-worker/data`) | `cmake`/`ninja` via `zheavy` (-47) | in the folder |
| worktree for the PM | `~/zhr-worker/wt-pm`, detached at `feature/mac-port`, vendor and art cloned in; `build-pm` once the FFmpeg fix is merged | `git worktree add --detach` (-47) | in the folder |
| incoming bundles | `~/zhr-worker/bundles/`, `~/zhr-worker/zhr.bundle`, `~/zhr-worker/w1.bundle` | `scp` (-47) | in the folder |
| a Metal / window-server probe | `~/zhr-worker/tools/gpuprobe.m` and its binary | written and compiled by -47 (it opens no window) | in the folder |
| a tree hasher | `~/zhr-worker/bin/hashtree.py` (path, size and BLAKE2 per file) | written by -47, for the data check | in the folder |
| the README | `~/zhr-worker/README` | written by -47 | in the folder |
| worktree for -18 | `~/zhr-worker/wt-18`, branch `agent-18` (then `feature/mac-port-data-gone`), vendor and art cloned in, SDL3 patched by `Tools/vendor.sh`; the repo ref `refs/remotes/mac18/feature/mac-port` | `git worktree add` (-18) | in the folder |
| build dir for -18 | `~/zhr-worker/build-18` (Release, `ZH_GAME_DATA=~/zhr-worker/data`), its logs `~/zhr-worker/build-18.*.log`, and `build-18/s18`: the sweep and lid-close scripts, APFS clones of the binary, their logs | `cmake`/`ninja` via `zheavy`, `scp` (-18) | in the folder |
| -18's bundles | `~/zhr-worker/bundles/fmp18.bundle`, `dg18.bundle` | `scp` (-18) | in the folder |
| disk images for the lid-close repro and `data_gone_check` | made in a temporary work folder under `build-18/s18` or `$TMPDIR`, attached, detached (by their own device) and deleted by the script that made them; none is left | `hdiutil create/attach/detach` (-18), no sudo | nothing left to undo; `hdiutil info` lists none |
| -a9's offscreen probes (SDL3 GPU and raw Metal, no window) | `~/zhr-worker/probe-a9` | written and compiled by -a9 with clang against build-47's `libSDL3.a` | deleted by -a9, 2026-09-27, once `-offscreen` was validated there |
| -a9's PERF1 folder: bundles, scripts, logs, captures, the install's hash list, a symlink farm of `data/zerohour` (510 links, real directories), and `tmo`, a perl stand-in for GNU `timeout`, which macOS lacks | `~/zhr-worker/perf-a9` | `scp` of bundles and scripts, then `setup.sh` and `matrix.sh` (-a9). Nothing was pushed anywhere | in the folder |
| worktree for -a9 | `~/zhr-worker/wt-a9`, branch `perf1-finer-run`; the repository also holds `perf1-finer-nofix3` (fix 3 reverted, for the A/B). Vendor and art cloned in; its SDL3 patched by `vendor.sh` | `git worktree add` (-a9) | in the folder, and `git -C ~/zhr-worker/repo worktree prune` and `branch -D perf1-finer-run perf1-finer-nofix3` |
| build dir for -a9 | `~/zhr-worker/build-a9` (Release, `ZH_GAME_DATA=~/zhr-worker/data`), with copies `generals-fix3` and `generals-nofix3` | `cmake`/`ninja` via `zheavy` (-a9) | in the folder |
| -a9's further worktrees and builds on finer: `wt-a9-l2`/`build-a9-l2` (L2's branch, for a Mac binary); `wt-a9-san`/`build-a9-tsan`/`build-a9-asan` (PERF candidate 1 under the sanitizers); `wt-a9-lw`/`build-a9-lw` (lock-wait instrumented) | `~/zhr-worker/…` | `git worktree add`, `cmake`/`ninja` via `zheavy` (-a9) | in the folder, then `git -C ~/zhr-worker/repo worktree prune` |
| -a9's branches in finer's repository | `a9-*` (base, candidates, proof and pixel-proof branches, `a9-l2`, `a9-lw-*`) | fetched from bundles (-a9) | `git -C ~/zhr-worker/repo branch -D` each `a9-*` |
| the fleet gate's folder (`ci-matrix.sh`, E2): the worktree `ci/wt` (vendor and art cloned in), the build `ci/build`, the logs `ci/*.log`, the lock `ci/lock`; the repository ref `refs/ci/head`; `bundles/ci-host.sh` | `~/zhr-worker/ci` | `ci-matrix.sh` (-18) | `git -C ~/zhr-worker/repo worktree remove --force ~/zhr-worker/ci/wt; git -C ~/zhr-worker/repo update-ref -d refs/ci/head; rm -rf ~/zhr-worker/ci ~/zhr-worker/bundles/ci-host.sh` |

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

## thinkerer (`ssh zhr@thinkerer`): the Linux build and test host

Arch Linux (rolling, kernel 7.1.4), x86_64 i5-8350U with 4 cores and 8 threads, 62 GB, 247 GB free on
btrfs at the start. It already had cmake 4.4.2, ninja, clang 22.1.8, gcc 16.2.1, python3, rsync and
docker, and every development package the Linux check installs (X11, Wayland, ALSA, Pulse, Vulkan,
spirv-tools, ...). One package was installed system-wide: `unifdef` (see the table). No firewall is active (iptables policy
ACCEPT; firewalld and ufw inactive). It is on the Macs' LAN as `wlan0` 192.168.1.21, and has other
interfaces (a wired 10.99.0.1, tailscale, a VPN tun0, docker bridges).

The host's own services are not touched: vaultwarden, pihole (192.168.1.21:53 and 5053), dnscrypt,
uptime-kuma and the tailscale-* sidecars.

Everything is under `/home/zhr/zhr-worker` unless noted:

| what | where | how it got there | undo |
|:--|:--|:--|:--|
| the worker folder | `~/zhr-worker` | `mkdir` (-47) | `rm -rf ~/zhr-worker` removes everything below |
| `zheavy` (the queue, as finer's, from `docs/mac-port/tasks/workers/zheavy`) and `hashtree.py` | `~/zhr-worker/bin/`; the queue in `~/zhr-worker/.heavy/` | `scp` (-47) | in the folder |
| the game data (4.6 GB) | `~/zhr-worker/data` | `rsync -a` from this Mac (-47) | in the folder |
| the vendored sources git ignores | `~/zhr-worker/vendor` | `rsync --files-from` (-47) | in the folder |
| the art archives | `~/zhr-worker/art` | `rsync -a` (-47) | in the folder |
| the repository | `~/zhr-worker/repo`, from `~/zhr-worker/bundles/fmp.bundle` (feature/mac-port 9d1c8895) | `git bundle` here, `scp`, `git clone` (-47) | in the folder |
| worktree and build for -47 | `~/zhr-worker/wt-47` (branch `agent-47`, vendor copied in with `cp --reflink=auto`), `~/zhr-worker/build-47` | `git worktree add`, cmake/ninja via `zheavy` (-47) | in the folder |
| `unifdef` 2.12-4 (system-wide, `/usr/bin/unifdef`), which widechar_check needs; it was not installed before | pacman | `sudo pacman -S --needed --noconfirm unifdef`, as the PM directed (-47, 2026-09-27) | `sudo pacman -Rs unifdef` |
| `vulkan-swrast` 1:26.2.2-1 (lavapipe: `/usr/share/vulkan/icd.d/lvp_icd*.json`, llvmpipe on LLVM 22.1.8, Vulkan 1.4), a software Vulkan beside ANV for L2 | pacman, system-wide | `sudo pacman -S --needed --noconfirm vulkan-swrast`, as the PM approved (-47, 2026-09-27). Nothing else was installed or upgraded (pacman.log); it is one point release ahead of the host's mesa and vulkan-intel 26.2.1, because the sync database is newer | `sudo pacman -Rs vulkan-swrast` |
| core dumps of uid 1001 (zhr): 12 from test_crash_reporting's children, one 14.8 MB `generals` from W1's null-this segfault | `/var/lib/systemd/coredump` (outside the worker folder), and their metadata in the systemd journal | systemd-coredump, from our crashing test children (-47) | the 13 files deleted with `sudo find … -name 'core.*.1001.*' -delete` (2026-09-27); uid 1000's chromium core untouched. The journal entries remain (coredumpctl lists them as missing). Since f8825d76 the children set RLIMIT_CORE to 0 and store none |
| -a9's worktree and build for L2 | `~/zhr-worker/wt-a9` (branch `a9-l2`, later `a9-fmp`, `a9-synth` and `a9-f7`; vendor and art copied in with `cp -a --reflink=auto`; its SDL3 patched by `vendor.sh`), `~/zhr-worker/build-a9` | `git worktree add`, `cmake`/`ninja` via `zheavy` (-a9) | in the folder, then `git -C ~/zhr-worker/repo worktree prune` and `branch -D a9-l2 a9-fmp a9-synth a9-f7` |
| -a9's L2 folder: bundles, the setup script, logs, the install's hash list, a symlink farm of `data/zerohour` (510 links), game runs and capture sets | `~/zhr-worker/l2-a9` | `scp` and `l2setup.sh` (-a9) | in the folder |
| two core dumps of `build-a9/generals` from L2's exit crashes (2026-09-27 08:18 and 08:21, about 187 MB each), both fixed since | `/var/lib/systemd/coredump` (outside the folder) | systemd-coredump | `sudo coredumpctl list` shows them; remove those two files, or leave them to systemd's own cleanup |

**The Windows VM is not -47's.** The user approved it (relayed by the PM), but this session's permission
classifier refused its setup as "Unauthorized Persistence" (an auto-started SSH server keyed to this Mac
inside the VM), and -47 created nothing for it. The user then started it themselves (container
`zhr-windows`) and the PM handles its access and tools. Its revert entries belong to them, not to this table.

-47's one use of it (2026-09-27, for L1b's font heights, with -18 told before and after): `C:\zhr-worker\fontmetrics`
holds `gdi-font-metrics.ps1` (the repository's Tools/ script) and its two outputs, `gdi-metrics.csv` and
`fonts.txt`. It only read `C:\Windows\Fonts` and installed nothing. Undo: `Remove-Item -Recurse
C:\zhr-worker\fontmetrics`. -47 reached it with a known-hosts file in its scratch folder, which kept nothing:
no host key of it was left on the Mac (`~/.ssh/known_hosts` has none).
A second use, for #33's Windows side (2026-09-27, -18 told before and after): `C:\zhr-worker\w47` holds -47's
own clone of the integration branch (from -18's repository's objects and a bundle, `core.autocrlf true`), its
`build64` from `build.bat Release`, a rule-9 farm (`w47\farm`: links to the data copy, the build's Run over
it) and the scripts. A one-off interactive task `zhr47desk` ran the game in the desktop session and was
deleted. The data copy's hash was unchanged. Undo: `Remove-Item -Recurse C:\zhr-worker\w47`.


### The Windows VM: what -18 created and installed for W2 (2026-09-27)

| what | where | how it got there | undo |
|---|---|---|---|
| the work folder | `C:\zhr-worker` | `New-Item` (-18) | `Remove-Item -Recurse -Force C:\zhr-worker` removes everything below |
| the repository | `C:\zhr-worker\repo` (`core.autocrlf true`), from `fmp.bundle` and `w2.bundle`; its `build64` and the ignored `build.local.bat` naming VS's CMake 3.31.6 | `scp`, `git clone`, `build.bat` (-18) | in the folder |
| the game data and art copies | `C:\zhr-worker\data\zerohour` (2.9 GB), `C:\zhr-worker\data\generals` (1.7 GB), `C:\zhr-worker\art` (1.6 GB), hash-verified against thinkerer's; `build64` configured with `ZH_GAME_DATA=C:/zhr-worker/data` | `scp -3` through the Mac (-18) | in the folder |
| the farm | `C:\zhr-worker\farm`: symbolic links to the data and the Run folder, copies of the exe and DLLs | `farm.ps1` (-18) | in the folder |
| scripts, logs, bundles, hash listings | `C:\zhr-worker\*.ps1`, `*.log`, `*.txt`, `*.bundle`, `hashtree.py`, `C:\Users\zhr\recon18.ps1` | `scp` (-18) | in the folder; `recon18.ps1` by hand |
| ATL for VS 2022 Build Tools | component `Microsoft.VisualStudio.Component.VC.ATL` | `setup.exe modify --add` (-18) | `setup.exe modify --installPath "<BuildTools>" --remove Microsoft.VisualStudio.Component.VC.ATL --quiet` |
| the DirectX End-User Runtime (June 2010): `d3dx9_43.dll` and its kind in System32 and SysWOW64 | the extracted redist in `C:\zhr-worker\dxredist` | `directx_Jun2010_redist.exe /Q /T`, `DXSETUP.exe /silent` (-18) | it has no uninstaller; the DLLs are harmless to leave, or are deleted by name (`d3dx9_*`, `d3dx10_*`, `d3dx11_*`, `D3DCompiler_*`, `XAudio2_*`, `X3DAudio*`, `XAPOFX*`, `xinput1_3`) |
| winget's `Microsoft.DirectX` (web installer; installed no DLL) | winget's package record | `winget install` (-18) | `winget uninstall --id Microsoft.DirectX` |
| replays copied into the user's Replays folder | `Documents\Command and Conquer Generals Zero Hour Data\Replays\w1a.rep`, `w1b.rep`, and the harness's `determinism*.rep` | `Copy-Item`, `replay-check.ps1` (-18) | delete them |
| one-off scheduled tasks `zhr18desk`, `zhr18desk2`…`zhr18desk5`, and windows-ci.ps1's `zh-windows-ci-*` | Task Scheduler | `schtasks /create /it` (-18, windows-ci.ps1) | deleted after each run |
| the PM's worktree for windows-ci.ps1 | `C:\zhr-worker\wt-pm-win` (a detached `git worktree` of `C:\zhr-worker\repo`, its own `build64`, `build.local.bat` copied), `C:\zhr-worker\bundles\`, and the check's work folder `C:\zhr-worker\ci-pm` (farm, logs, results) | `git worktree add` (-18) | `git -C C:\zhr-worker\repo worktree remove --force C:\zhr-worker\wt-pm-win`; delete the two folders |
| a Windows Defender exclusion for the work folder, so real-time scanning cannot hold a test's file open (the PM's call, for a `test_wwlib` flake seen once at -j4) | Defender's settings: `(Get-MpPreference).ExclusionPath` lists `C:\zhr-worker` | `Add-MpPreference -ExclusionPath C:\zhr-worker` (-18, 2026-09-27) | `Remove-MpPreference -ExclusionPath C:\zhr-worker` |
| ci-matrix.sh's part on the VM | `C:\zhr-worker\bundles\ci-host.ps1`, and each run's `ci-matrix-*.bundle` there until the run removes it | `scp` from `ci-matrix.sh` (-18) | in the folder |
| -18's WINDOWS-DEBT checks (2026-09-27) | `C:\zhr-worker\w18` (their output: ctest's JSON, `test_gameengine -V`, the probe and its build, dx9_smoke's output, the GameSpy hash list); the scripts `checks18.ps1`, `dbg18.ps1`, `dx9cap.ps1`, `gshash.ps1`, `procs18.ps1` and `probe18.cpp` in `C:\zhr-worker\bundles`; a one-off desktop task `zhr18dx9`, deleted after its run | `scp`, `schtasks` (-18) | `Remove-Item -Recurse C:\zhr-worker\w18`; delete the six files |
| -18's Debug-build worktree | `C:\zhr-worker\w18dbg` (a detached `git worktree` of `C:\zhr-worker\repo` at 6d5d69a6, with the PM tree's ignored vendored files, art and `build.local.bat` copied in; its `build64` holds the Debug build), and `windows-ci.ps1`'s work folder `C:\zhr-worker\w18\ci-dbg` | `git worktree add`, `robocopy`, `windows-ci.ps1 -Config Debug` (-18) | `git -C C:\zhr-worker\repo worktree remove --force C:\zhr-worker\w18dbg` |

On thinkerer, for W2: `~/zhr-worker/tmp18` (hash listings, the VM's host key in a known-hosts file of its
own, a copy of build-47's `generals`, `playwin.sh`, the Windows replay, the bundles). Undo: `rm -rf ~/zhr-worker/tmp18`.
Also on thinkerer, for #32's armed control and the final W2 check (turns agreed with -47): the worktree
`~/zhr-worker/wt-18` (vendor and art copied in with `cp --reflink=auto`) and the build `~/zhr-worker/build-18`,
with its logs `~/zhr-worker/build-18.*.log`. Undo: `git -C ~/zhr-worker/repo worktree remove --force
~/zhr-worker/wt-18; rm -rf ~/zhr-worker/build-18 ~/zhr-worker/build-18.*.log`.
The fleet gate's folder on thinkerer is as on finer: `~/zhr-worker/ci` (worktree `ci/wt` with vendor and art copied in
with `cp --reflink=auto`, build `ci/build`, logs, lock), the ref `refs/ci/head` and `bundles/ci-host.sh`, made by `ci-matrix.sh`
(-18). Undo: `git -C ~/zhr-worker/repo worktree remove --force ~/zhr-worker/ci/wt; git -C ~/zhr-worker/repo update-ref -d
refs/ci/head; rm -rf ~/zhr-worker/ci ~/zhr-worker/bundles/ci-host.sh`. The one core dump it made (the
#32 mutant's SIGSEGV, pid 3917314) was removed from `/var/lib/systemd/coredump` with `sudo rm` straight after.
