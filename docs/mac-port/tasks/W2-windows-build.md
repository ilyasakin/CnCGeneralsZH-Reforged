# W2: the fork's first MSVC build, on the Windows 11 VM (-18, 2026-09-27)

Every row of `WINDOWS-DEBT.md` claims something about what MSVC and Windows do. Until W2, no Windows build
of the fork existed to check those claims. W2 made one and measured the claims against it.

**Result:** the build is green. On the E1 seeds, W1's LAN match and a Windows replay played on Linux,
Windows x64 (MSVC) gives exactly the CRCs macOS arm64 and Linux x86_64 give. That is M2's acceptance.

## The machine

- **The VM:** Windows 11 Pro 25H2 (26200) on thinkerer, set up by -47: unactivated, 4 vCPU, 16 GB, no
  audio device.
- **Access:** reached as `ssh -J zhr@thinkerer -p 2222 zhr@127.0.0.1`, key only. The shell is Windows
  PowerShell 5.1. Send PowerShell as a script file (scp it, then `powershell -File`); inline quoting through
  ssh breaks.
- **Toolchain:**
  - Visual Studio 2022 Build Tools, cl 19.44.35229 x64.
  - Git 2.55.0, Python 3.12 and Ninja 1.13.2 from winget.
  - CMake: winget's is 4.4.3, but the build uses the 3.31.6 that VS Build Tools ships, set in the ignored
    `build.local.bat`. That is what a Windows developer with VS 2022 gets, and it sidesteps CMake 4's
    removal of `cmake_minimum_required` below 3.5.
- **The tree:** `C:\zhr-worker\repo`, cloned from a bundle with `core.autocrlf true`, which gives CRLF
  sources, as the repository's `.gitattributes` expects. A clone whose `autocrlf` was switched off afterwards
  showed 1012 files as modified.
- **The build:** `build.bat Release` (runs `vendor.ps1`, then the Visual Studio 2022 generator, into
  `build64`).

## Windows prerequisites this found (environment, not code)

- **ATL** (`Microsoft.VisualStudio.Component.VC.ATL`): `PreRTS.h` includes `atlbase.h`. VS 2022's Desktop
  C++ workload installs ATL by default; Build Tools installed with VCTools only do not. Added with the VS
  installer: `setup.exe modify --add Microsoft.VisualStudio.Component.VC.ATL`.
- **The DirectX End-User Runtime (June 2010)**, for `d3dx9_43.dll`, which `d3dx9runtime.cpp` binds at run
  time (d3d8to9 needs it too). Windows 11 does not ship it.
  - winget's `Microsoft.DirectX` reports success but installs no DLL.
  - Microsoft's full `directx_Jun2010_redist.exe` does: extract with `/Q /T:<dir>`, then run
    `DXSETUP.exe /silent`.
  - Without it every FVF size is 0, and test_ww3d2 fails 12 checks.

## The build's errors, classified against WINDOWS-DEBT

63 errors on the first pass, then 19 more once ATL was in. All are fixed on `feature/mac-port-W2-msvc`; every
fix is identical on Windows or test-only.

| error | cause | predicted? | fix |
|---|---|---|---|
| `atlbase.h` not found (44) | environment | - | ATL, above |
| `Download.cpp`: `mkdir` takes 1 argument, `strncasecmp` unknown | B3 renamed the calls, but the file never includes `Platform/MSVCCompat.h`, so the shim was bypassed | **yes: row 61** ("if the shim were bypassed … the build fails loudly") | include the shim |
| `sdl3target.h`: `std::max` C2589 | windows.h's `max` macro | no (row 154, D3) | `(std::max)` |
| `mpu.cpp`: `abs` ambiguous | EA's `abs(3*freq - total)` on DWORD (unsigned long); `MSVCCompat.h` (B3) now declares `<stdlib.h>`'s long and long long overloads first | no | `abs((int)(…))`: int and long are both 32 bits on Windows, so either old overload gave these bits |
| `profile_funclevel.cpp`: `testEvent` not a member | B14 (row 99) removed `ProfileFastCS::testEvent` and left its Windows-only definition, which made an unnamed event nothing waited on | no | the definition removed |
| `WorldHeightMap.cpp`: C4101 unused `j` (an error in that target) | T1 moved the cell loop into `parseBlendTileCells` | no | `j` removed |
| `gameenginedevice`: SDL3 headers, `SdlDisplays.h`, `DIKeyCodes.h`'s `#error` | the Windows glob took in `Source/SdlDevice/`; only `PosixDevice/` was excluded | no | `SdlDevice/` excluded too |
| `test_clock`: `timeGetTime` unresolved | the test links no winmm | - (test) | winmm on Windows |
| widechar tests: `htonl` unresolved | they build `XferCRC.cpp` themselves and link no ws2_32 | - (test) | ws2_32 on Windows |
| `gametext_csf`, `test_premain_*`: `GameEngine.lib` not found | `GameMemory.cpp`'s EA `#pragma comment(lib, "GameEngine")`, while these tests build it without the library | - (test) | `/NODEFAULTLIB:GameEngine.lib` |

