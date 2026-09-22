# C5 — Crash reporting

- **Milestone:** M2
- **Depends on:** B6
- **Blocks:** nothing
- **Status:** not started
- **Size:** `StackDump.cpp`, the `_set_se_translator` hook in `WinMain.cpp`, and the `/EHa /Oy- /Zi`
  build flags that exist to serve them

## Why

"When the game crashes, the launcher sends the report without asking you for anything" is a feature
this project advertises, and the mechanism behind it is entirely Windows: `WinMain` installs
`_set_se_translator(DumpExceptionInfo)` to turn an access violation into a C++ exception,
`StackDump.cpp` walks the EBP chain by hand, and `ReleaseCrashInfo.txt` comes out with file and
line. `CMakeLists.txt` carries `/EHa`, `/Oy-` and `/Zi` specifically to keep that working, and its
comments say so.

None of it exists on macOS. Structured exception handling is a Windows concept, and there is no
frame-pointer chain to walk in the same way.

This is M2 rather than M5 because the porting machine has no debugger — `README.md` says so — and a
crash report is the only stack anyone gets. Porting without it means debugging M2 and M3 blind.

## Scope

- `GameEngine/Source/Common/System/StackDump.cpp` and `GameEngine/Include/Common/StackDump.h` —
  the EBP walk is the part that does not port
- `Main/WinMain.cpp`'s `_set_se_translator` and outer `catch(...)`
- `ReleaseCrashInfo.txt` is written from `GameEngine/Source/Common/System/Debug.cpp` and
  `Main/WinMain.cpp`
- `CMakeLists.txt` lines 56–71, the flags and their comments

## Do

1. On macOS: `sigaction` for `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGABRT`, plus
   `std::set_terminate` for the C++ side. `backtrace()` and `backtrace_symbols()` give the frames;
   `atos` or `dladdr` turns addresses into names.
2. **Signal handlers are async-signal-safe or they are lies.** A handler that calls `malloc` or
   `fprintf` will deadlock a fraction of the time, which is the worst possible failure mode for a
   crash reporter. Write the frames with `write(2)` into a preallocated buffer, and symbolicate
   offline.
3. `ReleaseCrashInfo.txt` keeps the same name, the same location and the same shape on both
   platforms. The launcher parses it, the launcher is a separate repository, and a format change
   here is a break there.
4. `RELEASE_DEBUG_LOGGING` (`CMakeLists.txt` line ~52) turns `DEBUG_LOG` back on in Release so
   startup failures land in a log. Make sure it works on macOS — it is the other half of debugging
   without a debugger, and M2's first boot failures will need it.
5. Update the `/EHa` and `/Oy- /Zi` comments to say they are Windows-only and why macOS does not
   need them.

## Done when

A deliberate null dereference on macOS writes a `ReleaseCrashInfo.txt` with a symbolicated stack,
in the same format the Windows build writes. Add a test that faults a child process and checks the
file — the crash reporter is exactly the kind of code that quietly stops working, and this project
already catches things that way.

Windows: unchanged. Same file, same format, same flags.

## Do not

- Do not change `ReleaseCrashInfo.txt`'s format to suit macOS. The launcher reads it.
- Do not allocate in a signal handler.
