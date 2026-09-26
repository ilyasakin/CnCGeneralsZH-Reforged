# C2 — Entry point

- **Milestone:** M2
- **Depends on:** C1
- **Blocks:** C3
- **Status:** claimed (-18, 2026-09-26)
- **Size:** `Main/WinMain.cpp` is ~1,400 lines with `WinMain.h` and `RTS.RC`

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** The entry point is `SDL3`-based, one `main` for macOS and Linux, in plain C++ (`PosixMain.cpp` or similar). The `MacMain.mm` / Cocoa option below is withdrawn: SDL3 owns the window and the event loop at M4.

> **Locale, 2026-09-25 (B5): a hard constraint, see the plan's rule "the only locale category the
> game may set is `LC_TIME`".** Call `setlocale(LC_TIME, "")` early so that the replay and save
> lists show dates in the user's format (`GameState.cpp`'s `getUnicodeDateBuffer` and
> `getUnicodeTimeBuffer` use `strftime`). Set nothing broader: `LC_NUMERIC` would change how the
> INI parser reads decimals, and the string shims assume `LC_CTYPE` is "C". Until C2 sets it, dates
> show in the "C" format (`MM/DD/YY`, 24-hour). One known difference from Windows: the time shows
> seconds (`%X`), because `strftime` has no "this locale's format without seconds" and Windows
> passes `TIME_NOSECONDS`. `nl_langinfo(T_FMT)` with the seconds removed would match, if anyone
> cares.

## The design, 2026-09-26 (PM's C2 brief; the PosixGameEngine shape agreed with C1 (f) and adopted)

- **`Main/PosixMain.cpp`**, one `main` for macOS and Linux on SDL3. In order, it:
  - hands `argv` to the engine: EarlyCommandLine's POSIX body reads it instead of "not given";
  - sets `LC_TIME` only (the locale note above);
  - sets the install root with ONE `chdir`, from a `-root` option or else the executable's
    directory (WinMain's `SetCurrentDirectory` to the exe's folder). Rule 9 applies: never the real
    install on `/Volumes/External`. Nothing changes the directory afterwards; C1's Roots paragraph
    relies on that;
  - does the rest of WinMain's pre-engine sequence that C1 does not already cover: the critical
    sections, `DEBUG_INIT` then `initMemoryManager`, `TheVersion`, the one-copy guard and
    `-multiInstance`, the `-win`/`-fullscreen`/`-borderless`/`-headless` pre-parse, and the
    `GameMain(argc, argv)` call with the same teardown and the same catch-alls into `RELEASE_CRASH`.
  - C1 covers the user data directory, the Options.ini and Registry.ini reads through `zh_fopen`,
    the file systems, and the joins after `getExecutableDirectory` (-a9, 2026-09-26).
- **`CreateGameEngine()`** is defined in `PosixMain.cpp` and returns `NEW SdlGameEngine`. C1 does not
  define it; a C1 test that needs one defines its own.
- **`SdlGameEngine : PosixGameEngine`**, in C2's device folder. `PosixGameEngine` (C1 (f)) is
  abstract: its file systems, network and browser are implemented, `serviceWindowsOS` is an empty
  virtual, and every factory is virtual and non-final. `SdlGameEngine` does four things:
  - overrides `serviceWindowsOS` to pump SDL events, with focus into `m_isActive` as
    `WM_ACTIVATEAPP` does;
  - owns the SDL window, with no renderer (D4 attaches one);
  - routes the window title through `setApplicationWindowTitle` to `SDL_SetWindowTitle`;
  - gives `Monitors` SDL's display list, replacing the 800x600 fallback, and `test_gameengine`'s
    `GetSystemMetrics` shim follows it.
  `MessageBoxWrapper`'s POSIX body uses `SDL_ShowMessageBox` when a window exists. The renderer,
  logic and audio factories stay C1's pure virtuals until T1, D4 and C4 fill them.
- **`-headless` initialises no SDL video subsystem at all.** It must run on a machine with no
  display, as E1's replay runs will. It still has a render device, as Windows' `-headless` does:
  decision 7's own CPU-backed D3D9-shaped device, with no window (decision 8, refined 2026-09-26).
  So it does NOT set `m_noRenderDevice`. W3DDisplay's device creation takes a null `RenderWindow`
  off Windows, which is A1's shim to honour. SdlGameEngine's factories mirror Win32GameEngine's
  exactly, including W3DRadar under `-headless` and HeadlessRadar only under `-nodevice`. The
  `-nodevice` path's known gaps are listed under the plan's status board.
