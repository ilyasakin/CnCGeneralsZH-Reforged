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
  asset test. **Requirement:** an archive the listing finds that does not start `BIGF` is skipped
  quietly, with a log line once per file, not mounted and not fatal. That covers macOS's AppleDouble
  `._*.big` companions (see "Found for (f)" below) and whatever else a copy from another file system
  leaves behind. A name rule would catch only the first. **And:** `GameEngine::init` deletes
  `Data\INI\INIZH.big` (patch 1.01's cleanup), and the Steam install on this machine has that
  file. On Windows the game does the same. No test may start the engine with the Steam install as
  its root: use a copy, or a read-only mount of it (C1 (c) found this).

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
    `nextToken` drops, and stops when the name runs out. On Windows a last component with no
    `'.'` runs that walk until AsciiString's 32767-character ceiling throws: defect 12 in the
    README, not reachable from the game today.
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
volume. They are 4 KB, start `00 05 16 07`, and are not BIG archives. Decided (PM, 2026-09-26):
(f) rejects any archive that does not start `BIGF`, quietly, logging once per file, rather than
skipping by name. The requirement is on (f) in the staging list above. The listing reports them, as
Windows would.

**gameengine on macOS** was down from 100 failing objects to 97 when (b) merged: `LocalFile`,
`RAMFile` and `StreamingArchiveFile` compile.

**The linked test (follow-up, after -18's GameMemory seam merged).** `test_posixlocalfilesystem` is
built from the real sources, because `gameengine` does not link on macOS yet: `GameMemory`,
`MemoryInit`, `LocalFile`, `File`, `RAMFile`, `AsciiString` and the two PosixDevice classes, plus
what they pull in. Eight names those sources reach and the test does not are stand-ins in its
`_stubs.cpp`, each with its reason: Debug.cpp's two, which does not compile yet, and INI's and
FileSystem's, which can be reached only through subsystem init and `RAMFile::open(name)`. The
unreachable ones abort if reached.

- It runs on `$TMPDIR` and on the case-sensitive image: 3 tests, 95 checks, all pass.
- What it covers:
  - the `FilenameList` INI::loadDirectory walks, in its order. That includes `Default2.ini`
    before `Default\x.ini`, which holds only while `'\'` is kept: the INI-CRC point;
  - case twins keeping the same entry;
  - `openFile`'s walk, relative and absolute, reusing a directory that exists in another case;
  - LocalFile's `scanInt`, `scanString`, `scanReal` and `nextLine` over a CRLF file opened `TEXT`,
    with `size()` still counting bytes;
  - `getFileInfo`'s FILETIME against a time set with `utimes`;
  - `createDirectory`;
  - the `PosixLocalFile` pool size.
- Two mutations were each caught: dropping the root that `openFile` puts back (6 red), and reading
  `TEXT` as binary (6 red).
- **It found a startup exit.** `initMemoryManager`'s link check counts the calls three `new`/`delete`
  pairs make, and clang `-O2` omitted all of them, which C++14 allows. The count stayed 0 and the
  process `exit(-1)`ed before `main`'s first test printed anything, as a macOS Release game would
  have done at startup. `GameMemory.cpp` now holds the pointer `volatile`. That is in the README's
  latent-trap list, and has a WINDOWS-DEBT row.
- Not on Linux yet, for `fpucontrol_selfcheck`'s reason (BaseType.h's min/max against libstdc++):
  registered DISABLED there.

**Tooling.** `windows_view_diff.py` now turns quotes in `#error`/`#warning` text into backquotes on
both sides, because unifdef read the apostrophe in `posixpath.h`'s `#error` as an unterminated
character literal and refused the file.

## PR (c): the file operations behind LocalFileSystem (2026-09-26)

**The calls (decision D3).** `LocalFileSystem` gains five calls.

| Call | Windows (`Win32LocalFileSystem`) | POSIX (`PosixLocalFileSystem`) |
|:--|:--|:--|
| `copyFile(from, to, failIfExists)` | `CopyFileA` | a copy that keeps the last write time, as CopyFile does, and never copies a file onto itself |
| `deleteFile(path)` | `DeleteFileA` | `zh_unlink` (new in WWLib): a file, never a directory, as DeleteFile does |
| `moveFileReplacing(from, to)` | `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING)` | `zh_rename` |
| `getFilesInDirectory(dir, pattern, names)` | `FindFirstFileA(dir\pattern)`, files only, in the order found | the listing of PR (b), without recursion, in byte order |
| `getCurrentDirectory()` | `GetCurrentDirectoryA` | `getcwd` |

The ANSI build's `CopyFile`, `DeleteFile` and the rest already were the `A` functions. Nothing runs
between a failing call and the caller's `GetLastError`.

**The sites.** -18's worklist in B5's task file is done:

- `GameState` makes the save folder, and lists saves by path. Its menu callback now opens
  `getFilePathInSaveDirectory(leaf)`; it used to open the bare leaf, which worked only because of
  the `chdir`.
- `GameStateMap`'s scratch-map cleanup lists first, then deletes, by path.
- `Recorder`'s three copies.
- `GameEngine.cpp`:
  - the checksum cache's replace;
  - the "not in <dir>" message;
  - patch 1.01's `INIZH.big` delete. It moved from just after `createFileSystem` to just after
    `TheLocalFileSystem` starts, because it now goes through it. Nothing between the two places
    touches the file, and the archives still mount after.
- `InGameUI`'s checkpoint folders and cleanup.
- `Image.cpp`'s mapped-images probe (defect 11 fixed).
- `PopupReplay` and `ReplayMenu`'s delete and copy. Their POSIX error text is `strerror`, as B5's
  was.
- `PeerDefs`' three folders.

**Not on the list after all:**
- `Directory.cpp` and `Directory.h` are whole-file `#if (0)`; nothing includes them.
- `EarlyOptions.h` has no live site any more.

**Still failing on macOS, and not (c)'s:**
- `ReplayMenu.cpp`'s `SHGetSpecialFolderLocation` for the Desktop. That is a user-folder question,
  for (e).
- The `'\'` joins after `getExecutableDirectory`, which are (d)'s.

**The condition: the Windows listing before and after, under Wine.** `fs_oracle dir-before` is the
old code: remember the directory, change in, search, change back. `fs_oracle dir-after` is
`Win32LocalFileSystem::getFilesInDirectory`. Both are verbatim but for `std::string`, built with
mingw-w64 and run in `zh-e4`.
- **12 cases, byte-identical, including order.** They covered:
  - the real install's `Data\INI` (with and without a trailing `'\'`, and `data\ini` with
    `*.big`), `Data`, the install root (99 files) and an empty directory argument;
  - on the case-sensitive image, a save-folder fixture: saves in both cases, a scratch `.map`, a
    directory named `dir.sav`, and the checkpoint folder with `*.sav`;
  - a wrong-case `FIXTURE\save`;
  - the quirk fixture's `Data\INI` with `*.ini`.
