# P1 — Packaging the macOS app

- **Milestone:** M5
- **Depends on:** C1 (file systems), C2 (entry point), V1 (FFmpeg's licence); E1 for the convergence step
- **Blocks:** anything a player runs; E2's notarisation question
- **Status:** done (-47, 2026-09-26): steps 1-5 merged or on their branch; step 5 below. The by-hand check of the
  root chooser is the user's (step 5, "By hand").
- **Owner:** -47

## Why

Decision 9 (README) says the fork's own data is an overlay that ships beside the executable and that the
local file system searches BEFORE the install root, never written into the player's install. Nothing
implements it yet: E1's harness fakes it with a symlink farm, and the POSIX executable roots itself at
`-root` or its own directory. A player needs an `.app` that finds their Zero Hour install, never writes
to it, and resolves every file exactly as Windows' `Run/` does.

## What Windows does (the behaviour to reproduce)

Read from this tree. **The shipped set is inferred and must be confirmed:** the Windows packaging
scripts `package.bat` and `launcher/build-payload.js` are referenced in comments but are not in this
repository, so the list below is taken from the post-build list in `CMakeLists.txt` and from
`vendor.sh`'s art step. It is an open item to check against upstream's release tooling before the
first macOS release, and the bundle target's comment says the same.

- **One folder.** `Run/` is the game folder: the player's Steam or CD Zero Hour files, plus what
  `generals`' post-build copies over them from `Code/Data`: `Data/INI/*` (the fork's INIs, in the
  multiplayer CRC), `Data/Patch.str`, `Data/Scripts/SkirmishScripts.scb` (in the CRC),
  `Data/Turkish/`, `Install_Final.bmp` (the splash), `Art/Textures/*.tga`, `Window/` (layouts and the
  HTML UI). `Scenarios/` and `Cinema/` are copied too but, per the CMake comments, are not in
  `package.bat`: they are measuring and footage tools, never loaded unless `-scenario`/`-cinema`.
  Also in `Run/`: the fork's art archives `ReforgedNormals.big` (350 MB), `ReforgedTerrain.big`
  (228 MB) and `ReforgedTextures.big` (1,069 MB) from the release channel, 1.65 GB in all, and
  `BrowserEngine.dll` (Windows-only).
- **Loose beats archive.** `FileSystem::openFile` asks `TheLocalFileSystem` first and the archives only
  when it answers NULL, so any loose file under the root wins over every `.big`.
