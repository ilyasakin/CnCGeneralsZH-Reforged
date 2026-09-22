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
| **The tooling** | **`grep -r` from the repo root on this machine silently skips 466 tracked files**, 356 of them in `GeneralsMD` — including every FFmpeg header and 54 sources in two built libraries. Measured, reproducible, and it affects every sweep this project has run that way. |
| **zlib `zutil.h:113`** | Still there. The rule records it as found; nothing fixed it, and §4 says why that is harder than it looks. |

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

**`zutil.h:113` has not been fixed.** `#if defined(MACOS) || defined(TARGET_OS_MAC)` with
`#define fdopen(fd,mode) NULL` inside it, exactly as the README describes, and the README argues
for fixing it. The reason it is awkward, which is probably why it did not happen: zlib is
**fetched, not committed**, so an edit there is lost the next time anybody vendors. It needs either
a patch applied by the vendor step — the mechanism `d3d8to9-d3d9on12.patch` already establishes in
this tree — or a compile definition. Filing it here rather than fixing it in a directory whose
contents are `.gitignore`d.

## 5. What was not swept

`GameSpy`'s 726 files got the predicate half only, which is what the rule's automatable half is
for; the human half over 726 files of SDK is a different task and nobody has claimed it. The seven
hits above are all accounted for. `Generals/` is out of the plan's scope throughout.