- **2 cases differ, by design:** a missing directory, run from a folder that has files. Before, the
  failed `SetCurrentDirectory` was ignored and the search listed the current folder (6 files, among
  them `Map Scratch.map`, which `clearScratchPadMaps` would have deleted). After, it lists nothing.
  That is defect 16, fixed.
- `PosixLocalFileSystem`'s listing found the same set in all 14 cases.
- The Win32 bodies were also compiled, verbatim, against mingw-w64's `<windows.h>` (`-Wall
  -Wextra`, clean). As always, the Wine and mingw pieces are not Microsoft's.

**Tests.**
- `test_posixlocalfilesystem`: 151 checks on both volumes. They cover:
  - `copyFile`'s bytes and kept write time, `failIfExists`, replacing, copying onto itself, a
    missing source, a directory as source;
  - `deleteFile` on a file, on a missing one, and on an empty directory it must leave;
  - `moveFileReplacing` over an existing file;
  - `getFilesInDirectory`'s pattern, files only, a missing directory, no trailing separator;
  - `getCurrentDirectory`.
- `test_posixpath`: `zh_unlink`, 138 checks.

**Windows-visible changes, beyond the calls moving:**
- defect 16's fix;
- `Image.cpp` counts files, so a directory named `*.ini` no longer triggers a load that found
  nothing anyway;
- the three `CreateDirectory` sites now pass `Win32LocalFileSystem::createDirectory`'s `_MAX_DIR`
  (256) check, where `CreateDirectory` took up to `MAX_PATH` (260).

WINDOWS-DEBT has the rows.

## PR (e): the user data directory and the Desktop (2026-09-26)

**The user data directory (D5), `EarlyOptions.h`'s POSIX `findUserDataDirectory`.**
- **Where:**
  - macOS: `~/Library/Application Support/Command and Conquer Generals Zero Hour Data`;
  - Linux: `$XDG_DATA_HOME/Command and Conquer Generals Zero Hour Data`, with `$XDG_DATA_HOME` an
    absolute path or else `~/.local/share`;
  - `ZH_USER_DATA_DIR`, when set, is the directory itself.
- **Home** is `$HOME`, or the password database's entry when `$HOME` is unset.
- **Made on first use**, with any missing parents.
- **Decided once a process**, as the Windows branch is.
- **Keeps Windows' trailing `'\'`**, so `getPath_UserData() + "Save\"` and its kind are unchanged
  and resolve through posixpath.
