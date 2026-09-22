# B5 — survey of the Win32 type leaks in `GameEngine`

Analysis for [B5](tasks/B5-win32-types.md), written while B5 itself is blocked on A1. No source
file is changed by this document and nothing here is added to `WINDOWS-DEBT.md`.

It answers three things: what each of the 21 sites actually is, how `ControlServer.cpp` maps onto
BSD sockets call by call, and what it would take to make `PreRTS.h:51` conditional.

## Summary

| | |
|:--|:--|
| `.cpp` in `GameEngine/Source` matching the grep | 21 |
| …of those, actually compiled | **18** — three are already `REMOVE_ITEM`'d from the CMake glob |
| Headers in `GameEngine/Include` matching | 14 |
| …of those, actually in the compile graph | **10** — two are comment-only, two are orphans of the dead `.cpp`s |
| `GameLogic` | still zero. Confirmed. |
| Bucket (a) — a scalar in disguise | 7 files |
| Bucket (b) — a genuine handle crossing the boundary | 3 files, all of them the same handle |
| Bucket (c) — should not be in `GameEngine` at all | **11 files**, and this is the real work |

The task file's shape is right and its size is optimistic. The count of sites is small, but the
distribution is not what "21 scalar types" suggests: over half the compiled sites are bucket (c),
whole files of Windows subsystem code — SNMP, ICMP, IMM32, dbghelp, wininet, ATL/COM — sitting in
the engine library. Those are not type substitutions. They are files that move or get excluded, and
B5's real deliverable is the list of them that B6 acts on.

Three facts change the estimate downwards, though:

- **`GameLogic` is clean and stays clean.** The two `BOOL` hits in `EMPUpdate.cpp` are inside
  commented-out lines, and `CrowdModel.h:195` is a comment explaining that its second parameter is
  deliberately not named `small` because `rpcndr.h` — which arrives with `windows.h` — `#define`s
  it. That comment is the only trace `windows.h` has left in `GameLogic`, and it disappears on its
  own merits once the PCH is conditional.
- **Three of the 21 are already out of the build.** `Common/Audio/simpleplayer.cpp`,
  `Common/Audio/urllaunch.cpp` and `GameNetwork/GameSpyGameInfo.cpp` are all in the
  `list(REMOVE_ITEM GAMEENGINE_SOURCES …)` blocks at `GeneralsMD/Code/CMakeLists.txt:498-513`, with
  reasons already written down there. Between them they account for a large share of the raw
  `HRESULT`/`DWORD` hit count — `simpleplayer.cpp` alone is ~25 of them — and none of it matters.
  Do not spend an hour on them; check the glob first.
- **There is an in-tree precedent for the socket work, and it is already half-done.** See the note
  at the end of section 2.

## 1. Every site, classified

### 1a. A scalar in disguise

Mechanical. `DWORD` → `UnsignedInt`, `BOOL` → `Bool`, from
`GeneralsMD/Code/Libraries/Include/Lib/BaseType.h`.

| File | Sites | Note |
|:--|:--|:--|
| `Common/System/ControlServer.cpp` | all of them | Section 2. Once the Winsock spellings go, nothing Windows-shaped is left. |
| `GameClient/MessageStream/LookAtXlat.cpp` | `:31` | `#include "windows.h"` and **nothing else in the file uses a Windows type**. Delete the line. The cheapest site in the task. |
| `GameClient/CinemaDirector.cpp` | `:385,388,463` | `DWORD` wall-clock variables around `timeGetTime()`. The type is (a); the call is B2's. |
| `GameNetwork/DownloadManager.cpp` | 9 sites | `HRESULT` used purely as a return code on `DownloadManager`'s virtuals. The only values that ever flow are `S_OK` and the downloader's own. Becomes `Int`. |
| `GameClient/GUI/GUICallbacks/Menus/DownloadMenu.cpp` | 12 sites | Same `HRESULT`, the override side of the same virtuals. Changes with `DownloadManager.h` or not at all. |
| `GameClient/GUI/GUICallbacks/Menus/MainMenu.cpp` | `:384`, `:627` | `:384` is `FILETIME` arithmetic. `:627` is inside a commented-out `InternetGetConnectedState` block — free. |
| `Common/Directory.h` | `:55,56` | `DWORD attributes` / `DWORD filesize`, the latter already carrying a comment saying 32 bits is all anyone wants. → `UnsignedInt`. |

