# B5 — Win32 scalar types

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** in review — the `wwlib` pass (below); the `GameEngine` files are untouched
- **Size:** 21 of 604 files in `GameEngine/Source` — 8 Common, 7 GameClient, 6 GameNetwork

## Why

`DWORD`, `HRESULT`, `BOOL`, `LPSTR`, `HWND` leaking out of the platform layer into engine code.
The number is small, which is the point: this is a tidy-up that makes the engine library compile
without `windows.h`, and it is worth doing properly rather than with a compatibility header
because there are only 21 files.

## Scope

Start with the list:

```console
grep -rIl --include='*.cpp' -E '\bHWND\b|\bHRESULT\b|\bDWORD\b|windows\.h' GeneralsMD/Code/GameEngine/Source
```

`GameEngine/Include` has 4 more under `Common`, plus `Precompiled` and
`GameNetwork/WOLBrowser`. `GameLogic` has none, and finishing this task should keep it that way.

The largest single item on that list is **`GameEngine/Source/Common/System/ControlServer.cpp`**
(840 lines): a loopback WebSocket server behind the `-control` switch, written against Winsock 1.1
because, as its own comment says, `windows.h` "which PreRTS.h already pulled in, brings Winsock 1.1
with it. That is every call this file makes". Every call it makes is also plain BSD sockets, which
is the POSIX original — `SOCKET` is an `int`, `closesocket` is `close`, `ioctlsocket(FIONBIO)` is
`fcntl(O_NONBLOCK)`, and `WSAStartup` goes away. Port it rather than stubbing it; see the note on
`-control` in the plan's README for why it is worth having on macOS.

## Recon findings, 2026-09-22 — this changes the ORDER of the work

The survey is done (`docs/mac-port/B5-win32-type-survey.md`). Its most important conclusion is
about sequencing, and this task file had it wrong.

**Do the `PreRTS.h` guard early, not last.** Until `windows.h` is out of the precompiled header,
the PCH silently satisfies every one of the other fixes, so none of them is visible. The guard is
what turns the remaining work into a compile-error worklist. Three steps:

1. Give every file that genuinely needs Windows its **own** `#include`s while the PCH still
   provides them. Today the bucket-(c) files declare nothing themselves — `ChromaKeyboard.cpp`
   includes `wininet.h` but leans on the PCH for `HANDLE` and `CreateThread`; `StackDump.cpp`
   names `IMAGEHLP_LINE64` with no include of its own. **This step is a no-op on Windows by
   construction** — identical declarations, identical command line, identical object file — which
   makes it the one change in B5 that can be made with real confidence without a Windows machine.
   Its own commit.
2. Wrap `PreRTS.h:43–96` in `#if defined(_WIN32)`. Windows is unchanged by construction; macOS
   starts producing honest errors.
3. Then the bucket-(a) substitutions, which are now visible.

Counts to revise: **18 of the 21 files are compiled** (`simpleplayer.cpp`, `urllaunch.cpp` and
`GameSpyGameInfo.cpp` are already in `REMOVE_ITEM`). The headers under `Common` are **9, not 4**,
of which 6 are real. And the distribution is not what "scalar types" suggests: bucket (a) 7 files,
bucket (b) 3, **bucket (c) eleven** — over half the compiled sites are whole files of Windows
subsystem code, not type substitutions.

Bucket (b) is smaller than it looks: all three sites are the same handle, `ApplicationHWnd`, and
nothing inspects it. The engine wants a "show the user this fatal message" hook, not a window
handle.

**WOLBrowser is confirmed as bucket (c), and it is worse and easier than this file assumed.** It
is the reason `gameengine` has a *configure-time* dependency on `midl.exe`, and the only reason
`atlbase.h` is in the PCH. `TheWebBrowser` has never been non-NULL — the sole assignment site
(`GameEngine.cpp:923`) is commented out and every consumer already tests for NULL. Moving
`GameNetwork/WOLBrowser/` into `Win32Device/` is behaviour-preserving in the strongest sense and
removes `atlbase.h`, the MIDL custom command and `eabrowserdispatch` from the engine's graph at a
stroke. The `createWebBrowser` factory stays on `GameEngine`.

**`JobSystem.cpp` came out of this survey and is now its own task, B8.** It is a Win32 thread pool,
not a type leak, and it is the one bucket-(c) file the engine actually needs working on macOS.