- **No localized leaf off Windows:** there is no installer registry to name one.
- **The path composition is a pure function**, `composeUserDataDirectory(convention, override,
  home, dataHome)`, and the convention is a parameter, not an `#if`. So the Apple and XDG rules are
  both tested on any machine; `findUserDataDirectory` passes the platform's.

**The Desktop, and what uses it.** The replay menu's **Copy** button (`ReplayMenu.cpp`,
`copyReplay`) puts a copy of the selected replay on the player's Desktop, under its listed name.
On Windows that folder comes from the shell, as before. Off Windows, `findDesktopDirectory`:
- macOS: `~/Desktop`;
- Linux: the `XDG_DESKTOP_DIR` that xdg-user-dirs writes in `$XDG_CONFIG_HOME/user-dirs.dirs`
  (default `~/.config`), as `"$HOME/..."` or an absolute path; the last assignment wins; anything
  else is ignored. The fallback is `~/Desktop`.
- It does not create the Desktop. If the folder is missing, the copy fails and the player sees the
  system's reason, as with any failed copy. With no home directory at all, the button shows
  `strerror(ENOENT)` and copies nothing.

**Tests.** `test_userfolders`: 4 tests, 35 checks. They cover:
- both conventions' paths;
- the override (including a trailing `'\'`);
- an XDG data home that is relative, and so ignored;
- no home;
- the process's own answer: made with parents, ending in `'\'`, fixed after the first call, and
  refused rather than cut short in a small buffer;
- the user-dirs file (the `$HOME` form, the absolute form, a relative value refused, a longer key
  not matched, last wins, empty);
- the Desktop under both conventions, with and without a user-dirs file and with `XDG_CONFIG_HOME`
  moved.

It writes only under `$TMPDIR`: checked, no folder appeared under the real Application Support.
`test_gameengine`, when it runs off Windows, gets `ZH_USER_DATA_DIR` in the build tree, for the same
reason.

**Result.** `gameengine` compiles on macOS but for -18's three Winsock files (`Transport`,
`IPEnumeration`, `udp`). `windows_view_diff`: all three changed C/C++ files are identical to MSVC. The
CMake additions are POSIX-only.

**Left for (d), and why it matters now.** Two readers open files in this directory with a raw
`fopen`, and on POSIX a raw `fopen` takes the `'\'` before the leaf as part of a file name:
- `EarlyOptions.h`'s `findEarlyOptionValue` (`Options.ini`);
- `registry.cpp`'s `Registry.ini`.

So until (d) moves them to `zh_fopen`, they find nothing and fall back to their defaults. The
engine's own writes go through the file system and resolve correctly.

## PR (d): raw file calls through the resolver (2026-09-26)

