# C2 — Entry point

- **Milestone:** M2
- **Depends on:** C1
- **Blocks:** C3
- **Status:** not started
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

## Do not

- Do not create a window, a `NSApplication` or a run loop for M2. Headless means headless, and
  keeping it that way is what makes E1 a clean experiment.
