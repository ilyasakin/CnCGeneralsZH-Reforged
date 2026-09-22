# B3 — CRT and string shims

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** in review — mac-port-B3 (renames and shim done; four findings handed on)
- **Size:** `stricmp` 71 files, `_snprintf` 12, `_vsnprintf` 8, `_stricmp` 5, `_access` 4,
  `_mkdir` 1, `__int64` 22

## Why

The Microsoft CRT spellings. Individually trivial, collectively the difference between a build that
starts and one that does not.

## Scope

`GameEngine`, `GameEngineDevice`, `Main`, `Libraries/Source/WWVegas`. Note the counts are files,
not call sites — `stricmp` in 71 files is the bulk of this task.

## Do

1. One header of shims, next to B2's. Map the Microsoft spellings to POSIX:
   `stricmp`/`_stricmp` → `strcasecmp`, `strnicmp` → `strncasecmp`, `_snprintf` → `snprintf`,
   `_vsnprintf` → `vsnprintf`, `_access` → `access`, `_mkdir` → `mkdir` (note the mode argument),
   `__int64` → `int64_t`.
2. **`_snprintf` and `snprintf` are not the same function.** MSVC's returns negative and does not
   null-terminate on truncation; C99's returns the length it wanted and always terminates. Any
   caller that checks the return value needs reading, not replacing. There are only 12 files; read
   all of them.
3. `__int64` → `int64_t` is safe and mechanical. Do it properly rather than `#define`-ing it, and
   include `<cstdint>` where it lands.
4. Path separators: 331 string literals in `GameEngine` and `GameEngineDevice` contain a
   backslash. macOS tolerates neither in a path. Most of these are `.big` archive-internal paths,
   where the backslash is part of the on-disk format and **must not change**. Only the ones that
   reach the real filesystem need normalising. Do not sweep this blindly — C1 owns filesystem path
   handling, so find them, list them on this task, and leave the filesystem-bound ones for C1.

## Done when

The shim header builds on both; no Microsoft-only CRT spelling remains outside it and the Windows
platform layer; Windows full build and `ctest` green.

Every `_snprintf` caller whose return value is used is named in the pull request description with a
line saying why the new behaviour is correct there.

## What was done

`Libraries/Include/Platform/MSVCCompat.h` is the one shim header, reached from `always.h` (all of
WWVegas) and from `Lib/BaseType.h` (GameEngine and `compression`). `PreRTS.h` is B5's and was not
touched. `Libraries/Include` is now on the WWVegas targets so it resolves there; nothing in that
directory can shadow a WWVegas header, because its only headers are under `Lib/`, `rts/` and
`Platform/`.

It goes the direction this task asks for — the call sites are renamed to the standard names and MSVC
is taught them, rather than the Microsoft spellings staying put behind a `#define` — and it uses
inline functions rather than macros wherever a function will do.

326 renames across 103 files: `stricmp`/`_stricmp` → `strcasecmp` (247 sites), `strnicmp`/
`_strnicmp` → `strncasecmp` (25), `_strdup` → `strdup` (13), `_snprintf` → `snprintf` (47),
`_vsnprintf` → `vsnprintf` (11), `_access` → `access` (8), `_mkdir` → `mkdir` (1).

### Counts, measured

| Symbol | Task file said | Actually |
|:--|:--|:--|
| `stricmp` | 71 files | 73 files, 213 sites |
| `_snprintf` | 12 files | 13 files (82 sites counting `_vsnprintf`) |
| `__int64` | 22 files | 24 files, 99 sites |

Not in the task file at all, and the same kind of work: `_MAX_PATH` (41 files, 117 sites),
`_isnan` (9 files, 54), `_finite` (2), `_strdup` (5), `__cdecl` (25 files, 58), `__stdcall` (8
files, 53), `__declspec` (6), `__forceinline` (7).

## Two decisions that went against the obvious

**`_MAX_PATH` is not renamed to `PATH_MAX`, and that is the point.** `PATH_MAX` is 1024 on Darwin
against `_MAX_PATH`'s 260, so renaming 117 sites would silently change the size of every
`char name[_MAX_PATH]` in the tree — and some of those are members of structures that go into save
games and into the multiplayer INI checksum. A port whose whole premise is that two builds compute
the same bytes cannot move a struct boundary for tidiness. The shim defines `_MAX_PATH` as 260 on
macOS and no call site changes. `_isnan`/`_finite` are shimmed for a smaller version of the same
reason: 54 sites that would each need reading for whether the `int`-versus-`bool` return matters.

**`_snprintf` → `snprintf` is not a rename.** On MSVC they are different functions: `_snprintf`
returns −1 and writes no terminator when it truncates; `snprintf` returns the length it wanted and
always terminates. Every caller that reads the return value therefore changes behaviour on Windows
as well, and each was read:

| Caller | What it did | What it does |
|:--|:--|:--|
| `AsciiString::format_va` ×2 | `if (_vsnprintf(...) < 0) throw` — a test C99 never satisfies | tests `wanted >= sizeof(buf)`. It also passed `sizeof-1` as the bound, so a string that exactly filled the buffer left it unterminated for `set()` to read past; that is now impossible |
| `FileClass::Printf` ×3 (`wwfile.cpp`) | handed the return straight to `Write(text, length)` | clamped. Under C99 a truncated format returns more than the buffer holds, so `Write` would read past the end — and Microsoft's −1 was a negative length. **Both platforms were wrong here before** |
| `File::print` | already tested `len < 0 \|\| len >= sizeof(buffer)` | rename, and a comment that no longer says the wrong thing |
| `Debug.cpp` assertion path | already named its variable `wanted` and tested it the C99 way | rename only |
| `StringClass::Format` ×2 (`wwstring.cpp`) | returns the value to its caller; nothing in the tree reads it | bound moves from 512 to the whole array, keeping 512 formatted characters working |