Two quick wins worth taking first: `LookAtXlat.cpp:31` is `#include "windows.h"` and nothing else
in the file uses a Windows type; `AsciiString.h:60` is a bare `windows.h` include with no Windows
type named anywhere in the header — and that one is in most of the engine's include graph.

## The `wwlib` pass, 2026-09-25 — what was done and what it did not reach

Scheduled from `Tools/syntax_sweep.py build-mac wwlib` on `9f4c1811`, re-run after every step.
Front-end numbers throughout; `-fsyntax-only` is not a link.

**58 of 82 → 64 of 67.** The denominator shrank by fifteen files that are now left out of `wwlib`
off Windows, so read it as two numbers: fifteen excluded with a reason, six fixed in place. On Windows
`wwlib` still gets all 82, in the same order.

| What | How | Why |
|:--|:--|:--|
| DirectDraw: `convert`, `ddraw`, `dsurface` | excluded | PM's ruling. `ConvertClass` survives as a type: `wwfont.cpp` calls only its inline `Convert_Pixel`, and WW3D2's `txt*.cpp` only hold references to it |
| `_mono`, `mono` | excluded | a driver for the `\\.\MONO` second-monitor device. WW3D2's `rendobj.cpp` includes `_mono.h` and uses nothing from it, which is a dead include for the D-track to drop |
| `data`, `rcfile`, `msgloop`, `keyboard`, `LaunchWeb`, `WWCOMUtil`, `verchk` | excluded | Win32 resources, the message pump, ShellExecute, COM and version resources. **Zero callers** in `GeneralsMD/Code`. That answers the `oaidl.h` question: `WWCOMUtil.h` is included by `WWCOMUtil.cpp` and nothing else |
| `win.cpp` | excluded | defines the `HINSTANCE`/`HWND` globals that `win.h` declares only under `_WINDOWS` |
| `registry.cpp` | excluded; `registry.h` `#error`s off Windows | HKEY throughout. Its real callers are `dx8wrapper.cpp`, `W3DDisplay.cpp` and `WWAudio.cpp`, which persist device and audio settings; where those live on macOS is their owners' decision. `ww3d.cpp` includes it and uses nothing. WWDownload's `urlBuilder.cpp` includes its **own** `Registry.h` |
| `srandom.cpp` | excluded | `SecureRandomClass` has no caller, and its only non-Windows seed is `_UNIX` |
| `systimer.h`, `refcount.cpp` | `<windows.h>` guarded | dead since B2 moved the clock onto `Lib/Clock.h` |
| `systimer.h` | **width fix** | `StartTime`/`WrapAdd` were `unsigned long`, so `WrapAdd = 0 - StartTime` wrapped at 2^64 on LP64 and `Get` returned about 2^64 after the 49.7-day wrap. `Lib/Clock.h` names this exact expression as needing 32 bits |
| `ini.cpp` | `OutputDebugString` guarded; stderr off Windows | |
| `rawfile.cpp` | **ported to POSIX descriptors** | see below |

**`rawfile.cpp`'s `_UNIX` arms were holes, like `mutex.cpp`'s.** Opening READ\|WRITE used
`fopen("w")`, which truncates the file the Windows arm opens with `OPEN_ALWAYS` precisely so that it
is not destroyed. `Raw_Seek` returned `fseek`'s 0. `Read` took `ferror`'s answer as success.
`Get_Date_Time` returned Unix time where every caller keeps DOS time. `Set_Date_Time` asserted. They
are replaced by POSIX arms written call-for-call against the Win32 ones. Three tests in `test_wwlib`
cover them, and each was checked by putting the old behaviour back: a truncating open, seek answering 0,
Unix time, and local time instead of UTC each turned their test red. They were run against the real
`wwlib` objects in a scratch harness, since `test_wwlib` does not link yet. **The local-time mutation is
caught only under a non-UTC `TZ`.** `Set_Name`'s `_UNIX` arm (backslash rewriting and lowercasing) is
left alone: paths are C1's.

### What is left in `wwlib`, and whose it is