Also here: `Common/AsciiString.h:60` is a bare `#include "windows.h"`, sitting oddly below the class
forward-declaration, and **no Windows type is named anywhere in that header**. It is a delete, and it
is worth doing early because `AsciiString.h` is in the include graph of most of the engine.

### 1b. A genuine handle crossing the boundary

All three sites are the same handle, `ApplicationHWnd`, reaching into the engine as a bare `extern`:

| File | Site | What it is for |
|:--|:--|:--|
| `Common/GameEngine.cpp` | `:188` `extern HINSTANCE ApplicationHInstance`, `:2369` `extern HWND ApplicationHWnd` | |
| `GameClient/GameText.cpp` | `:461` `extern HWND ApplicationHWnd` | |
| `Common/System/Debug.cpp` | `:68` `extern HWND ApplicationHWnd` | `getThreadHWND()` (`:146`) returns it or `NULL`, and `MessageBoxWrapper()` (`:172`) passes it to `::MessageBox`. `GameEngine.cpp:716,727` re-declare `MessageBoxWrapper` locally to call it. |

There is less here than the three files suggest. In all three cases the handle is only ever used to
parent a message box or to ask "am I the main thread". Nothing in the engine inspects it. The task
file's prescription — an opaque typedef the platform layer owns — is right, and the smaller version
is righter still: the engine wants a *"show the user this fatal message"* hook, not a window handle
at all. `Debug.cpp`'s `isUnattendedRun()` already short-circuits the message box under `-headless`,
which is most of the abstraction written out by hand.

`Debug.cpp` also carries `DWORD theMainThreadID` / `GetCurrentThreadId` (`:109`) and
`GetTickCount` (`:241,242,246`); the thread id is (a) plus a one-line shim, the tick count is B2's.

### 1c. Windows code that should not be in `GameEngine`

Eleven compiled files. These do not get their types substituted — they move to
`GameEngineDevice`, get excluded from the macOS glob, or get a portable reimplementation. B5 names
them; B6 decides. Ordered by how confidently they can be dealt with.

