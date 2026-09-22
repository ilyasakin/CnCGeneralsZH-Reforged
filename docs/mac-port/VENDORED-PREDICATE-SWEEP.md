# The vendored-predicate rule, run to the end

Assignment: the WWVegas half of the sweep — EAC and the committed FFmpeg dist, neither of which
anyone had run the rule against. Run 2026-09-22 against `64e1f8ee`.

Two of the three results are negative, and the instruction was to record a negative as a result.
They are here as results. The third is a live instance of the rule's own bug shape, in the file
that already contained one.

## 0. The short version

| | |
|:--|:--|
| **EAC, predicate half** | Clean. The only hits are the `__APPLE__`/`__BIG_ENDIAN__` lines of the fix already made in `gimex.h`. |
| **EAC, human half** | **One live instance**, in `gimex.h` again, and the automatable pattern list could never have found it. Fixed here, with the branch change proved by compiling it. |
| **FFmpeg dist, both halves** | Clean, and it was never going to be otherwise — see §3. |
| **The rule's pattern list** | **Incomplete.** It has no `__i386__`, which is what the new instance is written with. One line to fix. |
| **GameSpy, human half** | Claimed and done — §5. 44 hits read, **no defect reaching this port**, one trap documented for whoever builds it on macOS, and 16 more case-sensitivity sites for C1. |
| **The tooling** | **`grep -r` from the repo root on this machine silently skips 466 tracked files**, 356 of them in `GeneralsMD` — including every FFmpeg header and 54 sources in two built libraries. Measured, reproducible, and it affects every sweep this project has run that way. |
| **zlib `zutil.h:113`** | ~~Still there.~~ **Fixed since this was written** — `vendor.sh` now narrows the predicate at vendor time (`0a2dba01`), which is the patch-at-fetch mechanism §4 asks for. |

## 1. EAC

Fourteen files, nine of them built (`btree*`, `huff*`, `ref*` — RefPack is what
`CompressionManager` uses, so this is on the path that save games and network packets are made of).

### The predicate half: clean

```console
$ grep -rIn --no-ignore-files -E '__APPLE__|TARGET_OS_|\bMACOS\b|__BIG_ENDIAN|POWERPC' EAC/
gimex.h:380,381   (comment, describing the fix)
gimex.h:387       (the fixed ggetm branch)
gimex.h:435,436   (comment)
gimex.h:442       (the fixed gputm branch)
```

Six hits, all six part of the `ggetm`/`gputm` fix already in the file. Nothing new.

### The human half: one live instance, and the pattern list would never have found it

`gimex.h:99` chose the `ARGB` channel order like this:

```c
#if defined(_MSC_VER) || defined(__i386__)     /* -> b,g,r,a */
#elif defined(__R5900) || defined(SGI)         /* PS2 -> r,g,b,a */
#else /* GameCube/Mac */                       /* -> a,r,g,b */
```

It is the same shorthand as the bug already fixed twenty lines further down, wearing a different
predicate: **the first branch asks "MSVC or i386?" and means "little-endian desktop".** On Apple
Silicon none of `_MSC_VER`, `__i386__`, `__R5900` or `SGI` is defined, so the struct falls through
to the GameCube/Mac order — the big-endian one — on a little-endian machine.

Proved rather than argued, by compiling the header both ways on arm64:

```
before:  sizeof(ARGB)=4   a@0 r@1 g@2 b@3     <- the big-endian order
after:   sizeof(ARGB)=4   b@0 g@1 r@2 a@3     <- what MSVC gets
```

**Fixed**, by adding a byte-order branch as a *fourth* arm rather than rewriting the first, so every
platform that already had an answer keeps exactly the answer it had: MSVC and i386 still take the
first branch, PS2 and SGI the second, a genuinely big-endian machine the last. Only the case that
had no branch of its own changes, and on Windows the preprocessor output is identical.

**It is inert today.** `ARGB` is used in exactly one place — `GBITMAPINFO`'s 8-bit `colourtbl` —
and nothing in the built tree reads it. A landmine, not a fire; which is the argument the rule
already makes for fixing it while the file is open.

**What this says about the rule:** the automatable pattern list is
`__APPLE__|TARGET_OS_|MACOS|__BIG_ENDIAN|POWERPC`. This instance contains none of them. A
2003-era "which machine am I?" predicate is just as often written as the *positive* CPU test —
`__i386__`, `_M_IX86`, `__x86_64__` — as the negative platform one. **`__i386__` should be in the
list**, and it costs nothing: adding it returns one further hit in the whole tree, in commented-out
GameSpy PS3 code.

### The rest of the human half: three things checked and clean

