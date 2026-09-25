# C1 — MacGameEngine and file systems

- **Milestone:** M2
- **Depends on:** B6
- **Blocks:** C2 C5
- **Status:** in progress, -a9: path-resolution design approved (D1-D7, 2026-09-26), PR (a) the resolver under way; the `mixfile.cpp` piece is done
- **Size:** mirrors `GameEngineDevice/Source/Win32Device`, 10 files / 2,756 lines

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** The engine subclass and its file systems are POSIX code that builds on macOS and Linux both; name new files for what they are (`Posix*` for the file systems, the SDL-backed pieces after SDL) rather than `Mac*`. Darwin-only calls go behind `__APPLE__` as a refinement of the POSIX path.

## Path resolution: the design (approved 2026-09-26, decisions D1-D7)

**Why this needed a design before code: normalising paths would change the INI CRC.**
`INI::loadDirectory` (`INI.cpp:219`) lists a directory into a `FilenameList`, which is a set
ordered by `strcasecmp`. It loads the files with no separator after the directory first, then the
rest in that order. `'\'` is 0x5C and `'/'` is 0x2F, which sort on opposite sides of the digits
(0x30-0x39). So a listing whose joins were normalised to `/` loads subdirectory INIs in a different
order from Windows. That order feeds the INI CRC, which multiplayer and replays compare. The same
kind of dependency runs through:

- `Maps\MapCache.ini`'s keys, lowercased, with the backslash escaped as `_5C_`;
- the portable save and map paths (`GameState.cpp`: `"Save\"`, `"Maps\"`, lowercased prefixes);
- about 100 `'\'` joins and splits in engine code;
- the archive keys.

So the engine's strings are never normalised.

**D1. Windows spelling end to end, one resolver at the OS boundary.** Engine code keeps building and
comparing paths exactly as on Windows. Only the string handed to `open()`, `stat()`, `rename()` and
the like is translated, by one resolver, and nothing else in the tree converts separators or case.

**The resolver (D7: in WWLib, `posixpath.{h,cpp}`, POSIX only).** WWLib is the lowest library that
both `RawFileClass` and GameEngine link. The resolver has to work before any subsystem exists: Debug,
MemoryInit and EarlyOptions open files first.

- **Separators and roots.** `'\'` and `'/'` both separate. A leading `'/'` is absolute; anything
  else is relative to the process's current directory, which is the install root (below). A drive
  letter or a UNC path is refused: it can only come from Windows registry data.
- **Matching.** Each component is tried exact first. On a miss it is matched case-insensitively,
  with ASCII folding, against a per-directory listing cache.
  - On a case-insensitive volume (default APFS, the exFAT the Steam installs sit on) the exact
    lookup always succeeds and the cache is never touched.
  - The cache is dropped by the resolver's own create, delete and rename, and re-read once on a
    repeat miss, so a change made outside the game is seen too.
- **D2, ambiguity.** When two entries on a case-sensitive volume differ only in case: an exact
  match wins; otherwise the byte-order-first candidate, with one log line naming both. Windows cannot
  have this case, so any rule is ours; this one is deterministic.
- **Intents.**
  - Existing: every component must exist.
  - Create-leaf: all but the last must exist, and a new last component keeps the engine's spelling,
    as Windows would create it.
  - Create-path: for the write walk.
- **What it fixes on the real install.** The survey found the code's spelling and the disk's apart
  in several places:
  - Win32Mouse asks for `data\cursors\SCCPointer.ANI`; the disk has `Data/Cursors/sccpointer.ani`.
  - Bink asks for `Data/english/Movies/EA_LOGO.bik`; Zero Hour has `Data/English/Movies/EA_LOGO.BIK`.
  - `genseczh.big` is `gensecZH.big` on disk.
  - `MapUtil` and `GameState` lowercase whole absolute paths.

**The local file system (`PosixLocalFileSystem`, `PosixLocalFile`).**