| File | What it actually is | Recommendation |
|:--|:--|:--|
| `GameNetwork/WOLBrowser/WebBrowser.cpp` + `Include/GameNetwork/WOLBrowser/{WebBrowser,FEBDispatch}.h` | An ATL `CComObject` COM control. See below — this is the worst one and also the easiest. | Move to `Win32Device`. |
| `Common/System/StackDump.cpp` | `dbghelp.dll` symbol resolution and an EBP-chain walk. | Excluded on macOS; C5 owns the replacement. |
| `GameClient/GUI/IMEManager.cpp` | Windows IMM32 input-method editor. | Move to device. The seam already exists — `GameClient.cpp:413` calls `CreateIMEManagerInterface()` and tolerates a null return. |
| `GameClient/ChromaKeyboard.cpp` | `wininet` HTTP against the Razer Chroma SDK's localhost REST server, driven by a `CreateThread` worker. Reached only via `disableChromaKeyboard`/`updateChromaKeyboard`/`shutdownChromaKeyboard` from `CommandLine.cpp:949` and `GameEngine.cpp:229,2150`. | Excluded on macOS behind those three entry points. |
| `Common/System/JobSystem.cpp` | `CreateThread`, `WaitForSingleObject`, `CloseHandle`, `volatile LONG` + interlocked counters. | Not a type fix. The whole file is a platform threading primitive and wants a `std::thread`/`std::atomic` rewrite or a device-owned implementation. Flag prominently for B6 — it is the one bucket-(c) file the engine genuinely needs working. |
| `Common/System/Directory.cpp` + the `WIN32_FIND_DATA` in `Common/Directory.h:49` | `FindFirstFile`/`FindNextFile` directory enumeration. | Belongs behind the `LocalFileSystem` seam C1 already has to build. `FileInfo::set(const WIN32_FIND_DATA&)` is the only Windows type in the header's interface. |
| `Include/Common/Monitors.h` | Header-only GDI: `EnumDisplayMonitors`, `EnumDisplaySettingsA`, `ChangeDisplaySettingsEx`, `RECT`, `CCHDEVICENAME`. Included by `GlobalData.cpp`, `OptionsMenu.cpp`, `W3DDisplay.cpp`, `WinMain.cpp` and the tests. | Device. **Good news:** `GlobalData.h` does *not* include it — the only mention at `GlobalData.h:145` is a comment pointing at it. The leak is contained. |
| `Include/Common/EarlyOptions.h` | Header-only `CreateFileA`, `SHGetKnownFolderPath` by `GetProcAddress`, registry reads. | Device, or a platform-split header. Included by `OptionsCatalog.h`, so it reaches further than it looks. |
| `Include/Common/EarlyCommandLine.h` | Header-only, needs `GetCommandLineW`. Its own comment explains why it must read the *wide* command line. | Platform layer. C2 (entry point) is the natural owner — on macOS `argv` is UTF-8 and the whole reason this header exists goes away. |
| `Include/Common/ScopedMutex.h` | `HANDLE` + `WaitForSingleObject`. | Just move it. **Its only consumer is `GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp`** — it is already device-only code that happens to live in `GameEngine/Include`. |
| `GameNetwork/GameSpy/Thread/PingThread.cpp` | `#include <winsock.h>` plus `icmp.dll` loaded by `GetProcAddress` for `IcmpSendEcho`. | GameSpy. Stub. |
| `GameNetwork/GameSpy/MainMenuUtils.cpp` | `HANDLE s_asyncDNSThreadHandle` (`:79`) and `DWORD WINAPI asyncGethostbynameThreadFunc` (`:676`) — a Win32 thread wrapping `gethostbyname`. | GameSpy. Stub. |
| `GameNetwork/GameSpy/StagingRoomGameInfo.cpp` | An SNMP MIB-II walk — `inetmib1.dll` + `snmpapi.dll` by `LoadLibrary` — to find which local interface is talking to the chat server. The function is `GetLocalChatConnectionAddress` (`:112`). | GameSpy. Stub, or `getifaddrs` if anyone ever cares. |

The last three are all GameSpy, and the README is explicit that GameSpy "has to compile and link,
and that is all it has ever had to do here". Stub them; do not port them.

### The WOLBrowser suspicion: confirmed, and it is worse and easier than you thought

You were right. The evidence, in order of how much it settles the question:

1. **`GameEngine`'s COM dependency is a build-time dependency on `midl.exe`.** `WebBrowser` derives
   from `FEBDispatch<WebBrowser, IBrowserDispatch, &IID_IBrowserDispatch>`, and
   `IBrowserDispatch`/`IID_IBrowserDispatch` come from `BrowserDispatch.h` and `BrowserDispatch_i.c`,
   which do not exist in the tree. `GeneralsMD/Code/CMakeLists.txt:439-463` generates them by running
   MIDL at build time, behind a `find_program(MIDL_EXECUTABLE midl … REQUIRED)`. That is a hard
   **configure-time** requirement on the Windows SDK, caused by one header in the engine library.
   It is also the only reason `atlbase.h` is at `PreRTS.h:50`.

2. **`TheWebBrowser` is always `NULL` and always has been.** It is declared at `WebBrowser.h:129`,
   defined `= NULL` at `WebBrowser.cpp:75`, and **nothing anywhere assigns it**. The only assignment
   site in the tree is `GameEngine.cpp:923`, which is commented out. Every consumer —
   `INIWebpageURL.cpp:92`, `WOLLadderScreen.cpp:73,92`, `WOLLoginMenu.cpp:724,1422,1486` — is already
   written as `if (TheWebBrowser != NULL)`.