- `mixfile.cpp` — `_splitpath`, **C1**, as ruled.
- `mpu.cpp` — x86 `__rdtsc`/`__cpuid`, left alone as ruled.
- **`cpudetect.cpp` — handed on whole, not split.** Guarding its `<windows.h>` and `<intrin.h>`
  exposes 23 errors. The Windows-API ones (`OSVERSIONINFO`, `VER_PLATFORM_*`,
  `GetTimeZoneInformation`) are B5-shaped, but they sit in the same file as x86 `__rdtsc`/`__cpuidex`,
  `__int64` and an MSVC-only extra qualification (`:1064`), and the two halves cannot be separated:
  `Init_Memory()` and `Init_OS()` run **only inside `if (Has_CPUID_Instruction())`**, so what the
  memory port does depends on the arm64 answer to CPUID. And the outputs are not diagnostics:
  `W3DShaderManager::testMinimumRequirements` hands `Get_Processor_Speed`, the Intel/AMD model tiers
  and `Get_Total_Physical_Memory` to `GameLOD`, which chooses the default detail preset from them.
  "What tier is an Apple M-series chip" is a product decision, not a type substitution. **Recommended:
  one task, `cpudetect.cpp` + `mpu.cpp`, "CPU detection and the tick clock on arm64".**

So `wwlib` is not one task from a front-end build: it is that task plus C1's `_splitpath`.

### Found on the way, not fixed here

- **The link tail is three symbols, and none comes from an exclusion.** A trial link of the 64
  objects leaves exactly `AutoPoolClass<GenericSLNode,256>::Allocator`,
  `AutoPoolClass<MultiListNodeClass,256>::Allocator` and `Int<64>::Remainder` unresolved.
  `mempool.h`'s `DEFINE_AUTO_POOL` and `int.cpp:48` spell these `template<> T X<...>::m;`, and in
  standard C++ an explicit specialisation of a static data member **with no initialiser is a
  declaration, not a definition**, so clang emits nothing. `int.cpp`'s three siblings survive only
  because they have `= 0`. MSVC presumably treats the form as a definition, since Windows links; that
  is not verified. The fix is an initialiser (`{}`). It changes Windows-compiled text in a macro with
  ten users, so it wants its own commit and a second reader. It was introduced by `8a468857`.
  What this check cannot see: Debug-only references, the three files that do not compile, and
  consumers outside `wwlib`.
- **Debug configuration:** `refcount.cpp`'s two `DebugBreak` calls are the only addition; they wait
  for B16's portable break rather than invent a second one.
- **`Tools/syntax_sweep.py` reads `CXX_FLAGS` and `CXX_INCLUDES` but not `CXX_DEFINES`, and compiles
  `.c` as C++.** `compression` builds, yet sweeps 13 of 28. `wwlib`'s defines are only
  `_CRT_NONSTDC_NO_WARNINGS` and `_CRT_SECURE_NO_WARNINGS`, so its number is unaffected; a target
  with real `target_compile_definitions` is not.
- `texturethumbnail.cpp:339`/`:459` read and write a DOS date as `sizeof(unsigned long)`: 8 bytes on
  macOS, 4 on Windows. The thumbnail cache is therefore not portable between the two. D-track or B10.
- Shared-code quirks now documented in the tests, identical on both platforms: `RawFileClass::Size()`
  stores its answer in `BiasLength`, so a second call on the same object never sees the file change.
  `Read` retries forever on a persistent read error, because the base `Error()` does nothing.

## Do

1. For each site, decide which of three it is:
   - **A scalar in disguise.** `DWORD` standing in for `uint32_t`, `BOOL` for the engine's own
     `Bool`. Replace with the engine type from `Libraries/Include/Lib/BaseType.h`. Most sites are
     this.
   - **A genuine handle crossing the boundary.** `HWND` reaching engine code from the device layer.
     These want an opaque typedef the platform layer defines — the engine should not know what a
     window handle is made of. `GameEngine/Include/Common/GameEngine.h`'s factory seam is the model
     for how this code already separates the two.
   - **Windows code that should not be in `GameEngine` at all.** `GameNetwork/WOLBrowser` and the
     `WebBrowser` factory are the likely candidates. Do not move files in this task; name them in
     the pull request and let B6 decide whether they are stubbed or excluded.
2. `GameEngine/Include/Precompiled/PreRTS.h:51` is `#include <windows.h>`, and it is the
   precompiled header the whole engine library compiles against. Whatever else happens, that one
   line has to become conditional or every other fix in this task is invisible.

## Done when

`gameengine`'s headers and sources compile on macOS without any `windows.h` in the include graph —
which is not the same as the library linking, which is B6. Windows full build and `ctest` green.

## Do not

- Do not write a `WinTypes.h` that defines `DWORD` and `HWND` on macOS. It would make the errors go
  away and leave the problem in place, and every later task would build on it. There are 21 files;
  fix them.
- Do not touch `GameEngineDevice`. Windows types belong there and stay there.