- **Integer widths.** The only `long` in EAC is `GPOS`, and its `#if` already resolves to
  `__int64` on MSVC and `long long` everywhere else — 64 bits on both. No `unsigned long` used as
  a fixed-width type anywhere; the LZH-Light bug shape is not present.
- **The RefPack hash table**, which is the LZH bug's exact shape — an index whose width outgrew its
  table. `HASH()` is built from three bytes and cannot exceed `0xFFFF`; `hashtbl` is
  `galloc(65536*sizeof(int))`; the chain index is masked with `&131071`. In bounds.
- **Stream reads.** Every multi-byte field in `refdecode`, `btreedecode` and `huffdecode` is
  assembled byte-at-a-time with shifts — `type = (type<<8) + *s++` — never through a cast to a
  wider pointer. The codecs are endian-independent by construction. This is worth recording
  because it is the reason the `ggetm` bug was confined to the *header* fields.
- `typedef enum{False,True}bool` at `gimex.h:285` is correctly guarded by `!defined(__cplusplus)`.

## 2. The FFmpeg dist

136 headers, 5 Windows DLLs, 5 import libraries, a licence.

### Both halves: clean

One predicate hit in 136 headers — `libavutil/hwcontext_opencl.h:22`, `#ifdef __APPLE__` choosing
`<OpenCL/cl.h>` over `<CL/cl.h>`. That is a *correct, modern* use of `__APPLE__`, and nothing in
the tree includes that header. Two `unsigned long` in `libavformat/avio.h` (the `AVIOContext`
checksum field), which is FFmpeg's own ABI rather than ours. One typedef under an `#if`, and the
`#if` is `FF_API_INIT_PACKET`, a version macro. The `_WIN32` hits are all in the D3D11, DXVA2 and
Vulkan hardware-context headers, which we never include.

This is the expected answer and it is worth saying why: these headers are 2020s FFmpeg, written
against `stdint.h` by people who build on twelve platforms. The rule finds 2003-era code that
guessed; FFmpeg does not guess.

### What is worth knowing about the dist is structural, and already owned