- **`gAppPrefix`, `g_csfFile` and `g_strFile`** get their real definitions here, with WinMain's
  values. They move out of the test stubs and drivers.
- **Out of scope:** input mapping (C3), audio wiring (C4's upper half), the app bundle (M5).

**Done when** (M2's first slice, which also needs C1 (f) and T1): `generals -headless` on macOS,
rooted at a COPY of the game data, boots, mounts the `.big` files, runs a skirmish on a random map
(`-randommap ... -autoskirmish`, as `replay-check.ps1` does) for N frames, writes a replay, and exits
0. The windowed build opens an empty SDL window and closes cleanly on quit.

**Known UX point, for the LAN screens (B5, 2026-09-26).** IPEnumeration lists the machine's IPv4
addresses lowest first, as Windows does. The LAN lobby, direct connect and Options take the first
one when no preference is saved. On a Mac with Parallels and Tailscale the lowest is the Parallels
host-only adapter (10.37.129.2), not the LAN (192.168.1.103), and a Tailscale 100.x sits between
them. So a first run hosts on a VM adapter until the player picks another address in Options.
Windows does the same with VirtualBox or VPN adapters: parity, and deliberately not changed (PM
decision). A later UX pass could prefer the default-route interface on both platforms.

## Why

`generals.exe` starts at `WinMain`, which creates the window, installs the crash handler, parses
the command line and runs the engine loop. A Mac build needs the same sequence without the Windows
in it. M2 only needs the headless half; the window can wait for M4.

## Scope

- `GeneralsMD/Code/Main/WinMain.cpp` — read it end to end before starting. It does more than it
  looks like it does.
- New: `Main/MacMain.mm` (Objective-C++ so it can reach Cocoa at M4) or `MacMain.cpp` if you can
  keep M2 free of Cocoa entirely. Prefer the latter for M2 and rename when M4 needs it.
- `CMakeLists.txt` line 604, `add_executable(generals WIN32 ...)`.
- `RTS.RC` is a Windows resource script — icon, version info. The Mac equivalent is an
  `Info.plist` and an `.icns`, and that is packaging, not M2. Note it and move on.

## Do

1. Split `WinMain.cpp` into what is genuinely Windows and what is the startup sequence. The
   sequence — parse the command line, set up the engine, run it, tear it down — is portable and
   should end up somewhere both entry points call.
2. `-headless` (`CommandLine.cpp:1348`) is the M2 target. A headless run draws no frame, runs the
   logic tick flat out, and is bounded by `-maxframes`. That path should need no window, no
   graphics device and no audio, and if it currently does, say so on this task — it is a finding
   worth having.
3. `_set_se_translator(DumpExceptionInfo)` and the `/EHa` that makes it work have no macOS form.
   Leave the hole; C5 fills it. Do not fake it.
4. The exe becomes `Generals.app` eventually. For M2 a plain executable is correct and simpler to
   run under `ctest`.

## Done when

```console
./build-mac/generals -headless -maxframes 1000 <a skirmish setup>
```

runs a skirmish to completion on macOS and exits cleanly, with the same log lines the Windows build
writes at the same points.

It does not yet have to produce the same checksum — that is E1, and it is the next thing that
happens.

Windows: `WinMain.cpp` behaviour unchanged. The refactor is a move, not a rewrite; if `generals.exe`
starts differently than it did, the split went too far.

## What B5 left for C2 to replace, 2026-09-26

B5 made these compile off Windows with a stand-in, and C2 replaces each:

- **The process command line.** `EarlyCommandLine.h`'s `findEarlyCommandLineOption`/`Value` read
  `GetCommandLineW` on Windows. Off Windows every option reads as not given until C2 hands over
  `argv`: `-jobthreads`, `-logPrefix` and **`-headless`**, which a headless run off Windows needs
  first.
- **The displays.** `Monitors.h` off Windows reports ONE primary monitor, 800x600, offering that
  one mode: Windows' own no-desktop fallback, with the game's floor for a size. It is not empty on
  purpose, because an empty rect would give borderless a 0x0 resolution. Replace it with SDL3's
  display list and modes. `test_gameengine.cpp`'s POSIX shim answers `GetSystemMetrics` with the
  same 800x600 floor as the borderless tests' expectation; it has to follow, to the primary
  display's size by SDL3, or those two tests will compare against the wrong number.
- **The window title.** `GameText.cpp`'s `setApplicationWindowTitle()` does nothing off Windows.
  It becomes `SDL_SetWindowTitle` once C2 owns the window.

## The exe's names that stand-ins define today, 2026-09-26 (B6)

`gameengine` names four things `Main/WinMain.cpp` defines on Windows: `CreateGameEngine()`,
`g_strFile`, `g_csfFile` and `gAppPrefix`. Until this task writes the POSIX entry point, the only
executables that link `gameengine` off Windows are tests, and they define them: `test_gameengine`'s
stubs (`g_strFile`, `g_csfFile`, `gAppPrefix`; it never reaches `GameMain`, so needs no
`CreateGameEngine`) and B6's measuring driver. The real definitions are this task's, beside the entry
point, with the same values as `WinMain.cpp`'s; `CreateGameEngine` returns C1's `MacGameEngine`.

## Do not

- Do not create a window, a `NSApplication` or a run loop for M2. Headless means headless, and
  keeping it that way is what makes E1 a clean experiment.

## The W3D factories, and the first real run, 2026-09-26

**Wired** (A1's hand-over):
- **Factories:** SdlGameEngine's are Win32GameEngine's, the same W3D classes (decision 8). The radar is
  W3DRadar, and HeadlessRadar only under `-nodevice`.
- **PosixW3DGameClient:** gives the video player. It is the engine's own `VideoPlayer`, which opens
  nothing, until V1. It is not NULL, because GameClient calls the player unguarded.
- **Window globals:** `ApplicationHWnd` and `ApplicationIsBorderless` are defined in PosixMain.cpp.
  `ApplicationHWnd` is the SDL window, and NULL under `-headless`.
- **The window follows the display** through `W3DWindowHooks.h`.
- **Stand-ins removed:** `PosixRenderHooks.cpp` and `MapObjectRenderPosix.cpp` are gone. Decision 2 now
  lives in `W3DShaderManager::testMinimumRequirements`, checked by `test_render_hooks`.

**The first real `generals -headless`** ran on a symlink farm of the install (rule 9). The install
listing is identical before and after every run. The command was
`-headless -root <farm> -randommap 1234 2 small -autoskirmish 2 -seed 1234 -maxframes 600`, built with
`RELEASE_DEBUG_LOGGING`. What happened:
- **Mounted:** every archive, with the base game from `ZH_Generals`.
- **Initialised:** INI CRC 0x1E635A82.
- **Played:** it generated the random map, brought up W3DDisplay on the POSIX device with no window,
  started a two-player skirmish (a Hard AI) and ran it to the frame limit: `HEADLESS RESULT: frame
  limit reached on frame 600 (600 frames in 2.1s wall, 285 logic fps)`, `HEADLESS CRC: 0x78BEA937 at
  frame 600`.
- **Wrote a replay** (`Replays/00000000.rep`) and **exited 0**.
- **Repeated:** a second run with the same seed gave the same CRC.

**Found on the way:**
- **The fork's own data.** The first run stopped at `Data\INI\FXListReforged.ini`. On Windows,
  `generals`' post-build copies `Code/Data`'s masters over `Run/`, and there is no POSIX equivalent yet.
  The farm lays the same directories over the install, unlinking each link before copying, so nothing is
  written through into the install. A POSIX deployment step is still to be decided.
- **Output in the exe's folder.** The debug log and the perf files land next to the executable, as on
  Windows. The fixed-function probe's `ffprobe.txt` lands in the root (the farm). Runs in parallel would
  share the exe's folder.
- **A lowercased path.** The random map's path is lowercased, user-data prefix included
  (`/private/tmp/.../-users-ilyasakin-...`). APFS's default is case-insensitive, so it works here. On a
  case-sensitive volume (Linux) that path would not exist.
- **C5 met its first real crash:** the missing INI gave a ReleaseCrashInfo.txt with the reason and the
  named stack.
