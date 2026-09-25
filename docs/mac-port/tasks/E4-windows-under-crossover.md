# E4 — Windows under CrossOver

- **Milestone:** M1
- **Depends on:** nothing
- **Blocks:** D1's PRs 2-8 (before/after pixels on Windows), E1 (its gate is degraded without a Windows CRC)
- **Status:** blocked: stage 1 has no game executable to run (see below); stage 2 needs the user's acceptance of Microsoft's licence
- **Size:** tooling and measurement; no game code

## Why

Nothing in this plan is ever checked on Windows (`WINDOWS-DEBT.md`), and that stalls two tasks.
D1's PRs 2-8 need before/after pixels on Windows. E1's determinism gate is degraded because it has no
Windows replay CRC to compare against. This Mac has CrossOver 26.1, clang-cl and lld 21. If an
MSVC-compiled build of the game can play a replay headless here and print its CRC, E1 gets the half
it is missing, without a Windows machine.

## Stage 1: upstream's MSVC build, under CrossOver (2026-09-25)

**Set up.** Bottle `zh-e4`, template `win10_64`, created with `cxbottle --create`. The existing
bottles (`Steam`, `RA2RE`) were not touched. The Zero Hour data is at
`/Volumes/External/Games/cnc/zerohour` and was only listed, never written.

**What `v2.1.0` actually is.** The release has one asset, `CnCGeneralsZHReforged-Setup-1.1.4.exe`
(sha256 `4df5e385…98a3`). It is an NSIS 3.04, electron-builder installer for the **launcher**
(`zhreforged-launcher`, into `%LOCALAPPDATA%\Programs`), **not the game**. The game executable is
delivered through the release channel, which the launcher knows and this public repository
deliberately does not name (`vendor.sh`: *"This repository is public and does not name it"*).
Upstream publishes no build artifacts: its only CI workflow checks commit messages.

**The installer does not install under CrossOver.** `setup.exe /S` exits 2 (NSIS: aborted by the
script). Traced with `--debugmsg +reg,+file,+process`: before installing, electron-builder's script
asks PowerShell whether the launcher is already running (`Get-CimInstance Win32_Process | ? Path
-StartsWith ...zhreforged-launcher`). Wine's `powershell.exe` is a stub that answers "yes", and a
silent install that cannot close a running app aborts.

**So the replay question is unanswered.** There is no game executable here to run. The ways forward,
none of which is taken here:

1. **`ZHR_CHANNEL_URL`**, which `vendor.sh` already honours for the art. The same channel's
   `_versions.json` lists the game build (`["game"][0]`), so with the URL the v2.1.0 files could be
   fetched exactly as the launcher fetches them. This needs the user, who may know the URL. It was
   deliberately **not** extracted from the launcher binary: the project chose not to publish it.
2. **The launcher's own flow:** install it interactively and let it download the game. This needs
   the user's hands in the launcher's UI. Untested: whether the interactive install hits the same
   PowerShell check, and whether the Electron launcher runs under CrossOver at all.
3. **Stage 2:** our own cross-build, below.

**The method, ready for when there is an executable.** `replay-check.ps1`, reproduced without
PowerShell:

- **Live run:** `-headless -quickstart -noshellmap -multiInstance -noFPSLimit -maxframes N -logPrefix
  P -randommap S 2 128 -autoskirmish 2 -aidiff brutal -seed S -observer`.
- **Keep the replay:** move `%USERPROFILE%\Documents\Command and Conquer Generals Zero Hour
  Data\Replays\00000000.rep` aside as `determinismS.rep`.
- **Playback:** `-replay determinismS` with the same headless switches.
- **Compare** the last `HEADLESS CRC: 0x… at frame N` in each run's `<RunDir>\<P>DebugLogFile.txt`.

`-randommap` generates the map, so nothing but the base data is needed. `RELEASE_DEBUG_LOGGING`
defaults on, so a Release build writes that log. All of these switches are handled outside the
debug-only block of `CommandLine.cpp`; `-logPrefix` is read in `Debug.cpp`.

### What CrossOver cannot see: measured, where it could be

- **Not Windows.** It is x86_64 code, translated by Rosetta 2, over Wine rather than a Windows
  kernel. A CRC from here is evidence about that stack, not about Windows.