- **openFile, writing.** It creates the directories along the path the way Win32's walk does,
  including its "the first dotted token is the file" rule. Win32's walk uses `nextToken`, which
  drops a leading `'/'`, so that code is not reused as it is.
- **LocalFile's `_open` family** becomes portable.
- **getFileListInDirectory** reads the directory with `opendir`/`readdir`, keeping Windows' wildcard
  semantics:
  - case-insensitive, so `*.ini` matches `.INI`;
  - the `*.` rule: it recurses only into subdirectories without a dot.

  It returns names exactly as Win32 does: `originalDirectory + currentDirectory + on-disk name`, with
  `'\'` joins on recursion. The list is byte-identical to Windows for the same files.
- **getFileInfo** converts to FILETIME units (100 ns since 1601).
- **createDirectory** stays one level.

**D3, LocalFileSystem gains copyFile, deleteFile, moveFile(replace) and a lister that never changes
directory.** `Win32LocalFileSystem` implements them with exactly the calls the sites make today. The
sites that change directory, list `*` and change back (`Directory.cpp`, `GameState`, `GameStateMap`,
`InGameUI`) become explicit-directory listings, which changes the Windows code path. The condition:
the Wine listing oracle must show the Windows `FilenameList` byte-identical before and after that
conversion, for the real install's `Data\INI` and for the test fixture, in the same PR.

**D4, `zh_*` forwarders for the raw CRT sites** (`fopen`, `open`, `remove`, `rename`, `stat`).
About 60 sites, many of which run before `TheLocalFileSystem` exists. On Windows each forwarder is
the CRT call, verbatim. Off Windows it resolves the path first. They are not MSVCCompat-style
shims: they add behaviour on POSIX, which is why they do not borrow the standard names, and their
header says so. `RawFileClass`'s POSIX open goes through the resolver too, which covers
`_TheWritingFileFactory`: screenshots, `INIClass::Save` and wwprofile.

**Roots.**

- **The install root** is the process's current directory, set once at start-up (where WinMain
  changes to the executable's directory today) and never changed afterwards. That is why the
  listers above must not change it. Where the root comes from (the app bundle or a Steam path) is
  C2's and C5's question.
- **D5, the user-data directory.**
  - **Where.**
    - macOS: `~/Library/Application Support/Command and Conquer Generals Zero Hour Data/`.
    - Linux: `$XDG_DATA_HOME/Command and Conquer Generals Zero Hour Data/`, defaulting to
      `~/.local/share/...`.
    - The same leaf name as Windows' default. There is no registry off Windows; `ZH_USER_DATA_DIR`
      overrides the location, for tests and portable installs.
  - **Behaviour.** The directory is created if missing. The value keeps Windows' trailing `'\'`
    (`EarlyOptions.h`: `"%s\%s\"`), so every `endsWith("\")` and `+ "Save\"` works unchanged.
  - **A product point to revisit.** A macOS player cannot easily browse to Application Support for
    their replays and saves: it is hidden in Finder. A Documents location, which is what Windows
    uses, was considered and rejected, because Apple's guidelines keep an application's own data in
    Application Support and leave Documents to documents the user makes. The user may want to
    revisit this.

**D6, text mode.** The Windows CRT's text mode turns CRLF into LF on read, so POSIX reads opened
`TEXT` strip a `'\r'` before a `'\n'`. Writes stay LF. Windows-written files are CRLF, and both
platforms read either.

The CRT's text mode also treats Ctrl-Z (0x1A) as end of file. That is **not** emulated, because no
game text contains one. Measured on both installs:

- 9 loose text files (the `.lcf`, `.txt` and `Data/Scripts/Scripts.ini` files): none;
- 825 text entries in the 54 `.big` archives (556 `.ini`, 240 `.wnd`, 16 `.txt`, 13 `.str`): none.