3. **The factory seam is already correct.** `GameEngine.h:91` forward-declares `class WebBrowser;`
   and `:134` declares `virtual WebBrowser *createWebBrowser( void ) = 0;` — a pointer to an
   incomplete type, which is exactly the shape the task file holds up as the model. The only
   implementation is `Win32GameEngine.h:115`, `return NEW CComObject<W3DWebBrowser>;`, and it is
   never called.

So the seam is not broken; only the implementation is on the wrong side of it. Moving
`GameNetwork/WOLBrowser/` into `GameEngineDevice/Win32Device/` is behaviour-preserving in the
strongest possible sense — `TheWebBrowser` stays `NULL`, which is what it already is on Windows
today — and it takes `atlbase.h`, the MIDL custom command and the `eabrowserdispatch` library out of
the engine's graph in one move. That is worth doing on Windows first, on its own merits, where it
can be proved.

The `WebBrowser` factory on `GameEngine`, by contrast, should **stay**. It is the abstraction working.

## 2. `ControlServer.cpp` → BSD sockets

840 lines, and the socket surface is 30 lines of it. The file's own comment at `:46-48` says it took
Winsock 1.1 "rather than starting the winsock2 header fight" because `windows.h` had already put it
on the table — it never wanted Winsock, it wanted sockets. Everything it calls is in POSIX.

### Call-by-call

| Winsock, today | Lines | POSIX | Header |
|:--|:--|:--|:--|
| `SOCKET` | 236, 237, 255, 731 | `int` | — |
| `INVALID_SOCKET` | 236, 237, 246, 275, 307, 719, 729, 732, 781, 829 | `-1` | — |
| `closesocket(s)` | 248, 292, 831 | `close(s)` | `<unistd.h>` |
| `ioctlsocket(s, FIONBIO, &ul)` | 258 | `fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) \| O_NONBLOCK)` | `<fcntl.h>` |
| `WSADATA` / `WSAStartup(MAKEWORD(1,1), &d)` | 263, 266 | deleted, with `theWinsockStarted` (`:238`) and the `MAKEWORD` | — |
| `WSACleanup()` | 837 | deleted | — |
| `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` | 274 | identical | `<sys/socket.h>`, `<netinet/in.h>` |
| `sockaddr_in`, `AF_INET`, `htons`, `htonl` | 281-285 | identical | `<netinet/in.h>`, `<arpa/inet.h>` |
| `INADDR_LOOPBACK` | 54 | identical — `0x7f000001` host order on both, and it is `htonl`'d at `:285`, correctly | `<netinet/in.h>` |
| `bind(s, (sockaddr *)&a, sizeof(a))` | 287 | identical; third parameter is `socklen_t` rather than `int`, which the `sizeof` satisfies | |
| `listen(s, CONTROL_BACKLOG)` | 288 | identical | |
| `accept(s, NULL, NULL)` | 731 | identical; returns `int`, `-1` on failure | |
| `recv(s, buf, len, 0)` | 743 | identical; returns `ssize_t` | |
| `send(s, buf, len, 0)` | 338, 480 | identical; returns `ssize_t`, takes `const void *` rather than `const char *` | |
| `WSAGetLastError()` | 765 | `errno` | `<errno.h>` |
| `WSAEWOULDBLOCK` | 765 | `EAGAIN` (`== EWOULDBLOCK` on macOS) | `<errno.h>` |

That is the whole of it. The `#include <vector>` at `:44` and the SHA-1 and base64 at `:72-460` are
already portable; `ControlServer_computeAcceptKey` has test coverage at `test_gameengine.cpp:13031`
that runs on any platform and is the obvious thing to keep green first.

### What does *not* map cleanly

Five items, in descending order of how badly they bite.