- **Archive against archive** (`Win32BIGFileSystem::init`, which POSIX compiles too):
  1. `*.big` in the root and no deeper, loaded in the order of a `FilenameList`, a
     `std::set<AsciiString, less_than_nocase>`: case-insensitive alphabetical by path, whatever the
     directory enumeration order was. `loadIntoDirectoryTree` keeps the FIRST archive to claim a path.
     So `ReforgedTextures.big` beats `TexturesZH.big`, `W3DZH.big` and `WindowZH.big` (R < T, W) and
     loses to `INIZH.big`, `EnglishZH.big` and the rest before R.
  2. `Patch*.big` again, with overwrite.
  3. The base game's archives: from the root if it holds `Textures.big`, else the registered
     `InstallPath` (and its First Decade subfolder), else `ZH_Generals\`, `..\Command & Conquer
     Generals\`, `..\Command & Conquer(tm) Generals\`. No overwrite, so Zero Hour's copies win.
  4. `prioritizeLargerFiles` between `TexturesZH.big` and `Textures.big`.
  - `ClassicGraphics` (Options.ini, read before mounting) leaves every `Reforged*` archive out.
- **Writes.** `GameEngine::init` deletes `Data\INI\INIZH.big` from the root on every start (patch
  1.01's stray-file fix). Logs go beside the executable (`MiniLog.cpp`, `MemoryInit.cpp` through
  `getExecutableDirectory`). Everything else the player owns goes to the user data directory.

## Design

### 1. The overlay: a second read root, searched first (decision 9)

`PosixLocalFileSystem` gets an ordered list of **read roots**, `[overlay..., install]`, in place of the
one working directory. The install stays the working directory, `chdir`'d at start-up as now, so
every relative path the engine builds still means what it meant.

- **Open, exists, size, stat** of a relative path: tried in each root in order, through
  `PosixPath_Resolve` per root, and the first hit wins. An overlay file therefore beats the install's
  loose file of the same name and every archive, which is Windows' copied-over result.
- **Directory listing** (`getFileListInDirectory`): the union of the roots' listings, by relative
  name. `FilenameList` is a set, so a name in both roots appears once, and opening it takes the
  overlay's copy. Because the set orders by name, not by root, the `*.big` pass sees
  `Reforged*.big` (in the overlay) interleaved with the retail archives (in the install) in exactly
  the order it sees them in one Windows folder. **The archive order is then byte-for-byte Windows',
  by construction, not by imitation:** it is the same code, over the same set of names.
- The INI loader's `Data\INI\*.ini` listing is the same union, so `FXListReforged.ini` and the other
  masters join the INI set, and the multiplayer INI CRC, exactly as in `Run/`.
- An **absolute** path (the user data directory, a `-mod` folder) is untouched: roots apply only to
  relative paths.
- The base game is found as today (`holdsBaseGameArchives("")` sees `Textures.big` through the union;
  the registry and `ZH_Generals` fallbacks are unchanged).

**Where the overlay is.** Found once at start-up, first match wins:
1. `-overlay <dir>`, repeatable, in the order given (development and the harnesses);
2. `<exe>/../Resources/Overlay` when the executable is inside an app bundle (macOS);
3. `<exe>/../share/zero-hour-reforged/overlay` (Linux packages; the constraint, not built now).
A build with no overlay found runs as today, install only. (Step 1 dropped a bare `<exe>/overlay`
from the design: once `zh_overlay` stages `<build>/overlay`, every development run from the build
folder would have picked it up silently, and E1 would have changed what it runs without anyone
asking.)

**Read-only roots.** Every root is read-only. `deleteFile`, `createDirectory`, and an open with
`WRITE`/`CREATE` on a *relative* path are refused, logged once per path, and answer as a missing
file does. The `INIZH.big` deletion becomes a logged no-op, and it loses nothing: the stray it
removed is two folders down, and pass 1 mounts the root's top level only. Before this ships,
implementation step 2 audits the other writers: a grep finds 31 lines with `deleteFile`, `WRITE`,
`CREATE` or `createDirectory` in `GameEngine/Source` that do not name the user-data path. Each is
classified, so none of them depended on writing into the root.
With read-only roots, **the packaged game cannot modify the player's install**, which is rule 9
enforced by the file system rather than by how the game is run.

**Logs** move from beside the executable to `<user data>/Logs/` whenever the executable's directory
is inside a bundle. Writing into the `.app` would also break its code signature. E1's harness reads
them from there, and a flag, as now, keeps them next to the executable for the unpacked build.

### 2. Where the game data comes from

The player's own copy: a Steam install, CD, First Decade, or copied from a PC; anything that is a
Zero Hour folder. macOS Steam does not install this game natively, so no path can be assumed.

**The root, in order:**
1. `-root <dir>` (development, harnesses; unchanged);
2. Registry.ini's Zero Hour `InstallPath` (`registryFileKey("", "", "InstallPath")`, C1's protocol);
3. only when running from a bundle: a search of known places, each validated. These are `~/Games`,
   `/Applications` and the CrossOver and Whisky bottles' `drive_c/Program Files*/…/Command &
   Conquer Generals Zero Hour` and its Steam `steamapps/common` spelling;
4. otherwise the **first-run chooser**: SDL3's `SDL_ShowOpenFolderDialog` (native `NSOpenPanel`,
   present in the vendored SDL), titled "Choose your Command & Conquer Generals Zero Hour folder",
   before `GameEngine` exists;
5. outside a bundle with none of these: the executable's directory, as today.

**Validation** (the same function for every source):
- **Zero Hour:** `INIZH.big` at the top level of the chosen folder (the file
  `GameEngine::init` already requires, through `Data\INI\Default\GameData.ini`).
- **The base game:** `Textures.big` found by the existing search (root, `ZH_Generals/`, siblings, or
  Registry.ini's Generals `InstallPath`). If it is missing, a second chooser asks for the Generals
  folder, and its answer is written as the Generals `InstallPath`, the key
  `Win32BIGFileSystem::init` already reads.
- **Refusals:** a folder inside the `.app` or the overlay; a folder without `INIZH.big`, re-asked
  with the reason.

A validated choice is written to Registry.ini, so the chooser runs once. The Options menu gets no new
UI in P1. "Choose again" is `-root` or deleting the key; a menu entry is a follow-up.

### 3. The `.app`

```
Zero Hour Reforged.app/Contents/
  Info.plist        CFBundleExecutable generals; CFBundleIdentifier (to decide, e.g.
                    io.github.<owner>.zero-hour-reforged); CFBundleShortVersionString from version.h;
                    LSMinimumSystemVersion = the deployment target; NSHighResolutionCapable; the
                    icon; LSApplicationCategoryType public.app-category.strategy-games
  PkgInfo
  MacOS/generals    stripped; its dSYM kept beside the bundle, not in it, for C5's crash symbols
  Resources/
    AppIcon.icns
    Overlay/        the shipped overlay, exactly the staged one (step 4)
    Licenses/       see below
