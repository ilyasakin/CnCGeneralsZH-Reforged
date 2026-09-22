# C1 — MacGameEngine and file systems

- **Milestone:** M2
- **Depends on:** B6
- **Blocks:** C2 C5
- **Status:** not started
- **Size:** mirrors `GameEngineDevice/Source/Win32Device`, 10 files / 2,756 lines

## Why

`GameEngine` is an abstract class with a pure-virtual factory for every subsystem
(`GameEngine.h:124–136`): `createLocalFileSystem`, `createArchiveFileSystem`, `createGameLogic`,
`createGameClient`, `createAudioManager`, `createRadar`, `createWebBrowser` and the rest.
`Win32GameEngine` implements them. That seam is the port's front door, and it was built in 2003 —
this task walks through it rather than cutting a new one.

## Scope

New: `GameEngineDevice/Source/MacDevice/` and `GameEngineDevice/Include/MacDevice/`, laid out to
mirror `Win32Device/`. What is there now:

| File | Lines | Mac equivalent |
|:--|:--|:--|
| `Win32GameEngine.cpp` | the factory and the service loop | `MacGameEngine` |
| `Win32LocalFileSystem.cpp` | real files | POSIX |
| `Win32LocalFile.cpp` | one open file | POSIX |
| `Win32BIGFileSystem.cpp` | mounts `.big` archives | mostly portable already |
| `Win32BIGFile.cpp` | one file inside an archive | mostly portable already |
| `Win32CDManager.cpp` | the disc check | a null implementation; there is no disc |
| `Win32OSDisplay.cpp` | message boxes | minimal for headless |
| `Win32Mouse.cpp`, `Win32DIKeyboard.cpp`, `Win32DIMouse.cpp` | input | **not this task** — C3 |

`Win32DIMouse.cpp` is already excluded from the build (`CMakeLists.txt` line ~549: the shipping
build uses the Win32 message mouse).

## Do

1. `MacGameEngine : public GameEngine`, implementing the same factories `Win32GameEngine` does.
   For M2, `createGameClient` returns a headless client and `createAudioManager` returns the
   existing `Stubs/NullAudioManager.h`. C3 and C4 replace them.
2. The `.big` reader is the piece to treat carefully. It is an archive format with its own
   internal paths, and those paths use backslashes and are case-sensitive in ways a Windows
   filesystem hid. Note that macOS's default APFS is case-insensitive, so a naive port will appear
   to work on the porting machine and fail on a case-sensitive volume. **Test on a case-sensitive
   APFS volume**; creating one with Disk Utility takes a minute and is the only way this bug gets
   caught before a user finds it.
3. The local filesystem is where B3's leftover path-separator work lands. Normalise at this
   boundary and nowhere else: archive-internal paths keep their backslashes, real paths get
   slashes, and the conversion happens here where it can be read in one place.
4. `Win32CDManager` becomes a null manager. Say so in a comment with the reason, rather than
   leaving a reader wondering where the disc check went.

## Done when

`MacGameEngine` constructs, mounts a Zero Hour install's `.big` files from `GeneralsMD/Run/`, and
reads a known asset out of one with a byte-identical result to the Windows build. That comparison
is a test, checked into `Tests/`.

Verified on a case-sensitive volume as well as the default one.

Windows: `Win32Device` untouched, full build and `ctest` green.

## Do not

- Do not change `GameEngine`'s factory interface. If a factory does not fit macOS, that is worth
  discussing on this task before it becomes a change every platform pays for.
- Do not implement input or audio here. Null implementations, and C3/C4 own the real ones.