**1. `SIGPIPE` will kill the process. This is the one that matters.**

`send()` to a peer that has closed raises `SIGPIPE` on macOS, and the default disposition
terminates the process. Windows has no equivalent — it returns `SOCKET_ERROR` with
`WSAECONNRESET`. There are two `send()` calls, `:338` and `:480`, and **neither checks its return
value**, so on Windows the failure is invisible and harmless and on macOS the same code path kills
the game.

This is not a hypothetical. The normal way a `-control` session ends is the driving script exiting,
and a script that exits between the game's `send()` and the next `recv()` is the ordinary race, not
a rare one. A test harness whose failure mode is "the thing under test dies" is worse than no
harness.

The fix is per-socket and BSD-specific:

```c
const int one = 1;
setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof( one ) );   // macOS/BSD
```

set on the **accepted** socket, next to the existing `setNonBlocking( theClientSocket )` at `:735`.
Note that `MSG_NOSIGNAL` — the flag most people reach for — is the Linux spelling and **macOS does
not have it**; `SO_NOSIGPIPE` is the BSD one. A process-wide `signal( SIGPIPE, SIG_IGN )` would also
work but belongs to C2, not here, and per-socket is the narrower change.

**2. `EINTR` has no Windows counterpart, and the current code treats it as fatal.**

On POSIX, `recv()` and `accept()` can return `-1` with `errno == EINTR` when any signal arrives.
Line `:765` reads:

```c
if (WSAGetLastError() != WSAEWOULDBLOCK)
{
    closeClient();
    return;
}
```

Translated literally, that closes the client connection on the first signal the process takes. There
is no Windows behaviour to preserve here because the case cannot arise on Windows; the POSIX branch
needs `if (errno == EINTR) continue;` ahead of the `EWOULDBLOCK` test. `accept()` at `:732` has the
same exposure, though there the consequence is only a dropped connection attempt.

**3. `SO_REUSEADDR`: correct to omit on Windows, wanted on macOS, and the two cannot share a line.**

The listening socket never sets `SO_REUSEADDR`, and on Windows that is the right call — Windows'
`SO_REUSEADDR` lets a *different process* bind a port that already has a live listener, which would
quietly destroy the "loopback only, therefore no authentication is needed" argument in
`ControlServer.h:28-30`. On BSD and macOS the option means something narrower: it only bypasses the
`TIME_WAIT` lingering of a previous connection on the same local port, and a second live listener is
still refused. So it is both safe and wanted there.

Without it, restarting the game inside the `2 × MSL` window after a `-control` session gets
`EADDRINUSE` from `bind()`. And the failure is not soft — `ControlServer_poll` at `:724` does:

```c
// stop trying: a port that will not bind now will not bind on the next frame either
TheWritableGlobalData->m_controlPort = 0;
```

which disables `-control` for the entire run. For M2 and M4, where the harness is a loop of short
runs on a fixed port, a transient `TIME_WAIT` would present as a silently dead harness with one
line in the debug log. This must be a platform-guarded `setsockopt`, not a shared one — it is the
only place in this file where the two platforms genuinely want different code.

**4. `errno` vs `WSAGetLastError()` — same shape, different values.**

Both are thread-local, so no locking changes. But the values differ, and on macOS
`EAGAIN == EWOULDBLOCK` while on Linux they are permitted to differ. Testing `EAGAIN` alone is
correct on macOS; testing both costs nothing and is what `udp.cpp` already does.

**5. Two narrowing conversions that MSVC tolerates and clang will complain about.**

- `:743` assigns `recv()`'s `ssize_t` to `const Int received`. Harmless — the buffer is 4096 bytes —
  but `-Wshorten-64-to-32` will fire, so it wants an explicit cast.
- `:338` and `:480` cast the length to `Int` for a parameter that is `size_t` on POSIX. The cast
  narrows and then widens straight back. Drop it rather than keep it.

