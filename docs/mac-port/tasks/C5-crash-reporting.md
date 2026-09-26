# C5 — Crash reporting

- **Milestone:** M2
- **Depends on:** B6
- **Blocks:** nothing
- **Status:** claimed (-18, 2026-09-26)
- **Size:** `StackDump.cpp`, the `_set_se_translator` hook in `WinMain.cpp`, and the `/EHa /Oy- /Zi`
  build flags that exist to serve them

## The design, 2026-09-26 (-18; for the PM's review before code)

### What Windows does today (read, not run)

- **Main thread.** `WinMain` sets `_set_se_translator(DumpExceptionInfo)`. A fault becomes a C++
  exception after `DumpExceptionInfo` (StackDump.cpp:413) has written, through `DOUBLE_DEBUG`, into
  both the debug log and `g_LastErrorDump`:
  - the exception kind, with Windows' description text;
  - for an access violation, the address and whether it was a read or a write;
  - the registers and the 32 bytes at the faulting PC, deliberately BEFORE the fragile part;
  - then "Stack Dump:" and the dbghelp context walk.
  `WinMain`'s `catch(...)` then calls `RELEASE_CRASH("Uncaught exception in WinMain")`.
- **Other threads.** `SetUnhandledExceptionFilter(dumpUnhandledException)` does the same, guarded,
  then `ReleaseCrash("Uncaught exception on a worker thread")`. The GameSpy threads also call
  `InstallThreadExceptionTranslator` first thing.
- **`ReleaseCrash` (Debug.cpp:794).**
  - Logs the reason and `g_LastErrorDump`.
  - Rotates `ReleaseCrashInfo.txt` to `…Prev.txt` in the user data folder.
  - Writes `Release Crash at <asctime>; Reason <reason>`. asctime ends in a newline, so "; Reason"
    starts the second line.
  - Then `Last error:` (`g_LastErrorDump`), then `Current stack:`, with frames as
    `  <file>(<line>) : <function> 0x<address>`. Every line also goes to DebugLogFile.txt.
  - Shows "Technical Difficulties..." unless unattended, then `_exit(1)`.
- **Other ways out.** No minidump, no upload: the launcher (another repository) sends the file.
  `RELEASE_DEBUG_LOGGING` keeps `DEBUG_LOG` on in Release, and it works on macOS (seen in C2's runs).

### POSIX (`CrashHandlerPosix.cpp`, installed by PosixMain first thing)

1. **Install (normal context; everything the handler needs is prepared here).**
   - `sigaction` with `SA_SIGINFO | SA_ONSTACK | SA_RESETHAND` for SIGSEGV, SIGBUS, SIGILL, SIGFPE,
     SIGABRT and SIGTRAP. arm64's `__builtin_trap` is SIGTRAP, not SIGILL.
   - A `sigaltstack` for the main thread. On POSIX `InstallThreadExceptionTranslator` gives each
     thread that calls it one of its own; a thread without one still reports anything but a stack
     overflow.
   - The crash file's real POSIX path, resolved once, and the previous file's.
   - The local-time offset, the main thread's id, the executable's load address, and a primed
     `backtrace()`: its first call loads the unwinder, which allocates.
   - Debug.cpp hands over its log's file descriptor when it opens the log.
2. **The handler, async-signal-safe only.**
   - Allowed: `open`, `write`, `rename`, `close`, `time`, fixed buffers and integer formatting of
     its own. Never `malloc`, `printf`, `AsciiString`, `DEBUG_LOG`, the memory manager or a box.
   - A second crashing thread waits (`nanosleep`); a fault inside the handler kills the process
     (`SA_RESETHAND`).
   - It writes, to the crash file and to the log's descriptor, the same sections in the same order
     as Windows:
     - `Release Crash at <asctime shape>` then `; Reason Uncaught signal SIGSEGV on the main thread`
       (or `…on a worker thread`);
     - `Last error:` with the signal, its `si_code` meaning and the fault address, then
       `Details:`, `Register dump...` from the ucontext (arm64 x0-x28, fp, lr, sp, pc, cpsr;
       x86_64 its sixteen and rip), then the 32 bytes at the PC, each read checked through a pipe
       write so an unmapped PC cannot fault the handler;
     - `Stack Dump:` then raw frames as `  <module>(0) : <module>+0x<offset> 0x<address>`: the
       fault PC first, then `backtrace()`;
     - `Current stack:`.
   - That much is `fsync`ed. Only then comes a best-effort second pass that names the frames with
     `dladdr` in Windows' shape (`  <module>(0) : <symbol>+0x<offset> 0x<address>`; POSIX has no
     line numbers in-process). `dladdr` is not formally async-signal-safe; if it hangs or faults,
     only the names are lost. `atos` turns the raw lines into lines offline.
   - Finally it re-raises with the default action, so macOS's crash reporter and a core dump still
     get the crash, and the process dies of the signal, not `exit(1)`.