- **The CPU the game sees.** `cpuid` is an instruction, not a system call, so Wine does not intercept
  it: the game sees what Rosetta reports. Measured with an x86_64 probe under Rosetta on this M3 Pro,
  macOS 27.0: vendor **`GenuineIntel`**, brand `VirtualApple @ 2.50GHz`, family 6 model 0x2C, SSE
  through SSE4.2, **no AVX, AVX2 or FMA**. Microsoft's D3DX would therefore take its **Intel** body
  (decision 1, defect #7).
- **Which D3DX runs.** A fresh `win10_64` bottle has **Wine's builtin `d3dx9_43.dll`** in both
  `system32` and `syswow64`. v2.1.0 predates B17's Windows flip, so its simulation calls D3DX on the
  CRC path, and under the builtin those calls run **Wine's C implementation, not any of Microsoft's
  bodies**. A CRC from that setup would be a Wine-D3DX CRC. Microsoft's DLL is in the Steam folder
  (`_CommonRedist/DirectX/Jun2010/Jun2010_d3dx9_43_x64.cab`, `DXSETUP.exe`), and installing it is
  accepting Microsoft's redistributable licence. That is the user's call, as for stage 2, and it was
  not done.

## Stage 2: our own branch, cross-built with clang-cl (blocked on a licence)

**Blocked:** the Windows CRT and SDK headers and libraries are Microsoft's, and `xwin` fetches them
only with `--accept-license`. Accepting it is the user's decision. Nothing was downloaded.

Present on this machine: `/opt/homebrew/opt/llvm@21/bin/clang-cl` (21.1.8),
`/opt/homebrew/opt/lld@21/bin/lld-link`, `llvm-rc`, `llvm-mt` and `llvm-lib`. Not present: `xwin`.
The commands it would run (xwin's documented usage; not run):

```sh
xwin --accept-license --arch x86_64 splat --output "$HOME/.xwin"     # the licence step
X="$HOME/.xwin"
cmake -S GeneralsMD/Code -B build-win -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=AMD64 \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@21/bin/clang-cl \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@21/bin/clang-cl \
  -DCMAKE_LINKER=/opt/homebrew/opt/lld@21/bin/lld-link \
  -DCMAKE_RC_COMPILER=/opt/homebrew/opt/llvm@21/bin/llvm-rc \
  -DCMAKE_MT=/opt/homebrew/opt/llvm@21/bin/llvm-mt \
  "-DCMAKE_C_FLAGS=/imsvc $X/crt/include /imsvc $X/sdk/include/ucrt /imsvc $X/sdk/include/um /imsvc $X/sdk/include/shared" \
  "-DCMAKE_CXX_FLAGS=/imsvc $X/crt/include /imsvc $X/sdk/include/ucrt /imsvc $X/sdk/include/um /imsvc $X/sdk/include/shared" \
  "-DCMAKE_EXE_LINKER_FLAGS=/libpath:$X/crt/lib/x86_64 /libpath:$X/sdk/lib/um/x86_64 /libpath:$X/sdk/lib/ucrt/x86_64"
```

**Two more things it needs, which the licence alone does not settle:**
- **MIDL.** Configure has `find_program(MIDL_EXECUTABLE midl ... REQUIRED)` for the browser-dispatch
  headers. xwin brings the SDK's headers and libraries, not `midl.exe`. The candidates are Wine's
  `widl`, or `midl.exe` run under Wine, passed with `-DMIDL_EXECUTABLE=`.
- **The DirectX 8 SDK headers**, which only `vendor.ps1` fetches (`vendor.sh` skips them).

**And what it could not show even when it builds:** clang-cl is not MSVC. A cross-built binary's
replay CRC speaks for clang-cl's code generation, which is not the binary players run. For defect #7,
it would compare our portable D3DX path against itself, not against Microsoft's DLL.

## Done when

Stage 1: an MSVC-built game plays one replay headless under CrossOver twice, with the same `HEADLESS
CRC` both times, and the log records which `d3dx9_43.dll` was loaded. Stage 2: `build-win` builds our
branch, and the same replay check runs on its output.
