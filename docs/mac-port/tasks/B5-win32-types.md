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