3. **C++.** `std::set_terminate` sends an exception that escapes a thread to
   `ReleaseCrash("Uncaught exception on a worker thread")` in normal context. The main thread
   already has PosixMain's `catch(...)`.
4. **The box.** Never from a signal. `ReleaseCrash` (normal context) shows "Technical
   Difficulties..." through `MessageBoxWrapper`'s SDL hook when there is a window and the run is
   not unattended, and prints to stderr otherwise. After a signal, the file, the log and macOS's
   own report are the record.
5. **Format.** Same file name, place, sections and line shapes. Only the register names and the
   signal text differ, because the CPU and the OS do; the launcher reads the sections.
6. **The Windows flags.** CMake's `/EHa /Oy- /Zi` comments say they are Windows-only. macOS keeps
   frame pointers by ABI. Linux gets `-fno-omit-frame-pointer` at its milestone (noted, not now).

### How it is tested

`test_crash_reporting` (ctest) runs a child of itself per crash kind, with its own
`ZH_USER_DATA_DIR`:
- a null write (SIGSEGV), `abort()`, `raise(SIGFPE)` and `raise(SIGBUS)`, `__builtin_trap()`, a
  stack overflow (the alternate stack), a worker thread's null write, and a worker thread's
  uncaught exception;
- the parent asserts the exit (killed by that signal, or `exit(1)` for the exception) and the
  file: the sections in order, the reason, the fault address, a register line, and the crashing
  test function's name in the named pass;
- the child mutes macOS's crash reporter for itself only (`task_set_exception_ports`,
  `EXC_MASK_CRASH`), so a ctest run leaves no reports;
- armed controls: without the install there is no file; without `SA_ONSTACK` the overflow writes
  nothing.

### As built (2026-09-26)

- **POSIX frame lines, a stated difference for whoever ports the launcher.** POSIX uses
  `  <module>(0) : <symbol>+0x<offset> 0x<address>`, and `  <module>(0) : <module>+0x<offset>
  0x<address>` for a raw frame. It is Windows' shape, with the module where the file goes and line 0,
  because in-process there is no line table. `atos -o <module> -l <load address> <address>` gives the
  line offline. Accepted by the PM; the launcher (the upstream Electron app) has no macOS build to
  check it against.
- **Retry in the Debug assertion box** (`breakIntoDebugger()`, `raise(SIGTRAP)` off Windows). With no
  debugger attached it is a crash: the handler writes the report and the process dies of SIGTRAP, as
  `DebugBreak` without a debugger is an unhandled exception on Windows. `test_crash_reporting`'s
  "break" child checks that. With lldb attached (measured by hand, `lldb -- test_crash_reporting
  --crash break`): lldb stops with "stop reason = signal SIGTRAP" in `__pthread_kill`, and at that
  moment no ReleaseCrashInfo.txt exists. The debugger takes the break before the handler runs, which
  is what Retry is for.
- **macOS's crash reporter** is muted in the test's children only, by `task_set_exception_ports` for
  `EXC_MASK_CRASH | EXC_MASK_CORPSE_NOTIFY`. `EXC_MASK_CRASH` alone still let some reports through
  (26 `.ips` files over the first runs, since deleted). With both masks a full run leaves none.

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