**The early readers, first.** `EarlyOptions`' `Options.ini` reader and `registry.cpp`'s
`Registry.ini` reader (-18's file, changed with their agreement) open through `zh_fopen`.
- Before, they read nothing off Windows: the user data directory ends in `'\'`, and a raw POSIX
  `fopen` takes that as part of a file name.
- `findRegistryFile` is the one path for every reader and writer. The protocol is in the next
  section.
- `test_posixlocalfilesystem` builds the real `registry.cpp` and reads a `Registry.ini` in a
  `ZH_USER_DATA_DIR` of its own. With the raw `fopen` put back, 7 of its checks fail.
- `test_userfolders` reads an `Options.ini` the same way.

**`zh_stat`** joins the forwarders: on Windows it is `stat`, the CRT's POSIX name, as the one engine
call site already spelled it; on POSIX it resolves, then stats.

**The sites: 78 calls in 30 files.** Every raw `fopen`, `remove`, `rename`, `access`, `_open`, `stat`
and `unlink` whose path the engine spelled, in code the macOS build compiles, now uses its `zh_`
form. On Windows each is the same call, inline. They cover:
- the save, load and map writers;
- the Xfer files;
- Recorder's stats and debug copies;
- `UserPreferences`;
- the model-checksum cache;
- `StatsCollector`, `MiniLog`, the perf and CRC dumps, `ThingFactory`'s exports, `DataChunk`'s
  temp file, `Debug.cpp`'s log rotation, and the GameSpy/LAN files;
- WWLib's `mixfile`, `argv`, and `Wwutil`'s `miscutil` (its `Remove_File` POSIX arm is `zh_unlink`, as `DeleteFile` removes files only);
- **`RawFileClass`'s POSIX arms** (its four `open`s and its `unlink`). Set_Name's inert `_UNIX` arm,
  which lowercased names and rewrote backslashes, is removed: it is what D1 rules out.

**The joins after the executable's directory** (`MiniLog`, `MemoryInit`'s
`"\Data\INI\MemoryPools.ini"`, `Debug.cpp`'s log names) need no change of their own. They reach the
disk through `zh_fopen` now, and the resolver takes both separators.

**Left raw, on purpose:**
- real POSIX paths (`EarlyOptions`' `mkdir`/`stat` of the directory it builds, `user-dirs.dirs`,
  `srandom`'s `/dev/random`, `PosixLocalFileSystem`, which resolves itself);
- code the macOS build does not compile (W3D2's `missingtexture`, `FramGrab`, `ffprobe`, the D3D11
  backend; every `CreateFile`);
- `LocalFile`'s `USE_BUFFERED_IO` branch, which is compiled out.

**Tests.**
- `test_posixpath`, 168 checks: `zh_stat`, and RawFileClass reading, writing and deleting
  engine-spelled paths on both volumes. With RawFileClass's read `open` made raw again, its checks
  fail on both, because the backslashes alone defeat a raw `open`.
- `test_posixlocalfilesystem`, 161 checks.
- ctest 35 of 35 on macOS: `gametext_csf` and `d3dx_oracle` skipped, `test_gameengine` disabled.
- The five tests that compile engine files from source (the widechar gates, `widechar_file_selfcheck`,
  `gametext_csf`) link wwlib off Windows now, for `zh_fopen`.

**Windows.** `windows_view_diff.py`: 29 of the 33 files differ only by the `zh_` prefix and the
include. The other four are `zhio.h`'s `zh_stat` and its `<sys/types.h>`/`<sys/stat.h>`,
RawFileClass's removed `_UNIX` arm, and two POSIX-only files. WINDOWS-DEBT has the rows.

## PR (f): the BIG reader, PosixGameEngine and the asset test (2026-09-26)

**One BIG reader, not two.** `Win32BIGFile.cpp` and `Win32BIGFileSystem.cpp` are built into
`posixdevice` as they are.
- `Win32BIGFile` has no Windows call.
- `Win32BIGFileSystem` had three, now behind `#if`: `<winsock2.h>`/`<windows.h>` for `ntohl`
  (`<arpa/inet.h>` off Windows), and the missing-base-game `MessageBox` (`MessageBoxWrapper`
  off Windows).
- A copy was the alternative, and it would have two versions of the mount order: which archive wins
  which path (the patch pass, the base-game search, `prioritizeLargerFiles`), which is what the
  INI CRC and every asset hang off. -47 agreed. The classes keep their Windows names.
- Two includes were in the wrong case (`Common/File.h`, `Common/registry.h`), which Linux would
  refuse. They are fixed; Windows sees only an include-case change.

**The BIGF rule (requirement above).** A file the listing finds whose first four bytes are not
`BIGF` is left out, with one `DEBUG_LOG` line, off Windows. It is neither mounted nor fatal. Windows
keeps its `DEBUG_CRASH`, which fires in debug builds only.

**PosixGameEngine**, `GameEngineDevice/{Include,Source}/PosixDevice/Common/`, shared by macOS and
Linux, and **abstract on purpose.**
- **It answers what is the platform's:**
  - `PosixLocalFileSystem`;
  - the BIG reader;
  - `NetworkInterface::createNetwork()`;
  - no web browser (nothing creates one on Windows either).
- **Pure virtual:** the factories Windows answers with W3DDevice classes (game logic, client,
  module factory, thing factory, lexicon, particles, radar) and the audio manager.
  - No W3D class links on macOS yet (-47).
  - `createGameLogic` must not fall back to the base `GameLogic`: `W3DTerrainLogic` takes ground
    height from the renderer's height map (-47's finding, now task T1).
  - A headless `createModuleFactory` needs `W3DModuleFactory`'s 19 draw-module names, registered in
    the same order right after `ModuleFactory::init` (their NameKeys), with parsers that accept each
    one's INI fields. -47 sent the list; it is not in (f).
  - An abstract class can't return the wrong one.
- **The headless module factory's requirement** (from -47, for whoever builds it; proposed as its
  own piece after T1):
  - Register these 19 draw modules straight after `ModuleFactory::init()`, in this order, which
    fixes their NameKeys as Windows has them: W3DDefaultDraw, W3DDebrisDraw, W3DModelDraw,
    W3DLaserDraw, W3DOverlordTankDraw, W3DOverlordTruckDraw, W3DOverlordAircraftDraw,
    W3DProjectileStreamDraw, W3DPoliceCarDraw, W3DRopeDraw, W3DScienceModelDraw, W3DSupplyDraw,
    W3DDependencyModelDraw, W3DTankDraw, W3DTruckDraw, W3DTracerDraw, W3DTankTruckDraw,
    W3DTreeDraw, W3DPropDraw.
  - Each one's ModuleData must accept that module's INI fields (W3DModelDraw's table and the ones
    that extend it), or object INI loading fails on the first unknown field. Ignoring them is safe
    only if no draw ModuleData feeds logic; -47 knows of none, but W3DModelDraw's parse table should
    be checked for anything GameLogic reads.