The only 0x1A bytes on the installs are in `Game.dat`, `Generals.dat`, `langdata.dat` and
`patchget.dat`, which are binary and never opened in text mode. INI files are opened binary on
Windows too (`File::open` defaults to `BINARY`), so their parser already copes with `'\r'`.

**Staging**, each piece a PR for a second read:

- (a) the resolver, the `zh_*` forwarders and `test_posixpath`;
- (b) `LocalFile` made portable, `PosixLocalFileSystem`, and the listing and order tests;
- (g) the Recorder rewritten byte-oriented, pulled forward because E1 and every later milestone lean
  on replays;
- (c) the LocalFileSystem additions and -18's worklist (B5 `f52451d8`), including defect 11's leaked
  find handle, with the before-and-after oracle;
- (d) the raw sites moved to `zh_*`, `RawFileClass`, and the joins onto the executable's directory;
- (e) the user-data directory;
- (f) `PosixBIGFileSystem`, the POSIX engine subclass, the null CD manager, and the byte-identical
  asset test.

Linux is a constraint on how the code is written, kept POSIX-generic, not a gate on each piece (the
user's macOS-first direction, plan `74750f5a`).

**Testing case.** The worktrees and the Steam install volume are case-insensitive, so a default macOS
run proves nothing about case. The proof is a small case-sensitive APFS image that the tests create
and attach with `hdiutil`. That run is mandatory: an image that cannot be created or attached, or
that turns out not to be case-sensitive, fails the test. The Wine listing oracle stays alongside it,
because Windows equivalence is a different question from case.

## PR (b): LocalFile, PosixLocalFileSystem and the listing (2026-09-26)

**What is in it.**

- **WWLib's listing.** `PosixPath_Matches_Pattern` is FindFirstFile's matching for the engine's
  patterns (`*.ini`, `*.big`, `Patch*.big`, `*`, `*.w3d`, `*.tga`, the empty pattern, and `*.` for
  directories), case-insensitive. `PosixPath_List_Like_Win32` is `getFileListInDirectory`: names come
  out as `originalDirectory + currentDirectory + on-disk name`, recursion joins with `'\'`, and
  entries are visited in byte order. So when a case-sensitive volume holds two names differing only
  in case, the engine's case-insensitive set keeps the same one every time (D2's rule).
- **`zh_read_text`** (WWLib, `zhio.h`) is D6: `_read` on an `_O_TEXT` file. A `'\r'` at the end of
  a read is settled by one more byte, put back unless it is the `'\n'`, so the position counts file
  bytes and `LocalFile`'s one-byte-then-seek-back scanners behave as on Windows.
- **`LocalFile.cpp`** calls named helpers (`openFile`, `readFile`, `writeFile`, `seekFile`,
  `closeFile`) and `OPEN_*` flags. On Windows each is the CRT call it replaced; off Windows they
  are `zh_open` and `zh_read_text`. `RAMFile.cpp` and `StreamingArchiveFile.cpp` included `<io.h>`
  without using it; it is now Windows-only.
- **`PosixLocalFileSystem`, `PosixLocalFile`**, in `GameEngineDevice/{Include,Source}/PosixDevice/`,
  built as `posixdevice`. It stays out of the default macOS build for as long as `gameengine` does.
  - `openFile` walks the directories as Win32 does, but puts back the leading `'/'` that
    `nextToken` drops, and stops when the name runs out. On Windows a name with no `'.'` made that
    walk loop for ever.
  - `getFileInfo` gives FILETIME units (100 ns since 1601) and size 0 for a directory, as
    FindFirstFile does.
  - `createDirectory` makes one level and does not impose `_MAX_DIR`.
  - The pool is `PosixLocalFile`, with a POSIX-only row in `MemoryInit.cpp` sized like
    `Win32LocalFile`'s. No MemoryPools.ini names either.
- **Why `PosixDevice/` and not `MacDevice/`, as the scope below first said:** these classes are
  POSIX. Linux uses them unchanged, and a `MacDevice/` path would be wrong on Linux. `MacDevice/` is
  kept for what only macOS has: `MacGameEngine`, and C3's input.

