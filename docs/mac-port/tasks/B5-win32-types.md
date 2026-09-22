# B5 — Win32 scalar types

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** not started
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