- **Agreed with -18 for C2:**
  - `serviceWindowsOS` is an empty virtual, and C2's SDL subclass overrides it (event pump, focus).
  - The factories stay virtual and non-final.
  - `CreateGameEngine` is C2's, in its `main`.
  - C2's `main` makes the install root the current directory once, under rule 9.
- `Win32GameEngine::update`'s minimized-window idle and audio wake-up are window work, left to that
  subclass.
- `PosixCDManager` is -47's, from B6.
- Compiled here; no test constructs it, since `GameEngine`'s constructor needs the whole engine
  linked.

**The asset test: `test_bigfilesystem`, under rule 9.**
- **The farm.** It builds a farm of symbolic links to the install's archives under a temporary
  folder: Zero Hour's `*.big`, the AppleDouble `._*.big` too, `Data\INI`'s stray `INIZH.big`, and
  Generals' as `ZH_Generals\`. It makes the farm read-only and mounts that with the real
  `Win32BIGFileSystem::init()`. The install is only ever read, and `INIZH.big` was checked still in
  place after runs.
- **An independent reading of the archives.** The test's own parser, and the mount rules written
  from their statement, not the code:
  - the root's archives in name order, first claim kept;
  - `Patch*.big` again, each overwriting;
  - the base game filling what is left;
  - in `Art\Textures\`, the larger of `TexturesZH.big`'s and `Textures.big`'s copies.
- **Against every path**, it compares the owning archive the game reports, and the bytes the game
  reads.
- **Results, on both `$TMPDIR` and a case-sensitive APFS image:**
  - 38 archives, and 38 AppleDouble files refused quietly;
  - 25,294 paths, 481 textures moved to the larger copy;
  - every owner as the rules say;
  - **all 25,293 openable files byte-identical**. The one left, `data\*` in `PatchZH.big`, has no
    `'.'` in its last component, so the directory walk (defect 12's) never files it, on Windows as
    here;
  - no missing-base-game message: `ZH_Generals` is found;
  - hash of every path and its bytes: `c8140abc27b4d05d`. A Windows run of the same test should
    print the same. That is the "byte-identical to the Windows build" comparison, left for a
    Windows machine.
  - 7 seconds.
- **With the `Patch*.big` pass removed**, owners and bytes both fail (`WindowTransitions.ini` from
  `INIZH.big` instead of `PatchINI.big`).
- It needs `ZH_GAME_DATA` at configure time, and exits 77 (Skipped) without it.

**C1's "Done when":**
- Mounting the `.big` files and reading assets byte-identically is shown on both volumes, and checked
  into `Tests/`.
- "`MacGameEngine` constructs" became `PosixGameEngine`, abstract, which constructs once C2's
  subclass supplies the renderer's factories and T1 the terrain.
- The Windows-build side of "byte-identical" is the hash above, for a Windows run to reproduce.

## Registry.ini: the protocol (agreed 2026-09-26 by -a9 (C1), -18 (registry.cpp) and -47 (B6))

Off Windows, `Registry.ini` in the user data directory stands in for the registry.
- **Path:** `findRegistryFile` (`EarlyOptions.h`), which is `findUserDataDirectory()` +
  `"Registry.ini"`. Every reader and writer takes it from there.
- **Opened with** `zh_fopen`: the path is spelled the Windows way, so a raw `fopen` finds nothing
  (fixed in C1 (d) for `registry.cpp` and `Options.ini`).
- **Format:** `key = value`, one per line, LF.
- **Reading:** `EarlyOptions`' `findEarlyOptionValueIn`. Keys are matched case-insensitively and
  whitespace-trimmed, and the last one wins. A value is at most 255 bytes.
- **Keys:** `registry.cpp`'s `registryFileKey`. That is the path below Zero Hour's key without
  leading backslashes, then `'\'`, then the value name. Original-Generals values have `"Generals\"`
  in front. DWORDs are decimal text.