```

- **Overlay contents:** `Data/INI/`, `Data/Patch.str`, `Data/Scripts/`, `Data/Turkish/`,
  `Install_Final.bmp`, `Art/Textures/`, `Window/`, and the three `Reforged*.big` (1.65 GB). There is
  no `Scenarios/`, no `Cinema/` (as `package.bat`) and no `BrowserEngine.dll`. The splash is read
  through the local file system, so it comes from the overlay.
- **The art archives go inside the bundle** for the first release: one self-contained download,
  nothing fetched at run time, and nothing for notarisation to question. A build option
  `ZH_APP_ART=OFF` leaves them out for local bundles; the game then looks as `ClassicGraphics` does.
- **The icon.** `Main/Generals.ico`'s largest image is 48 px, too small for a Retina Dock (1024 px).
  An upscale will look soft, so a 1024 px master is an open item for whoever owns the fork's
  artwork; until then `iconutil` builds the `.icns` from the 48 px image.
- **Licences** (`Resources/Licenses/`), copied at bundle time from each vendored source, so they
  cannot go stale:
  - the game: `LICENSE.md` (GPL-3 and EA's additional terms);
  - FFmpeg: `<build>/ffmpeg/LICENSE.txt` (LGPL 2.1+), with V1's source pointer (the release URL, the
    configure line in `Tools/ffmpeg-build-posix.sh`, and this repository for relinking);
  - SDL3 and SDL_shadercross (zlib);
  - FreeType (FTL chosen over GPL-2, which needs the credit line in the About text too);
  - miniaudio (public domain / MIT-0);
  - litehtml (BSD-3) and gumbo (Apache-2.0);
  - glslang (its LICENSE.txt, a mix of BSD-3/MIT/Apache-2.0) and SPIRV-Cross (Apache-2.0);
  - GameSpy (its LICENSE); nanosvg and zlib (zlib).
  The list is the one the game links today (`generals`' link line, 2026-09-26). Implementation
  regenerates it from the link line and fails the bundle target on any linked vendored library
  without a licence entry, so a new dependency cannot ship unlicensed.
- **Signing.** An ad-hoc signature, `codesign --force --sign - --timestamp=none` over the finished
  bundle, is the last bundle step. arm64 refuses unsigned code, and the resources' seal must match
  the final contents. Anything written into the bundle afterwards breaks the seal, which is one more
  reason for read-only roots and moved logs. A quarantined download of an ad-hoc app needs
  right-click Open once. **Notarisation, a Developer ID and the hardened runtime stay E2's open
  question.**
- **Built by** a POSIX, Apple-only CMake target `macos_app`, outside `ALL`: `configure_file` for
  Info.plist, copies from the staged overlay, the licences, `iconutil`, then `codesign`. Size with
  the art: ~1.7 GB (the executable is 31 MB unstripped). **Not built until the PM lifts the disk
  hold.**

### 4. The harnesses and the package converge

- **One staged overlay.** A target `zh_overlay` writes `<build>/overlay/` from `Code/Data` (the list
  above) and the art archives; `macos_app` copies that directory verbatim. `Scenarios/` and
  `Cinema/` stage to `<build>/overlay-dev/`, a second overlay only the development tools pass.
- **E1 runs the shipped resolution.** `replay-check.sh` stops copying `Code/Data` into its farm, and
  runs `generals -root <farm> -overlay <build>/overlay`: the same code path the bundle takes, over
  the same files. While read-only roots are unproven the farm stays (rule 9); once test (b) below
  passes, the harness may root at the install itself.
- **Proof that it matches Windows: `test_packaging_resolution`** (needs `ZH_GAME_DATA`, headless,
  no match). It builds two layouts from the same install:
  - W, the Windows shape: one folder, the install farm with the overlay copied in, as the farm is
    today;
  - P, the package shape: the install as root plus `-overlay`.
  It mounts both and dumps, for every path in the directory tree and every loose file the INI loader
  lists, which root or archive it resolves to and the bytes' hash. The two dumps must be identical,
  and so must the INI CRC. The armed control: renaming `ReforgedTextures.big` to
  `ZReforgedTextures.big` in P only must make the dumps differ, proving the test sees archive order.
- **Proof that it cannot write the install: test (b)** runs `generals -headless -quickstart` for a
  short match with P, and compares the install's recursive listing (names, sizes, mtimes) before and
  after (the rule-9 listing diff). The listing diff alone cannot tell "refused" from "nothing
  tried", so the run must also log the refusal of `GameEngine::init`'s `Data\INI\INIZH.big`
  deletion, the one write every start attempts. The armed control: on a farm copy (never the
  install), the check switched off must make the listing diff show the attempt (a planted
  `Data/INI/INIZH.big` in the farm disappears).
- **E1 tests what ships** once `replay-check.sh --app <Zero Hour Reforged.app>` exists. It runs
  `Contents/MacOS/generals` with no `-overlay`, so the bundle's own discovery is exercised, which is
  the final check before a release. That needs a bundle, so it waits for the disk.

## Order (each a reviewed branch)

1. Read roots and `-overlay` in `PosixLocalFileSystem`, with the union listing and tests at the file
   system level (unit: two temporary roots, one name in each, one in both; listing order; open
   order).
2. Read-only roots, the audit of the 31 grep hits, and logs out of the bundle directory.
3. `zh_overlay`, then `test_packaging_resolution` (W against P) and test (b); E1's harness switched to
   `-overlay`.
4. Root selection: Registry.ini, the known places, the chooser, validation. This is the only step with
   UI; tested headless through its validation function and by hand once.
5. `macos_app`: Info.plist, icon, licences with the link-line check, ad-hoc signing. It needs the
   disk.

## Windows

Nothing here changes what Windows compiles. `PosixLocalFileSystem`, `PosixMain` and the new targets are
POSIX-only; `Win32BIGFileSystem.cpp` is shared but untouched, since the union happens below it, in the
local file system. Each step still runs `windows_view_diff.py` and adds a WINDOWS-DEBT row if a shared
file moves.

## What this design cannot see

- A player's real install layouts beyond the one on this machine (`/Volumes/External/Games/cnc`,
  with `zerohour/ZH_Generals`). The known-places list is a guess until players report theirs.
- Gatekeeper's behaviour for a downloaded ad-hoc app on the current macOS; that is E2's.
- Windows' actual `package.bat` contents, which are not in this repository. The shipped overlay list
  is inferred from the CMake comments, and whoever holds that script should confirm it.
- Linux packaging, beyond the discovery rule's third entry.

## Step 1: the overlay as a read root (-47, 2026-09-26)

**Where it went.** The design put the roots in `PosixLocalFileSystem`. They went one level lower, into
WWLib's `posixpath` (`PosixPath_Resolve` and `PosixPath_List_Like_Win32`), because every relative
open off Windows goes through there, not only the engine's file system: the `zh_*` calls, the Bink
movies, the fonts, the splash. On Windows all of those see `Run/`'s copied files, so all of them now
see the overlay. The local file system needed no change.
- **Reads** (`POSIX_PATH_EXISTING`) of a relative path try each overlay, then the working directory.
- **Writes** resolve in the working directory only: every CREATE intent, and a new
  `POSIX_PATH_EXISTING_IN_ROOT` for whatever changes an existing file. The `zh_*` forwarders map
  `unlink`, `remove`, `rename`'s source, and opens for writing (`r+`, `O_WRONLY`, `O_RDWR`,
  `O_TRUNC`, `O_APPEND`) to it. A write can therefore never land in, or delete from, an overlay.
- **Listings** merge the roots by name (case-insensitively across roots; within one root every entry
  stays, as a case-sensitive volume can hold two), then byte order.
- `-overlay <dir>` (repeatable) or the bundle's `Resources/Overlay` is chosen in `PosixMain` before
  the `chdir` to the root, and reported on stderr.

**Checks.**
- `test_posixpath` gains `overlay_reads_first_writes_never_lists_as_one_folder`, worked by hand:
  - reads see the overlay's copy first (including an overlay spelled in another case);
  - the listings are one folder's, with and without subdirectories, one entry per name;
  - a create, an update, an unlink and a write-open all touch the install only, never the overlay;
  - absolute paths are untouched;
  - armed control: with no overlay set, nothing of it is seen.
  The case-sensitive APFS run still passes (it caught a first version that folded names within one
  root).
- `overlay_crc_check` (`Tools/overlay-crc-check.sh`, ctest, needs `ZH_GAME_DATA`): the PM's
  multiplayer check.
  - Layout W is one folder: the install farm with the shipped overlay copied in and the fork's
    `Reforged*.big` linked. Layout P is the install farm as the root, plus the same files as
    `-overlay`.
  - A 600-frame skirmish in each: the EXE CRC (decision 5's inputs off Windows: the version and the two
    script files), the INI CRC and the world CRC agree: `0x0D56B480`, `0x1E635A82`, `0xE21F0AC9`
    at frame 600.
  - Armed controls: without `-overlay` the EXE CRC differs (`0xFE3E96F1`), and that run stops before
    its INI CRC, as decision 9's first run did. With one value changed in the overlay
    (`BalanceReforged.ini` BuildCost 1000 to 1001) the run goes through and only the INI CRC moves
    (`0x86F44535`).
- Full ctest: every test passes except `ffref_gpu_selfcheck`, which is -a9's harness asking for its
  two F11 scenarios to come off its KNOWN list now that they pass: its designed signal, not a
  regression here.

**What it cannot see.** Windows' own `Run/` (W is the farm standing in for it, as E1's is); a write
that bypasses `zh_*` and `posixpath` (raw `fopen` of a relative path), which step 2's audit looks for;
Linux (the disk hold).

## Step 2: read-only roots, the writer audit, logs out of the bundle (-47, 2026-09-26)

**Read-only roots.** `PosixPath_Set_Root_Read_Only`, set by `PosixMain` for every run:
- every `zh_*` call that would write a RELATIVE path is refused with `EROFS` and reported once per
  path on stderr (and to WWDebug). That covers create, truncate, append, update (`r+`), remove,
  unlink, rename (either end) and mkdir;
- absolute paths (the user data directory, logs, a `-mod` folder) are the engine's deliberate
  destinations and pass;
- `-writableRoot` turns it off, for a harness's armed control only.
The refusal sits in the `zh_*` wrappers, not in `PosixPath_Resolve`, because the listing code
resolves directories with the root-only intent just to read them.

**The audit.** The design's 31 grep hits are classified, and a second sweep covers writes that bypass
`zh_*` (raw `fopen`, `open`, `rename`, `unlink`, `mkdir`, `ofstream`) in GameEngine,
GameEngineDevice, Main and WWVegas:
- **the user data directory** (absolute, unaffected): saves, replays and their archive, map previews,
  the GameSpy folders, the model-CRC cache, screenshots, Options.ini/Registry.ini, the crash
  reports;
- **relative, now refused:** `GameEngine::init`'s `Data\INI\INIZH.big` delete, a logged no-op;
  `MainMenuUtils`' `PatchAccessTest.txt` probe, whose POSIX branch called raw `open()`, now
  `zh_open`, so a read-only install answers "no write access", which is true;
- **found by the proof run, missed by the grep:** `ffprobe.txt`. The fixed-function probe is always
  on (W3DDisplay), and `DX8Wrapper::Shutdown` dumps it with a raw `fopen` into the working
  directory at every exit: `Run/` on Windows, the player's install here. Its POSIX branch is now
  `zh_fopen`, so it is refused;
- **relative, debug and dev tools only, not in a release build:** `-playStats`' `Stats\`, the
  `.\` stats base, `-updateTGAToDDS`'s `buildDDS.txt`, the particle editor's
  `Data\INI\ParticleSystem*.ini`, `PreloadedAssets.txt`, `FrameRateLog.txt` and the other
  `_DEBUG`/`_INTERNAL`/`DUMP_PERF_STATS` dumps. The water-track editor is Windows-only. If one is
  ever built, the read-only root refuses it (through `zh_*`) or the raw `fopen` needs the same
  one-line branch.

**Logs out of the bundle.** `getLogDirectory` (POSIX): the executable's directory, as on Windows,
unless the executable is inside `.app/Contents/MacOS`, in which case `<user data>/Logs/`. Debug.cpp
and MiniLog.cpp take it through a POSIX-only branch. The crash report was already in user data.
`MemoryInit`'s read of `Data/INI/MemoryPools.ini` beside the executable is a read (absent in a
bundle, so defaults), and was left as it is.

**Checks.**
- `test_posixpath`: `read_only_root_refuses_relative_writes_only`, each writer against the read-only
  root (`EROFS`, nothing changed), reads and absolute writes unaffected; the control with read-only
  off does write.
- `root_readonly_check` (`Tools/root-readonly-check.sh`, ctest, needs `ZH_GAME_DATA`):
  1. the install's listing (names, sizes, mtimes, stat only) is taken FIRST;
  2. a 600-frame skirmish rooted at a farm of the install with the overlay, and a real
     `Data/INI/INIZH.big` planted (the farm link removed first);
  3. afterwards the plant survives, the refusals are reported (`INIZH.big`, `ffprobe.txt`), and the
     farm's listing and the install's are unchanged;
  4. the armed control runs with `-writableRoot` in a root holding only the plant, never a farm of
     the install. It deletes the plant, so the check sees a write when there is one. It runs from
     the executable hard-linked into `Fake.app/Contents/MacOS`, and its log must be in
     `<user data>/Logs`, not in the bundle.

**INCIDENT, recorded as it happened.** The first version of `root-readonly-check.sh` planted the file
with a plain `printf >` into the farm. The install has `Data/INI/INIZH.big` (the stray this whole
step protects), so the farm entry was a link to it, and the write went through the link. **The
install's `Data/INI/INIZH.big` was overwritten with 31 bytes, twice, by the harness, not the game.**
- The check's own install listing diff flagged it.
- No copy exists on this machine and there is no backup, so it cannot be restored here. It is the
  file the game itself deletes on its first Windows start, and nothing reads it
  (`test_bigfilesystem`, `test_install_textures` and `gametext_csf` still pass).
- Restoring it from the install's source, deleting it, or leaving it is the user's decision; the PM
  was told at once.
- The script now removes the link before writing, snapshots the install before it writes anything,
  and runs its writable control outside any farm. A memory records the lesson for later sessions.

**What these cannot see.** A raw writer in a library not swept (the audit covered GameEngine,
GameEngineDevice, Main and WWVegas); a debug build's dumps (not built here); a real signed bundle
(the hard link stands in for its path, not its signature).

## Step 3: one staged overlay, the resolution proof, E1 on the shipped layout (-47, 2026-09-26)

**One staging script.** `Tools/stage-overlay.sh <Code/Data> <Run> <out> [--dev]` is the only place the
shipped overlay list lives (the list above, still inferred). It removes `<out>` and writes every file
into a fresh folder, never through a link. The art archives are linked; the bundle step will copy
through the links. Its stamp is `<out>.staged`, beside the folder, because a file inside it would be a
loose file to the game.
- `zh_overlay` (in ALL) stages `<build>/overlay`, 9.8 MB. That is what the bundle will copy verbatim.
- `zh_overlay_dev` stages `<build>/overlay-dev` with `Scenarios/` and `Cinema/`, which never ship.
- `replay-check.sh` (E1), `overlay-crc-check.sh` and `root-readonly-check.sh` all stage through it.
  A harness that needs the one-folder shape lays the staged overlay over its farm, removing each link
  before the copy.

**E1 runs the shipped resolution.** `replay-check.sh` no longer copies `Code/Data` into its farm. It
passes `-overlay <staged>`, the code path the bundle takes, and writes nothing into the farm. (Its
install hash check is -18's, on E1d.) That ends E1's known limit that it ran without the
fork's `Reforged*.big`: they are in the staged overlay. The seeds' CRCs are unchanged, and
`replay_check` passes.

**`-dumpFileResolution <file>`** (POSIX, `PosixFileResolutionDump.cpp`, called from
`SdlGameEngine::init` after `GameEngine::init`, which then exits). For every path the game can open
(the loose files under its roots, through the union, and every file in the mounted archives) it
writes:
- for a loose file: "loose", its size and the FNV-1a hash of its bytes; an archive's own `.big` gets
  the size only;
- otherwise: the archive that won the path and the member's size;
- then the INI and EXE CRCs.

**`packaging_resolution_check`** (`Tools/packaging-resolution-check.sh`, ctest, needs `ZH_GAME_DATA`):
- W is the install farm with the staged overlay laid over it; P is the install farm plus
  `-overlay`. Their dumps are identical: 31,667 lines (551 loose paths, 31,114 archived), INI CRC
  `0x1E635A82`, EXE CRC `0x0D56B480`.
- The armed control renames the overlay's `ReforgedTextures.big` to `ZReforgedTextures.big`, so it
  mounts after `TexturesZH.big`. That moves 2,681 paths to another archive. The check requires paths
  that change archive, not merely a renamed archive, so it sees order.
- The install is hashed before and after: unchanged.
- What it does not hash: an archive member's bytes. Both layouts read the same `.big` files, so the
  winning archive and the member's size are the question, and the bytes follow from them.

**Checks.** Full ctest, 67: all pass except `d3dx_oracle`, which skips as before. The three P1
harnesses pass on the shared stager, and replay_check passes with `-overlay`.

**What it cannot see.** The bundle itself: step 5 copies the staged folder, and `replay-check.sh
--app` waits for a bundle. The inferred list: see "What Windows does".

## Step 4: where the player's Zero Hour is (-47, 2026-09-26)

**The logic** is `PosixDevice/Install/PosixInstallRoot.cpp`, its own library `installroot` (no engine
state, no SDL), which `PosixMain` calls before anything asks for a path:
1. `-root <dir>`: as given, never validated (harnesses root at farms and test folders).
2. Registry.ini's Zero Hour `InstallPath`, while it still validates. An install can move; a stale key
   falls through.
3. Inside an app bundle only: the known places, silently, with nothing written. These are
   `~/Games/<name>`, `/Applications/<name>`, and CrossOver's and Whisky's bottles'
   `drive_c/Program Files*/{EA Games, Origin Games, Steam/steamapps/common, Command & Conquer The First
   Decade, …}/<name>`, for five spellings of the Zero Hour folder's name.
4. Inside an app bundle only, and never under `-headless`: the player's choice, through SDL's native
   folder dialog, titled "Choose your Command & Conquer Generals Zero Hour folder". A refused choice
   is explained in a message box and asked again; Cancel ends the start. **The chosen folder is the
   only thing written: Registry.ini's `InstallPath`**, so the dialog runs once.
5. Outside a bundle: the executable's directory, as always.

**Validation** (`PosixCheckInstallFolder`), each failure with its own sentence:
- not a folder;
- no `INIZH.big` (not Zero Hour);
- Zero Hour, but the base game's `Textures.big` is nowhere `Win32BIGFileSystem::init` looks. That is
  the folder itself, `ZH_Generals/`, the two sibling names, or Registry.ini's Generals `InstallPath`
  and its First Decade subfolder. The message says to copy Generals' .big files into `ZH_Generals`;
- a folder inside the app bundle or an overlay.
There is no second chooser for the base game (minimal UI, the PM's call); the message says what to do.
"Choose again" is `-root`, or removing the key; an Options entry is a follow-up.

`isExecutableInAppBundle()` (ExecutableDirectory, POSIX) now decides both this and the log folder
(step 2).

**Checks.**
- `test_install_root` (4 tests, 51 checks), in a temporary tree:
  - every validation outcome, including the base game beside, inside, and through Registry.ini and
    First Decade; an armed control: the forbidden folder validates when nothing forbids it;
  - the two failure sentences differ, and each names its remedy;
  - `-root` over a valid Registry.ini, and unvalidated;
  - Registry.ini used while valid, and a moved install falling through to the executable directory
    or, in a bundle, to the known places (`~/Games`' copy without a base game is skipped for the
    bottle's whole one);
  - the known places' list, bottles globbed;
  - the chooser as a stub: re-asked with each reason (not Zero Hour, then no base game), Cancel, a
    folder inside the app; `writeInstallPath` only for a chosen folder.
- `packaging_resolution_check` gains **B, the bundle**: the executable hard-linked into
  `Fake.app/Contents/MacOS` with the staged overlay in `Contents/Resources/Overlay`, run with no
  `-root` and no `-overlay`. With nothing registered and an empty home, it stops and says to use
  `-root`. With Registry.ini's `InstallPath` naming a farm, it finds its overlay and its install on
  its own, and its dump equals W's, all 31,667 lines. The install stays hashed-unchanged.
- Full ctest: 68 tests, all pass except `d3dx_oracle`, which skips as before.

**What it cannot see.** The dialog itself: SDL's folder panel and the message boxes need a person to
click them, and have not been clicked. Players' real install locations beyond these names. A real
signed bundle (step 5).

## Step 5: the `.app` (-47, 2026-09-26)

Built by `ninja macos_app` (Apple only, outside `ALL`) into `<build>/Zero Hour Reforged.app`, with its
dSYM beside it. `Tools/make-macos-app.sh` is the one step.

**Contents, as section 3 says**
- **Info.plist.** `CFBundleExecutable generals`, the version from the build's `BuildVersion.h` (2.1.0),
  `LSMinimumSystemVersion` read from the executable itself (27.0: no deployment target is set yet,
  which E2 should choose), `NSHighResolutionCapable`, the strategy-games category, `AppIcon`.
  - The bundle id is `ZH_APP_BUNDLE_ID`. It defaults to `io.github.olcayseygan.zero-hour-reforged`,
    the project's home per the README; whoever ships may change it.
- **PkgInfo.**
- **`MacOS/generals`,** stripped (30.9 to 24.6 MB). Its 210 MB dSYM sits beside the bundle for C5.
- **`AppIcon.icns`,** from `Generals.ico`'s 48 px image: soft on Retina, until a 1024 px master exists.
- **`Resources/Overlay`,** the staged overlay. Its three art archives are **APFS clones** (`cp -c`)
  of the files the staging links to. There is no fallback to a real copy: another volume or file
  system fails the build.
- **`Resources/Licenses`,** 15 files for 13 entries. `Tools/macos-app-licenses.txt` names the entry
  for every static library on `generals`' link line (read from `build.ninja`: 42 libraries), plus zlib
  and nanosvg, which are compiled into other libraries.
  - A linked library missing from the table fails the build, naming it.
  - FFmpeg's entry adds its source pointer.
  - zlib's licence is extracted from `zlib.h`'s opening comment, since it has no licence file.
  - Still open: FreeType's FTL wants a credit line in the in-game About text as well, which is not
    this step.
- **The HUD directive** (the user's): the build refuses, naming the file, if anything in the overlay
  or the finished bundle sets `ShowHudOverlay` to No, false or 0.
  - The check walks with `find -L`: this machine's `grep` is ugrep, whose `-r` does not follow the
    staging's links to the art, and a recursive grep would have skipped it.
- **The signature,** last: `codesign --force --sign - --timestamp=none`, then
  `codesign --verify --deep --strict`.

**Disk:** 8,097,152 KB free before, 7,764,784 after, lowest 7,728,392.
- The build used 324 MB for a bundle `du` counts at 1.6 GB (the clones in full) plus its 210 MB dSYM.
- The clones saved about 1.6 GB: the 34 MB of the bundle's own contents and the dSYM account for the
  rest, with other sessions' writes.

**Checks**
- `macos_app_check` (16 checks) builds a bundle from this build's own inputs, without art, and checks
  every item above, the seal, and the absence of a HUD-off line. Its armed controls:
  - a planted `ShowHudOverlay = No` INI is refused and leaves no bundle;
  - a link line with an unlicensed library is refused and leaves no bundle;
  - a file written into the signed bundle makes the strict verify fail.
- `replay_check_app`: `replay-check.sh --app` runs E1 on the bundle's own executable.
  - No `-root` and no `-overlay`: Registry.ini's `InstallPath` names the farm, and HOME is an empty
    folder. The game reported its overlay as `Contents/Resources/Overlay` and refused to delete the
    farm's `INIZH.big`.
  - The recording, its playback and a second run agree at frame 1200 on 0x0177BEF6, the same CRC as
    the `-overlay` harness on the same seed.
  - Afterwards the seal still verifies `--deep --strict`: nothing was written into the bundle.
  - Its control: a bundle without its `Overlay` is reported ("did not report its own overlay") and
    fails.
  - It skips (77), saying why, when no bundle has been built.
- The root chooser is tested only through `PosixCheckInstallFolder` (`test_install_root`, step 4),
  never by opening it.

**By hand (the user's check, not done here: it opens windows).**
1. `ninja macos_app` in the build folder.
2. Move `~/Library/Application Support/Command and Conquer Generals Zero Hour Data/Registry.ini` aside,
   so no `InstallPath` is set.
3. Open `Zero Hour Reforged.app` from Finder. A locally built bundle is not quarantined, so no
   right-click Open is needed.
4. The folder dialog, "Choose your Command & Conquer Generals Zero Hour folder":
   - choose a folder that is not Zero Hour (e.g. Documents): a message says so, and the dialog asks
     again;
   - choose a Zero Hour folder whose base game is missing, if one is at hand: the message says to copy
     Generals' `.big` files into `ZH_Generals`;
   - then choose a real Zero Hour folder. A copy is safer than the install itself, although the
     install root is read-only (steps 2 and 3).
5. The game starts, with the corner HUD readout present. Quit.
6. Registry.ini now holds `InstallPath`. Open the app again: no dialog.
7. Once more with the key removed, pressing Cancel: the app ends without starting.
8. `codesign --verify --deep --strict "Zero Hour Reforged.app"` still passes after these runs.
9. Put the original Registry.ini back.

**What this step cannot see**
- Gatekeeper and a quarantined download of an ad-hoc app: E2's, with notarisation.
- The chooser's dialogs, until the user has done the check above.
- A bundle built on another volume or a non-APFS file system: refused by design, never tried.
- The icon's look at Retina sizes.