**Not a port problem, noted so nobody re-finds it:** neither `send()` handles a partial write or an
`EAGAIN`, on either platform. In practice every reply this server sends is bounded by a `char[1024]`
(`replyOk` at `:493`) or a `char[512]` (the handshake at `:473`), and the `screenshot` command
replies `"requested"` rather than any image data, so nothing approaches a loopback socket buffer on
either OS. Leave it alone; it is pre-existing and it is not what B5 is for.

### There is already a POSIX branch in this library

Worth knowing before anyone writes this from scratch. `GameEngine/Include/GameNetwork/udp.h` and
`GameEngine/Source/GameNetwork/udp.cpp` are Westwood's original Unix-portable network layer, still
compiled into `gameengine`, and they already carry the exact split this task needs:

- `udp.h:35-52` — the whole `#ifdef _WINDOWS` / `#else` header set: `<netdb.h>`, `<sys/types.h>`,
  `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<unistd.h>`, `<sys/time.h>`, `<fcntl.h>`.
- `udp.cpp:212-231` — `SetBlocking()`, with `ioctlsocket(FIONBIO)` on one side and the
  `fcntl(F_GETFL)` / `fcntl(F_SETFL, … | O_NONBLOCK)` pair on the other. Copy this one; it is the
  house style for the conversion and it is already in the tree.
- `udp.cpp:333-360` — a `WSAGetLastError()` / `errno` value map, error code by error code.
- `udp.cpp:426` — `else if ((retval==-1)&&(errno==EINTR))`, the retry item 2 above needs.

`_WINDOWS` is a global compile definition at `CMakeLists.txt:40`, so A1 simply not defining it for
the macOS toolchain brings that UNIX branch to life for free.

Two caveats, both small and both worth fixing in the same commit: `udp.cpp:130` calls
`closesocket(fd)` in the destructor **outside any guard**, so the UNIX branch as written does not
actually compile; and the `_UNIX` spelling at `udp.h:31` is a third macro nobody defines. The
precedent is real but it has not been built since about 1999. Treat it as a pattern to follow, not
as working code.

## 3. `PreRTS.h:51`, and why nothing else is visible until it moves

`GeneralsMD/Code/GameEngine/Include/Precompiled/PreRTS.h` is the precompiled header for the
`gameengine` target, and `${GE}/Include/Precompiled` is on that target's `PUBLIC` include
directories, so everything that links the engine can see it. Lines 43-96 are Windows, and
`#include <windows.h>` at `:51` is only the loudest of them.

The header's own comment at `:39-41` is honest about how it got there:

> We actually don't use Windows for much other than timeGetTime, but it was included in 40
> different .cpp files, so I bit the bullet and included it here.
> PLEASE DO NOT ABUSE WINDOWS OR IT WILL BE REMOVED ENTIRELY. :-)

That is the whole task, twenty-two years early.

### What is in the block

| Line | | Who actually needs it |
|:--|:--|:--|
| 43 | `#define WIN32_LEAN_AND_MEAN` | — |
| 49, 52 | `#define AnimateWindow AnimateWindow_Win32Unused` / `#undef` | Self-contained; the comment explains it and it leaves with `windows.h` |
| 50 | `<atlbase.h>` | `WOLBrowser` only — leaves with section 1c |
| 51 | `<windows.h>` | everything below, plus the bucket (b) sites |
| 56, 62, 72 | `<direct.h>`, `<io.h>`, `<process.h>` | No macOS equivalent; B3 shims |
| 57, 85 | `<EXCPT.H>`, `<TCHAR.H>` | SEH and `_T()` — B3 / C5 |
| 61 | `<imagehlp.h>` | `StackDump.cpp` only |
| 64 | `<lmcons.h>` | `UNLEN` — one or two sites |
| 69 | `<mmsystem.h>` | `timeGetTime` — **B2** |
| 70, 71 | `<objbase.h>`, `<ocidl.h>` | COM; leaves with `WOLBrowser` |
| 73, 74, 75 | `<shellapi.h>`, `<shlobj.h>`, `<shlguid.h>` | `EarlyOptions.h`, Registry |
| 76 | `<snmp.h>` | `StagingRoomGameInfo.cpp` only |
| 87 | `<vfw.h>` | Video for Windows — nothing; video is FFmpeg |
| 88, 89, 90 | `<winerror.h>`, `<wininet.h>`, `<winreg.h>` | `HRESULT` values, `ChromaKeyboard.cpp`, Registry |
| 92-96 | `DIRECTINPUT_VERSION`, `<dinput.h>` | **Nothing in `GameEngine`.** DirectInput is `Win32Device`. This has no business in an engine PCH at all. |