**Tests.**

- `test_posixpath`: 4 tests, 128 checks, on `$TMPDIR` and on the mandatory case-sensitive APFS
  image. It covers the listing contract on a fixture (dotted directories, a directory named
  `zdir.ini`, wrong-case directory spellings, two names differing only in case), every pattern
  shape, and text reads at every chunk size from 1 to past the end, plus the seek-back.
- Three listing mutations were each caught: case-sensitive matching (11 checks red), no byte-order
  sort (7), and recursing into dotted directories (7).
- **Wine oracle** (`Tests/fs_oracle.cpp`). The mingw-w64 build runs a verbatim copy of the Win32
  listing (std::string for AsciiString) and `_read` on `_O_TEXT` files; the native build runs the
  WWLib functions. Both print the resulting `FilenameList` in its own order and, for `text`, how
  every file reads at chunk sizes 1-8 and 4096 (length, FNV-1a, final position).
  - Run in bottle `zh-e4` on a case-sensitive APFS image holding all 512 text entries (`.ini`,
    `.wnd`, `.str`, `.txt`, `.csf`) from both installs' archives, and a 35-file quirk fixture
    (awkward CR placements, `_` names that sort between the cases, dotted and extension-named
    directories, `Patch*.big` variants).
  - **32 cases, all byte-identical.** They cover every engine pattern, wrong-case directories, an
    empty `originalDirectory`, a missing directory, INI::loadDirectory's call on both installs'
    `Data\INI` (135 and 92 files), and text reads of all 509 `.ini`/`.wnd`/`.str`/`.txt` entries.
  - A POSIX build mutated to recurse into dotted directories and skip CR stripping differed in 14
    of the 32 cases. The oracle can fail.
  - **What it compares against:** the bottle's `kernel32` and `msvcrt` are Wine's builtins, not
    Microsoft's. No genuine CRT is on this machine. The oracle is Wine's reimplementation of
    Windows' behaviour.
  - **Found:** the `.csf` string tables contain 0x1A, and Wine's text-mode reads stop there. That is
    harmless: GameText opens `.csf` `BINARY` (`GameText.cpp:958`, `:1002`), so D6's premise holds.
    Nothing the engine reads in `TEXT` mode contains a Ctrl-Z.

**Not emulated: 8.3 short names.** Windows also matches a pattern against each file's short name,
so `*.ini` can find `x.inix` through `X~1.INI`. Nothing the game ships depends on it, short names
are often disabled on NTFS, and Wine does not generate them either, so the oracle cannot see it.
`posixpath.h` says so beside the matcher.

**Found for (f): AppleDouble files.** macOS writes `._name` companions on exFAT. The installs hold 20
`._*.big` beside Zero Hour's 20 archives and 18 beside Generals' 18 (535 and 356 `._` files in
all). They are real files: `*.big` matches them on POSIX, and FindFirstFile would too on that
volume. They are 4 KB, start `00 05 16 07`, and are not BIG archives. (f) decides whether the
archive mount skips `._*` or refuses non-`BIGF` files quietly. The listing reports them, as Windows
would.

**Open.** The linked `PosixLocalFileSystem` test waits on -18's GameMemory seam, which is not on
`feature/mac-port` as of `b691ae98`. `gameengine` on macOS is down from 100 failing objects to 97:
`LocalFile`, `RAMFile` and `StreamingArchiveFile` compile, and `GameMemory` and `MemoryInit` are
next (seam, and PR (d)).

**Tooling.** `windows_view_diff.py` now turns quotes in `#error`/`#warning` text into backquotes on
both sides, because unifdef read the apostrophe in `posixpath.h`'s `#error` as an unterminated
character literal and refused the file.

## Why