- **Writing** (-47's POSIX bodies of the WWDownload registry calls, in gameengine):
  - Read the whole file.
  - Replace the **last** line whose key matches by the reader's rules, keeping every other line
    and comment, or append one.
  - Refuse a value containing a newline or longer than 255 bytes, which the reader would truncate.
  - Write it all to `<path>.<pid>` with `zh_fopen("w")`, then `zh_rename` it over `<path>`, so a
    reader sees the old file or the new one, never half.
  - No locking; the last writer wins.
- **Shared code:** -18 exposes `readRegistryFileAt` and `registryFileKey` in `Common/RegistryFile.h`
  on top of C1 (d), so the writers' getters read back through the same parser.

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
| `Win32GameEngine.cpp` | the factory and the service loop | `PosixGameEngine` (abstract, C1 (f)); C2's SDL subclass |
| `Win32LocalFileSystem.cpp` | real files | POSIX |
| `Win32LocalFile.cpp` | one open file | POSIX |
| `Win32BIGFileSystem.cpp` | mounts `.big` archives | the same file, built off Windows (C1 (f)) |
| `Win32BIGFile.cpp` | one file inside an archive | the same file, built off Windows (C1 (f)) |
| `Win32CDManager.cpp` | the disc check | `PosixCDManager`, -47's (B6): no drives |
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

**Done in PR (g), 2026-09-26.**

- **What macOS actually did (measured).** It does not fail the mixed calls, as the paragraph above
  expected. After `fwrite`, the stream is byte-oriented (`fwide` < 0), yet `fputwc` and `fgetwc`
  still run. They go through the locale's multibyte encoding: `'A'` becomes one byte, `U+00FC`
  becomes `fc` in the C locale and `c3 bc` in a UTF-8 one. So a Mac-written replay would have read
  back on the Mac and been unreadable everywhere else, which is worse than failing. glibc was not
  measured.
- **The format**, from MSVC's documented binary-mode behaviour: in a stream opened `"wb"`/`"rb"`,
  the wide calls convert nothing, so each UTF-16 code unit is two bytes, low first, and a string ends
  with a 0 unit.
- **The change.** Three functions in `Lib/WideCharFns.h`, the funnel B1 left for this, all using
  `fputc`/`fgetc`:
  - `WideCharFileWrite` (was `fwprintf(L"%ls")`);
  - `WideCharFilePut` (for `fputwc`);
  - `WideCharFileGet` (for `fgetwc`). It returns 0xFFFF, MSVC's `WEOF`, at the end of the file,
    including with one byte left.

  `Recorder.cpp`'s five live wide calls use them. The two in the `/* */` player block are left as
  they are. Windows now makes byte calls too, and writes the same bytes.
- **Tests.**
  - `widechar_file_selfcheck`: 4 tests, 25 checks. It covers exact bytes for units a conversion or
    text mode would change (U+00DC, CJK, a surrogate pair, CR, LF, Ctrl-Z), and the header's shape
    interleaved with `fprintf`/`fwrite`/`fread`/`fgetc` on one stream, read back.
  - A control shows that on this libc a wide call after `fwrite` does not write the two-byte form.
- **Wine oracle** (`Tests/replay_wide_oracle.cpp`, mingw-w64 with the CRT's own `fwprintf`). It does
  exactly what Recorder did, run in bottle `zh-e4`:
  - It wrote the test's expected bytes, both cases, byte for byte.
  - Read back as the old reader did, it gave the same units, `WEOF` = 0xFFFF at the end, and 0xFFFF
    with one byte left.
  - As before, the msvcrt there is Wine's builtin, not Microsoft's.
- **Not verified:** no replay recorded by the Windows game exists on this machine, so no real
  `.rep` was compared. Recorder.cpp's object still fails on macOS only at `CopyFile` (`:768`, PR (c)).
  So the linked end-to-end replay test waits for (c) and for `gameengine` to link.
- **Found:** defect 15 in the README (a truncated header reads as U+FFFF names). Kept identical.

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

## What B5 left for C1 to replace, 2026-09-26

B5 made these compile off Windows with a stand-in. Each stand-in is a placeholder, not a design,
and C1 replaces it:

- **The user data directory.** `EarlyOptions.h`'s `findUserDataDirectory` returns false off
  Windows. GlobalData then logs "Could not find the Documents folder" and leaves `m_userDataDir`
  empty, so saves, replays, Options.ini and the crash log go to the **current directory**. That
  is GlobalData's own fallback, not a choice. Where they belong on macOS and Linux is C1's call,
  and until it is made no early option (window mode, monitor, MSAA) can be read either.
- **The file operations**: B5's task file, "File operations are C1's", lists every site in 11
  files.
- **`LocalFile.cpp`**: `_open`/`_read`/`_write`/`_lseek`/`_close` and `_O_BINARY` from `<io.h>`.
  It is the engine's local file system and the core of this task, so it was left failing rather
  than renamed. (`RAMFile.cpp` and `StreamingArchiveFile.cpp` included `<io.h>` for nothing; B5
  put those includes under `_WIN32`.)
- **The `'\\'` joins after `getExecutableDirectory()`**: MiniLog, `MemoryInit.cpp`'s
  `Data\INI\MemoryPools.ini` (until then every pool keeps its compiled-in size) and Debug.cpp's
  log names.

- **Registry.ini.** Off Windows, `registry.cpp` reads `Registry.ini` from the user data directory in
  Options.ini's "key = value" form: `Language`, `Version`, `ergc\<name>`, `Generals\<name>`.
  Nothing in the engine writes it; the future launcher or installer does, as the Windows installer
  writes the registry. Until C1 places the user data directory, none is read and every value keeps its
  compiled-in default. That default language is "english", so a non-English Mac install needs the file
  until the launcher detects the language from the installed `Data\<lang>\` folders (C2's or the
  launcher's call).

## Do not

- Do not change `GameEngine`'s factory interface. If a factory does not fit macOS, that is worth
  discussing on this task before it becomes a change every platform pays for.
- Do not implement input or audio here. Null implementations, and C3/C4 own the real ones.