## The Windows ctest

- **Final:** 47 of 50 pass in the ssh session. The other 3 (`dx9_smoke`, `dx9_smoke_msaa`, `test_dx11device`)
  pass in the desktop session (below). 2 skips wait for `ZH_GAME_DATA` on the Windows configure.
- **What the reds were:**
  - `test_ww3d2`: the missing DirectX runtime (above).
  - `test_ffvertex`: **a stale test.**
    - It was registered only inside `if(ZH_PLATFORM_WINDOWS)`, so no POSIX run executed it.
    - Defects #23 (0fa58343, each light's own ambient) and #26 (6f9237eb, `D3DMCS_COLOR2` reads the second
      vertex colour) changed the shared generator to D3D9's rules.
    - Its two checks now follow #23 and #26. It is registered on every platform, since `ffvertex` builds
      everywhere, so it cannot go stale that way again.
  - `widechar_format_selfcheck`: **MSVC measured.** See the widechar section.
  - `arch_differential` and `d3dx_oracle`: shell scripts registered on Windows ("Process not started").
    Now POSIX only.
- **Session 0 has no display.** An ssh session is session 0, so a D3D device cannot be made there, and
  Windows `-headless` still makes one (the first CRC try died "ERROR:D3DFailureMessage", with "Unable to
  acquire keyboard device"). The VM logs `zhr` in to a desktop (session 1). To run a GPU test or a game
  there, use a one-off interactive scheduled task, and delete it afterwards:

  ```
  schtasks /create /tn <name> /tr "powershell -NoProfile -ExecutionPolicy Bypass -File <script>" /sc once /st 23:59 /it /ru zhr /f
  schtasks /run /tn <name>
  (poll the script's own output file)
  schtasks /delete /tn <name> /f
  ```

## The widechar measurement

- **What MSVC does:** `_vsnwprintf(out, 8, L"%s", <longer>)` writes all 8 units, returns -1, and writes
  **no terminator**. The POSIX funnel wrote 7 units and a terminator.
- **The callers:** there are four, all passing one less than their buffer. `UnicodeString::format` and
  `format_va` throw on a negative return. The two `InGameUI::message` variants terminate the last unit
  themselves. So MSVC's fill is safe there, and a truncated in-game message showed one more character on
  Windows than on the Mac.
- **The fix:** the funnel now follows MSVC off Windows (all `outCount` units, unterminated, -1), and the test
  compares exactly `outCount` units with a guard unit.
- **What stays different:** the header's one deliberate difference, output of exactly `outCount`, keeps
  MSVC's units and reports truncation's sign (MSVC returns `outCount` and leaves an overread for
  `format_va`).

## THE CRC run (pinned to 8373eea5, the commit the Mac, Linux and W1 numbers were made on)

- **The data:** a copy of the game data, `C:\zhr-worker\data\zerohour`, came from thinkerer with `scp -3`.
  Its `hashtree.py` listing is identical to thinkerer's: 1070 entries, and the 3 Reforged art archives match.
- **The farm (rule 9):** `C:\zhr-worker\farm` holds symbolic links to every data file, with the build's Run
  folder over it, as Windows' one-folder layout has the fork's files over the install. The exe and its DLLs
  are copied. The data copy was hashed before and after, and was unchanged.
- **The run:** `replay-check.ps1`, run in-process (`& .\replay-check.ps1 -Seeds 0,1`). Through
  `powershell -File` a `-Seeds 0,1` arrives as one seed. It ran in the desktop session.

| run | Windows x64 (MSVC 19.44) | macOS arm64 / Linux x86_64 |
|---|---|---|
| seed 0, frame 12000 | 0xE896DEF3, playback the same | 0xE896DEF3 |
| seed 1, frame 12000 | 0x7C7DBA69, playback the same | 0x7C7DBA69 |
| seed 0, frame 1200 | 0x0177BEF6, playback the same | 0x0177BEF6 |
| W1's LAN match replay, from finer | 0x341D0C61 at frame 3000, 0 CRC mismatches, aligned from frame 0 | 0x341D0C61 |
| W1's LAN match replay, from thinkerer | 0x341D0C61 at frame 3000, 0 CRC mismatches | 0x341D0C61 |
| the Windows seed 0 replay, played by the Linux build | - | 0x0177BEF6 at frame 1200, 0 mismatches |

## Open measurements

- **The game's data gone:** `LocalFile.cpp` names ERROR_DEVICE_NOT_CONNECTED, ERROR_NOT_READY,
  ERROR_DEV_NOT_EXIST and ERROR_FILE_INVALID, unmeasured. Measure them with `data_gone_check`'s detach done
  against a VHD (diskpart create, attach and detach vdisk).
- **#32 (LocomotorStore::reset):** what MSVC's map does with the old loop's UB. The Hovercraft mission on a
  build without the fix shows it.
- **`_strdup(NULL)` (#31):** the UCRT is taken to return NULL. Measure it.
- **MSVC float→uint32 (S8):** the conversions `Platform/MsvcFloatCasts.h` models, against cl's own code.
## Measured since (2026-09-27)

- **`ZH_GAME_DATA` on the Windows configure** (`-DZH_GAME_DATA=C:/zhr-worker/data`, with the base game's
  `generals/` copied too and hash-verified, 712 entries). Both data tests pass on Windows:
  - `gametext_csf`: its golden hash of the `.csf`'s strings, written "for a Windows run to diff", matches.
  - `test_terrain_golden`: the terrain golden made off Windows matches.
  The data copy was unchanged after both.
- **#32's outcome under MSVC:** the Hovercraft mission on a build without the fix crashed at the end of the
  match ("Pure virtual function called"; EA's debug library then showed its "Game crash" box, which under
  `-headless` now writes to stderr instead of waiting). With the fix, it exits 0 at frame 600 (CRC
  0xF4096AF7).
- **`_strdup(NULL)` (#31):** `test_msvc_float_casts` calls the UCRT's `_strdup(NULL)` through
  `strdupAsWindows` on Windows and passes: NULL.
- **MSVC's float conversions (S8):**
  - `(unsigned)` is the low 32 bits of a 64-bit truncation.
  - `(unsigned short)` is the low 16 bits of the 32-bit one.
  - `(unsigned char)` is the low byte of the 32-bit one.
  - `(int)` is INT_MIN for NaN, the infinities and anything out of range.
  All four are measured over 16 to 20 edge values each and are now the helpers in `Platform/MsvcFloatCasts.h`.

- **The game's data gone, on Windows: measured.** The two window archives were copied to an exFAT VHD
  (`diskpart`, `Z:`) and the farm's two links pointed there. A headless skirmish ran in the desktop session,
  and the VHD was detached (`detach vdisk`) at logic frame 2400. The game ran to its limit and stopped at
  the first archive read after it, with **exit status 3** and "GAME DATA GONE: … (WindowZH.big could not be
  read) …", as on the Mac. So the Windows error a removed volume gives is one of the four `LocalFile.cpp`
  names. Which of them is not logged. The farm was restored and the VHD deleted.
  - Two earlier tries measured nothing:
    - a `Start-Process` argument split on the spaces in the map name, so the match never started;
    - the detach poller read a stale log and detached before the game began.
  - This run made each step a separate call and deleted the old log first.