What remains after the guard is genuinely portable: `<assert.h>`, `<ctype.h>`, `<float.h>`,
`<math.h>`, `<memory.h>`, `<stdarg.h>`, `<stddef.h>`, `<stdio.h>`, `<stdlib.h>`, `<string.h>`,
`<sys/stat.h>`, `<sys/types.h>`, `<time.h>`, and then `Lib/Basetype.h` and the five engine headers
at `:113-120`. `<sys/timeb.h>` at `:83` exists on macOS but is deprecated; it is B3's call whether
to keep it.

### What it would take, in the order it has to happen

The sequencing is the whole point, and getting it backwards costs a day.

1. **Give every file that genuinely needs Windows its own `#include`s, while the PCH still provides
   them.** Right now every bucket-(c) file gets its Windows declarations from the PCH and declares
   nothing itself — `ChromaKeyboard.cpp:23` includes `<wininet.h>` but relies on the PCH for
   `HANDLE` and `CreateThread`; `StackDump.cpp` names `IMAGEHLP_LINE64` with no include of its own.
   Adding those includes is **a no-op on Windows** — the PCH already supplied them, so the
   preprocessor sees the same declarations and the compiler produces the same command line and the
   same object. It is the one step in B5 that can be made with confidence without a Windows machine,
   and it should be its own commit.
2. **Then wrap 43-96 in `#if defined(_WIN32)`.** At this point the Windows build is unchanged by
   construction and the macOS build starts producing honest errors.
3. **Then the bucket (a) fixes become visible.** Until step 2 they are invisible, because the PCH
   silently satisfies every one of them. This is the task file's point at its step 2 and it is
   correct — but note the direction: the conditional is not the *last* fix, it is the thing that
   turns the other twenty into compile errors you can work down a list. Do it early, not last.
4. **`GameEngineDevice`, `Main` and `Tools` are unaffected.** They are Windows targets and stay
   Windows targets. B5 must not touch them, per the task file.

One thing to watch that the tree already knows about: `WindowMode.h:26-27` records that
`BaseType.h` and `windef.h` fight over the `BitTest` macro. Once `windows.h` is gone on macOS that
conflict simply does not arise; the MSVC branch keeps whatever it does today, unchanged.

## What this changes about B5's shape

- **The type substitutions are an afternoon.** Seven files, and two of them (`LookAtXlat.cpp`,
  `AsciiString.h`) are single-line deletes.
- **`ControlServer.cpp` is a day**, most of it spent on the four things in section 2 that are not
  transliteration, and it is the half worth doing carefully because M2 and M4 depend on the result.
  `SO_NOSIGPIPE` is not optional.
- **Bucket (c) is the rest of the task and it is mostly B6's decision, not B5's.** Eleven files.
  Three are GameSpy stubs, four are device moves, two are owned by other tasks (C5, C1), one
  (`ScopedMutex.h`) is a one-line `git mv` to where its only caller already lives, and one
  (`JobSystem.cpp`) is real engineering that nothing else in the plan currently accounts for.
- **`JobSystem.cpp` is the unbudgeted item.** It is the only bucket-(c) file the engine actually
  needs working on macOS, and it is a Win32 thread pool with interlocked counters, not a type leak.
  It is not in any task file. Somebody should own it before M1.
