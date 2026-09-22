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

**Seven case-sensitivity bugs already exist in the tree**, found by B9 and not caused by it:
`GameLogic/Weaponset.h` (4 sites; the file is `WeaponSet.h`), `WWMath/Vector3.h` (the file is
`vector3.h`), `../wwmath/rect.h` and `../wwlib/argv.h`. They resolve on MSVC and on a default APFS
volume, and break on a case-sensitive one or a Linux CI runner. They are include paths rather than
game data, so they are not strictly C1's — but they will be the first thing a case-sensitive volume
reports, and knowing that in advance saves an afternoon. E2 should care too if CI ever runs on
Linux.

Windows: `Win32Device` untouched, full build and `ctest` green.

## Also here: the replay writer

Found by B1 and not fixable there. `Recorder.cpp` opens the replay file `"wb"` and then mixes
byte-oriented `fprintf`/`fwrite` with wide-oriented `fwprintf`/`fputwc`/`fgetwc` on the **same
`FILE*`**. That is undefined behaviour. MSVC tolerates it; a POSIX C library sets the stream's
orientation on first use and then fails every call of the other kind, silently as far as the caller
is concerned.

So the replay writer does not work on macOS at all, and replays are how this project proves a
change leaves the simulation alone. Rewrite it byte-oriented — the file format does not change, the
calls that produce it do. Do this early in C1: E1 and every later milestone lean on replays.

## Do not

- Do not change `GameEngine`'s factory interface. If a factory does not fit macOS, that is worth
  discussing on this task before it becomes a change every platform pays for.
- Do not implement input or audio here. Null implementations, and C3/C4 own the real ones.