Two files are deliberately left alone, both Windows-only:

- **`EarlyOptions.h`** — three sites using the Microsoft return convention to detect truncation, in a
  file built on `CreateFileA`/`HANDLE`. No macOS build reads it.
- **`ChromaKeyboard.cpp`** — accumulates `used += _snprintf(body + used, bodyBytes - used, ...)`.
  Under C99 that can walk `used` past the buffer and then pass a negative size to the next call,
  which converts to an enormous `size_t`. It needs rewriting rather than renaming, and nothing on
  macOS gains from it. **Worth a task of its own** — it is a latent overflow on Windows the moment
  anything there starts returning C99 lengths.

## Path separators: found, listed, not swept

328 literals containing a backslash, across 85 files in `GameEngine`, `GameEngineDevice` and `Main`.
They fall into four groups and only the third is C1's:

| Group | Example | What to do |
|:--|:--|:--|
| **Archive-internal paths**, the bulk of it | `GameEngine.cpp`'s 49, e.g. `Data\INI\INIZH.big`; `GameAudio.cpp`'s 10; `GameLogic.cpp`'s `Data\Scripts\MultiplayerScripts.scb` | **Nothing.** The backslash is part of the `.big` table of contents and always will be |
| **Separator character sets**, not paths at all | `ArchiveFile.cpp`, `ArchiveFileSystem.cpp`, `CommandLine.cpp`, `GameInfo.cpp`: the two-character string `"\/"` handed to a tokeniser meaning "either separator" | **Nothing.** Already accepts both |
| **Real filesystem paths** | `GameState.cpp`'s `Save\`, `UserPreferences.cpp` and `PersistentStorageThread.cpp`'s `GeneralsOnline\...ini`, `MapUtil.cpp`'s `%s\%s`, `FileTransfer.cpp`'s `%s\%s.tga`, `Win32BIGFileSystem.cpp`'s `ZH_Generals\`, `GlobalData.cpp`'s `.\` | **C1's.** Left as they are |
| **GameSpy key/value protocol** | `PeerThread.cpp`'s 37 and `PersistentStorageThread.cpp`'s 32, e.g. `\ergc`, `\Widen\%d` | **Nothing.** The backslash is the GameSpy wire delimiter, not a path |

`W3DShaderManager.cpp`'s 9 (`shaders\monochrome.pso`) are archive paths reached through the W3D
file system, so they belong with the first group, but they are in the device layer and D-track will
see them again.

## Findings that are not B3's

1. **`hrawanim.cpp`'s frame idiom is a pre-existing bug in the shipping Windows build.**
   `Float_To_Long(frame - 0.499999f)` is an exact floor only below 33. Float spacing is 1.9e-6 in
   [16,32) but 3.8e-6 in [32,64), and `0.499999f` sits 1e-6 below a half — so from 32 upwards the
   subtraction lands on an exact tie, and at an **odd** integer frame the tie rounds to the even
   neighbour below and the idiom returns `frame-1`. `To_Long(33 - 0.499999f)` is 32. Even frames
   survive by luck. `cvtss2si` under `_RC_NEAR` does the same, so every architecture agrees and it
   does not block the port — but an animation picking frame 32 where it means 33 is player-visible.
   Three call sites, `hrawanim.cpp:460,518,603`, plus `htree.cpp:624`. **D-track, `ww3d2`.**
2. **Language conformance has no owner.** Two-phase lookup on dependent bases, redundant class
   qualification, and a missing `template<>` all blocked B3's own unlock and were fixed here:
   `Vector.H`, `simplevec.h`, `v3_rnd.h`, `mempool.h`. `CMakeLists.txt`'s own `/permissive` comment
   predicted "simplevec.h et al" and this is the et al. There will be far more as `GameEngine`
   compiles. **Wants a task.**
3. **`_interlockedbittestandset` needs an arm64 replacement**, at `mutex.h:141` and
   `wwmemlog.cpp:322`. An atomic test-and-set; arm64 wants `__atomic_test_and_set` or
   `std::atomic_flag`. **B8.** The `<intrin.h>` guards here only make the error name it.
4. **`compression` is one line from building.** With A2's zlib fix, 26 of 28 objects compile and the
   only remaining error in the library is `BaseType.h`'s `#include <windef.h>`. **B5.**

## Where the two targets stand

| Target | Objects | Blocked by |
|:--|:--|:--|
| `compression` | 26 of 28 | `BaseType.h`'s `<windef.h>` — B5 |
| `wwmath` | 24 of 35 | `mutex.h`'s `_interlockedbittestandset` (B8) and `win.h`'s `<windows.h>` (B5). Nothing else |
| `detround_selfcheck` | builds, **and passes** | — |

## A mistake worth leaving here

The bulk rename walked into the shim header itself and rewrote `return _stricmp(a, b)` inside the
MSVC wrapper into a call to `strcasecmp` — making `strcasecmp` call itself. That compiles, recurses
forever, and does it **only on Windows**, where nobody here could have seen it. It was caught by
re-reading the script's own scope rather than by a compiler. The header now says at the top that
sweeps must exclude it.

The other one: a case-sensitive grep for `_Interlocked` missed `_interlockedbittestandset`, and two
committed comments claimed a file named no intrinsic when it named that one. Search for MSVC
intrinsics case-insensitively.

## Do not

- Do not touch archive-internal path strings. A `.big` file's table of contents uses backslashes
  and always will.
- Do not add a compatibility `#define stricmp strcasecmp` and call it done. Rename the call sites;
  the macro will collide with something in six months.