The binaries are `PE32+ executable (DLL) (GUI) x86-64, for MS Windows`. There is no FFmpeg here
for macOS at all, and **A2 already says so** (`A2-vendor-posix.md:28`: "not for M1. It is a Windows
`.lib` dist; Mac wants Homebrew or a dylib build"), and hands it to C4 and the D track. Confirming,
not reporting.

One specific note for whoever picks that up, which came out of reading the headers:
`libavutil/avconfig.h` is **generated by configure** and this copy carries
`AV_HAVE_BIGENDIAN 0` and `AV_HAVE_FAST_UNALIGNED 1` — values baked by a Windows x86-64 build.
Both happen to be correct for Apple Silicon, so reusing this include directory would work by luck.
Do not: regenerate the headers from whatever build actually ships, because `avio.h`'s
`unsigned long checksum` is 32 bits in the library these headers came from and 64 in an LP64 one,
and a header/library mismatch there is a silently wrong struct layout rather than a link error.

## 3. The tooling finding, which is larger than the sweep

**`grep -r` from the repository root on this machine does not see 466 tracked files.**

`grep` here is **ugrep 7.8.4**, which honours `.gitignore` by default. Git does not ignore these
files — `git check-ignore -v` says nothing about them — so this is the tool's idea of "ignored"
rather than git's. Naming a directory explicitly avoids it; recursing from the root does not:

```console
$ grep -rln 'DEBUG' . | grep -c 'Libraries/Source/debug/'                    0
$ grep -rln --no-ignore-files 'DEBUG' . | grep -c 'Libraries/Source/debug/'  13

$ grep -rln 'include' . | grep -c 'FFmpeg/dist/include'                      0
$ grep -rln --no-ignore-files 'include' . | grep -c 'FFmpeg/dist/include'  124
```

Measured against `git ls-files`: 6,514 tracked files, **6,049 scanned, 466 skipped**. The in-scope
ones:

| Skipped | What it is |
|--:|:--|
| 147 | `Libraries/Source/FFmpeg` — the whole committed dist, headers included |
| 53 | `Libraries/Source/debug` — **a built library** (`debuglib`) |
| 24 | `Libraries/Source/profile` — **a built library**, and the home of `profile/internal.h`, which B14 ported |
| 43 | `Tools/WorldBuilder/res` |
| 10 | `Libraries/Source/WWVegas` — Miles6 headers, `wwshade/shdpp.exe`, `WW3D2/RequiredAssets` |
| 79 | the rest, mostly `Tools/` and `Data/` |

54 `.cpp`/`.h` files in two libraries that are compiled into the game have been invisible to every
sweep this project has run with a root-level `grep -r`. That includes B5's Win32-type survey, the
threading-primitive sweep, and my own D1 and B2 counts. None of those conclusions is necessarily
wrong — most named their directories — but none of them can be assumed complete either.

**What to do about it:** pass `--no-ignore-files` to any sweep that is meant to be exhaustive, or
drive the file list from `git ls-files` instead of from the filesystem. If the predicate half
becomes a CI check, it must do one or the other, or it will report clean on the files it never
opened.

### A second way the same check can pass on nothing

`zlib`, `GameSpy` and most of `LZHCompress` are **not in the repository**. Their directories hold a
`.gitignore` that ignores everything and the sources are fetched by `build.bat` before configuring.
In a fresh `git worktree` they are empty, so the rule finds nothing there and says so cheerfully:

```console
$ find Libraries/Source/GameSpy -type f | wc -l      # in this worktree
1
$ find Libraries/Source/GameSpy -type f | wc -l      # in the main checkout, after vendoring
726
```

A CI check must run **after** the vendor step, and should fail rather than pass if the directory it
was pointed at is empty.

### A third way, which is the one that actually produced a wrong number

Vendoring the sources is not enough. Those same `.gitignore` files that keep the SDK out of the
repository are **also** what ugrep reads, so once `Libraries/Source/GameSpy` is populated,
`grep -rn` over it still opens nothing:

```console
$ grep -rn socket Libraries/Source/GameSpy | wc -l                            0
$ /usr/bin/grep -rn socket --include='*.c' Libraries/Source/GameSpy | wc -l  854
```

This is the same root cause as §3 but a different victim set: §3 is about *tracked* files skipped
by root-level rules, and this is about *vendored* files skipped by the `*` in their own directory —
present on disk, invisible to the sweep, in the exact tree the rule most wants read.

It is not hypothetical. The plan's rule claimed the predicate patterns return "five hits total
across zlib, LZH-Light, EAC and **all 564 GameSpy files**". GameSpy contributed **zero** of those
five, because not one of its files was opened. The number was mine and it was wrong.

**So a sweep needs a control.** Run a pattern that must match and check it is non-zero before
believing a null result:

```sh
SRC=$(find <dir> -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \))
echo "$SRC" | wc -l                                                      # expected file count?
echo "$SRC" | tr '\n' '\0' | xargs -0 /usr/bin/grep -l socket | wc -l   # control: must be > 0
echo "$SRC" | tr '\n' '\0' | xargs -0 /usr/bin/grep -nE '<patterns>'
```

## 4. The counts, re-measured with a tool that opens every file

Run in the main checkout, where the vendored sources actually exist.

| Tree | Files | Predicate hits | What they are |
|:--|--:|--:|:--|
| `Compression/EAC` | 14 | 6 | the `gimex.h` fix already made |
| `FFmpeg` | 148 | 1 | `hwcontext_opencl.h`, correct and unreached |
| `GameSpy` | 726 | 7 | six are modern `__APPLE__ && __MACH__` Darwin support; the seventh is inside a `/* */` block marked "removed since only ps3 used" — and it is `(defined (__APPLE__) && ! defined (__i386__))`, the shape again, in dead text |
| `Compression/ZLib` | 28 | 1 | **`zutil.h:113`, still live** |
| `Compression/LZHCompress` | 16 | 0 | |
| `WPAudio`, `debug`, `profile`, `Benchmark`, `EABrowserDispatch` | 98 | 0 | |

The rule's "five hits total" is the right order of magnitude and still true of the *automatable*
half: the predicate patterns are quiet enough to run on every build. With `__i386__` added it is
still quiet — one more hit, in commented-out code.

**`zutil.h:113` was fixed after this section was written** (`0a2dba01`), by exactly the route it
proposes: `vendor.sh` narrows the predicate as it unpacks, so the edit survives a re-fetch. The
condition becomes `(defined(MACOS) || defined(TARGET_OS_MAC)) && !defined(__APPLE__)`, Darwin falls
through to zlib's own `#define OS_CODE 0x03 /* assume Unix */`, and `fdopen` stays a real function.
Measured before and after: `OS_CODE` 0x07 -> 0x03, `fdopen` macro -> not a macro.

## 5. GameSpy, the human half

Claimed and done, 2026-09-22. §4 ran the automatable patterns over GameSpy and got 7 hits. Widening
the list to `_MACOSX`, `__MWERKS__`, `__MACH__`, `__POWERPC__` and `_M_PPC` over the 564 `.c`/`.h`/
`.cpp` files gives **44**, and reading them gives the result below. Control: 108 of those files
contain `socket`.

**Outcome: no defect reaching this port.** Not "no hits" — 44 hits, read, and each accounted for.

**Every platform decision is centralised in `include/gamespy/gsplatform.h`.** There is not one
Apple, PowerPC or endianness predicate in the other 563 files. Most of the remaining hits are
`__MWERKS__` guards around prototypes in sample programs that are not built.

**Scope, which bounds all of it:** `add_subdirectory(GameSpy)` is inside `if(ZH_PLATFORM_WINDOWS)`,
so no GameSpy `.c` compiles on macOS at all. Its *headers* are on `gameengine`'s include path and
will be preprocessed there as soon as B1–B6 let gameengine build, so header predicates are live and
source predicates are not.

### Checked and correct

| | |
|:--|:--|
| **Endianness** | `gsplatform.h:243` already reads `//defined(_MACOSX)` — commented out of the big-endian list upstream. macOS gets `GSI_LITTLE_ENDIAN`, verified by preprocessing the game's own header chain. This is the `gimex.h` bug, already fixed here by someone else. |
| **Integer widths** | `gsi_i32`/`gsi_u32` are `int`/`unsigned int`, not `long`, so GameSpy does not have LZH-Light's problem. The `_UNIX` branch types `gsi_u64` as `unsigned long long`. Correct on LP64. |
| **The game's header chain** | `#include "GameSpy/Peer/Peer.h"` preprocesses clean for arm64 as C++. One warning, and it is the case issue below. |

### Dead branches, each measured rather than assumed

`_MACOSX` is **never defined**: `gsplatform.h:37` defines only `_UNIX` for `__APPLE__ && __MACH__`,
and neither CMakeLists defines it. Every `_MACOSX` branch is therefore dead on macOS.

- `gsplatformsocket.h:578` — macros casting socket length arguments to `socklen_t`, defined only
  for `_PS3` and `_MACOSX`, with no `_UNIX` equivalent. Compiled all 21 GameSpy `.c` files that
  call `accept`/`bind`/`getsockopt`/`recvfrom`/`getsockname` for arm64 with and without
  `-D_MACOSX`: **identical diagnostics** (2, both unrelated). Unnecessary in C on a modern SDK.
- `gsplatform.h:252` — `#if defined(_MACOSX) #undef _T #endif`. The macOS SDK's `_ctype.h:113` does
  define `_T`, so this was doing real work once. Dead now and costs nothing, because
  `gsplatform.h:372` undefines `_T` unconditionally before GameSpy defines its own. Verified:
  GameSpy's `_T(a) a` survives and `ghttpmain.c`, which calls `_T("wb")`, compiles clean.
- `gsplatformutil.h:37` — `GSI_UNUSED(x)` expands to nothing rather than to reference-and-discard.
  Cosmetic.

### One trap for whoever builds GameSpy on macOS

`GSI_MAX_INTEGRAL_BITS` falls back to **32** on clang, because `gsplatform.h:294` derives it from
`_INTEGRAL_MAX_BITS`, which only MSVC defines. macOS obviously has 64-bit integers — the same header
types `gsi_u64` as `unsigned long long` twenty lines on — so the obvious repair is to force it to 64.

**Do not, without fixing `gsplatform.h:351` in the same change.** That line is
`#define GSI_MAX_U64 0xffffffffffffffffui64`, and `ui64` is an MSVC-only suffix clang rejects
outright. The 32-bit fallback is the only thing keeping it out of the build, and the error lands at
the first *use* of `GSI_MAX_U64`, not at the definition — so the naive fix looks fine until
something references it. Nothing in this port needs 64-bit ghttp.

### Case sensitivity: 16 sites, and not GameSpy's doing

The SDK ships `include/gamespy/...` all lowercase; the game spells it `"GameSpy/Peer/Peer.h"`.
Checked every `#include` in `GameEngine` naming gamespy against the real include path with exact
case: 177 resolve exactly, **16 sites across 7 distinct paths resolve only case-insensitively**
(`GameSpy/GP/GP.h`, `GameSpy/Peer/Peer.h`, `GameSpy/peer/peer.h`, `GameSpy/ghttp/ghttp.h`,
`GameSpy/gstats/gpersist.h`, plus `GameNetwork/GameSpy/Peerdefs.h` and `.../peerDefs.h` into the
game's own headers). Two more are unresolved, both `GameNetwork/GameSpy.h`, which was stripped from
the release and whose only two includers CMake already removes.

These belong with the seven C1 already records. Same defect, not a new kind.

## 6. What was not swept

`Generals/` is out of the plan's scope throughout.