`GameEngine` is an abstract class with a pure-virtual factory for every subsystem
(`GameEngine.h:124–136`): `createLocalFileSystem`, `createArchiveFileSystem`, `createGameLogic`,
`createGameClient`, `createAudioManager`, `createRadar`, `createWebBrowser` and the rest.
`Win32GameEngine` implements them. That seam is the port's front door, and it was built in 2003 —
this task walks through it rather than cutting a new one.

## Scope

New: `GameEngineDevice/{Source,Include}/PosixDevice/` for the POSIX classes, and `MacDevice/` for
what only macOS has, laid out to mirror `Win32Device/` (PR (b) above says why). What is there now:

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

## First piece: `WWLib/mixfile.cpp`, 2026-09-25

Taken only to get `wwlib` to archive, and it does. `libwwlib.a` builds on macOS with all 66 sources,
and `-all_load` of it with `libwwdebug.a` resolves every symbol.

- **The `.mix` reader is not on Zero Hour's path.** Its one user is WW3D2's
  `ThumbnailManagerClass::Create_Thumbnails`. That is reached only from `Pre_Init` and
  `Add_Thumbnail_Manager`, and neither has a caller. It still links, because `TextureLoader::Init`
  calls `ThumbnailManagerClass::Init`, so excluding the file would have handed the D-track
  unresolved symbols. It was ported instead.
- **`_splitpath`, the drive-letter question**, arises in `Flush_Changes`, which rewrites a mix file
  in place with raw `DeleteFile`/`MoveFile`, a real-filesystem boundary of its own. On POSIX the
  directory is everything up to the last `/`. There is no drive, and a backslash is an ordinary
  filename character. **It does not normalise:** by this rule that is the local file system's job,
  in one place, and doing it here would make a second.
- **The format itself was broken on LP64.** `MIXFILE_HEADER` and the reader's `FileInfoStruct` were
  `long`s read straight off disk, 8 bytes each on macOS, against the writer's explicit 4-byte fields.
  Every mix file would have misread, including this build's own. They are pinned to `sint32`/`uint32`.
- **Tests** (`test_wwlib`):
  - The writer is checked against 90 bytes built by hand in Python, not by the code under test. That
    also confirms `CRC_Stringi` is standard CRC-32.
  - The reader and `Flush_Changes` are checked against those same bytes.
  - Green on the default volume and on a case-sensitive APFS image; a probe confirmed the image
    really is case-sensitive.
  - Putting `long` back turns 15 checks red.
  - The archive-internal name `dir\B.TXT` keeps its backslash in the file, as this task's rule
    requires.
- `Add_Files`/`Setup_Mix_File`, a makemix developer tool with no callers, stays Win32 only.

**For the rest of C1, found on the way:**

- `_TheFileFactory` is overridden by `W3DFileSystem`, but **`_TheWritingFileFactory` never is**. So
  `ww3d.cpp:1412`/`:1442`, `wwprofile.cpp:576` and `INIClass::Save` (`ini.cpp:629`) write through
  WWLib's `RawFileClass` directly: a second real-filesystem boundary outside the engine's local
  file system. "Normalise in one place" needs to know it exists.
- Pre-existing defects in `Flush_Changes`, on both platforms, in code Zero Hour never runs:
  - It deletes the original mix even when `Get_Temp_Filename` found no free name, and then renames
    nothing over it. That loses the file.
  - It deletes and renames `MixFilename` raw, while reading it through the factory's
    sub-directory. With a non-empty sub-directory (texturethumbnail sets
    `..\\data\\client\\mixfiles\\`) the two refer to different files.
  - Both behaviours are mirrored, not fixed: fixing either changes Windows.
- The class's contract is easy to trip over: `Delete_File` only edits a list that
  `Build_Internal_Filename_List()` fills. Without that call, a delete-and-flush silently does nothing.
  The test does it that way and says so.

## Do not

- Do not change `GameEngine`'s factory interface. If a factory does not fit macOS, that is worth
  discussing on this task before it becomes a change every platform pays for.
- Do not implement input or audio here. Null implementations, and C3/C4 own the real ones.
