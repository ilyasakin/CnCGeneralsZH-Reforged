# B1 — WideChar to char16_t

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** in progress: steps 1-3 merged; the typedef flip waits on `gameengine` compiling (was -3a)
- **Size:** 48 files mention `WideChar`, 724 `L"` literals in engine + device + Main, ~20 distinct
  `wcs*` calls

## Why

`Libraries/Include/Lib/BaseType.h:143`:

```cpp
typedef wchar_t WideChar;  ///< multi-byte character representations
```

`wchar_t` is 2 bytes under MSVC and 4 bytes under clang. This is not a warning, a nuisance or
something to paper over with a cast. It is a file-format and wire-format break, and it is the one
portability problem in this tree that fails silently.

The proof is in the tree, not in theory:

| Site | What breaks at 4 bytes |
|:--|:--|
| `GameClient/GameText.cpp:1056` | `file->read(m_tbuffer, len*sizeof(WideChar))` — every `.csf` string file is UTF-16 on disk. At 4 bytes this reads twice as much as the file holds and every string is garbage. |
| `Common/System/DataChunk.cpp:368,981` | map and scenario chunks, written and read by the same arithmetic |
| `Common/System/XferSave.cpp:335`, `XferLoad.cpp:232` | save games |
| **`Common/System/Xfer.cpp:209`** | **the checksum**, and this file cited the wrong line for it. `XferCRC` does **not** override `xferUnicodeString` — `XferCRC.cpp:355` is `XferDeepCRC`'s. So a plain `XferCRC` over a `UnicodeString` goes through the base `Xfer::xferUnicodeString` at `Xfer.cpp:209`, which is therefore not merely adjacent to the replay and network checksum path, it **is** that path. `sizeof(WideChar) * len`. A Mac build with 4-byte `WideChar` desyncs against Windows on the first unit with a name, and reports it as a desync rather than as a bug. |

`UnicodeString.cpp:96,103` size their allocations by it, so the string class itself changes shape.

There is no version of this port where B1 is skipped or deferred.

## Scope

- `Libraries/Include/Lib/BaseType.h` — the typedef
- `GameEngine/Include/Common/UnicodeString.h` and `Source/Common/System/UnicodeString.cpp` — the
  string class, its `peek()`, its allocation arithmetic, its `_vsnwprintf` formatting
- The 48 files that name `WideChar`
- The 724 `L"..."` literals
- The `wcs*` calls: `wcslen` 26, `wcsncpy` 25, `wcscmp` 15, `wcscpy` 13, `wcsstr` 7, `wcschr` 6,
  `wsprintf` 6, `wcstrim` 4, `wcsicmp` 3, `wcscat` 3, `swprintf` 3, and singles of `wcspbrk`,
  `wcstol`, `wcstok`, `wcstod`, `wcsspn`, `wcsrchr`, `wcsnicmp`, `wcsncmp`, `wcslcpy`

## Recon findings, 2026-09-22 — several of this file's numbers were wrong

Survey at `docs/mac-port/B1-widechar-survey.md`. Corrections, and two things this file missed
entirely.

**Counts.** "724 `L"` literals" came from a naive grep that also counted narrow strings ending in
a capital L (`"ALL"`, `"CONTROL"`). The real figure is **~533 wide string literals in 80 files**,
and **57** files name `WideChar`, not 48.

**The "handful" bound to Win32 wide APIs is 14 literals on 10 lines in 3 files** — and my guess at
which files was wrong. There is no `L"..."` near a `MessageBoxW` or a registry call; the registry
code is all `AsciiString`/`TCHAR`. The real set is `EarlyCommandLine.h`'s four callers
(`WinMain.cpp:1242`, `Debug.cpp:168`, `Debug.cpp:403`, `JobSystem.cpp:113` — that header is
deliberately `wchar_t` throughout because `WinMain` tokenises `lpCmdLine` in place, so
`GetCommandLineA` is truncated), plus `urllaunch.cpp` and `simpleplayer.cpp`, which are `LPWSTR`
shell-association code that never mentions `WideChar`.

**Consequence for the shim, and it is load-bearing:** because those files call the real `wcsstr`,
`wcscpy` and friends on real `LPWSTR`s, `WideCharFns.h` **must not be macros over the `wcs*`
names**, and should not be overloads on them either. Use distinct names. `Language.h:77-99` already
defines twenty `Game*` macros over the CRT wide functions and only five are called — repoint those
twenty at the new header in one commit and the live call sites port themselves.

**MISSED ENTIRELY: 1,016 wide CHARACTER literals (`L'x'`), 914 of them in `Keyboard.cpp`.** These
**convert silently** — `L'x'` to `char16_t` is an integral conversion with no diagnostic — so
unlike the string literals, the compiler will not hand you this worklist. Two need a human:
`Keyboard.cpp:467` is U+FFFD, a mojibake'd euro on the UK layout, and MSVC and clang already
disagree about it because nothing passes `/utf-8`; `NetworkUtil.cpp:451-463` tests
`L'\xd800'`–`L'\xdfff'`, which is code that means "this is UTF-16".

**MISSED: ~140 `%ls`/`%ws`/`%S` specifiers, 91 of them in NARROW format strings** being handed a
`WideChar*`. `printf("%ls", p)` promises the CRT a `wchar_t*`; on Windows with `char16_t` that
stays true by layout accident, and on macOS the CRT reads the buffer as UCS-4 and prints garbage.
**This compiles clean on both platforms.** Four are `CRCDEBUG_LOG` in `Player.cpp` — so leaving
them alone removes the instrument you would use to debug the very desync B1 exists to prevent. It
need not be fixed in B1; it must be **decided** in B1.

**My `sizeof(WideChar)` list was correct but incomplete.** Four more literal sites, of which
`NetPacket.cpp:365` and `:5109` belong next to `XferCRC.cpp:355` in the argument above — they size
a `WideChar` argument in the **lockstep command stream**. Plus twelve sites that say it another way
(`* 2`, `/ 2`, `sizeof(UnsignedShort)`) and work only because the widths coincide under MSVC;
`UnicodeString.cpp:350` is a bare magic `2` inside the string class itself.

**Three format funnels, not one.** `_vsnwprintf` (5 sites) is the one this file named. Also
`swscanf` (`NetworkDirectConnect.cpp:122,150`) and `fwprintf` (`Recorder.cpp:590` and neighbours) —
and that last one writes the **replay file header**, so doing it first gives B1 a canary that
`replay-check.ps1` runs straight through. Make the funnel present MSVC's truncation contract on
both platforms: MSVC's `_vsnwprintf` returns negative and does not terminate, C99's `vswprintf`
returns the would-be length, and all five call sites throw `ERROR_OUT_OF_MEMORY` on negative.

**One signature has to change**, and it is the only one: `ThreadUtils.cpp:33-78` allocates a
`WideChar *dest` and returns `std::wstring ret = dest;`. No boundary cast fixes a wrong return
type. Decide it before the sweep.

`IMEManager.cpp` is the dangerous file: `ImmGetCompositionStringW` and `ImmGetCandidateListW` take
`LPVOID`, so nothing in its Win32→`WideChar` direction produces a diagnostic, and it is also where
the `/2` and `*2` live. Read it by hand.

**Recommended order** (§6 of the survey): `Language.h` + `WideCharFns.h`, then the three format
funnels, **then** the typedef — which generates the worklist — then the compiler's list, then the
1,016 `L'x'` separately, then normalise the `*2` idioms, then decide the `%ls`. Steps 1, 2 and 6
are no-ops on Windows by construction.

Nothing in `GameLogic` touches a Win32 wide API. Nothing was compiled for this survey, so every
"hard error" and "silent" claim is read off the language rules; the first clang build is what turns
them into facts.

## Steps 1-2 findings, 2026-09-22 — the compiler found what reading could not

**macOS's `vswprintf`, `vswscanf` and `fwprintf` return -1 with `errno == EILSEQ` on ANY character
outside ASCII while the process is in the default "C" locale.** Measured:

```
default (C locale)       swprintf -> -1 errno=92    fwprintf -> -1 errno=92
uselocale(en_US.UTF-8)   swprintf ->  3 errno=0     fwprintf ->  2
```

MSVC's `_vsnwprintf` does no such conversion and passes anything through. The widen-to-`wchar_t`,
call-`vswprintf`, narrow-back funnel this task's own recon recommended would therefore have made
**every `UnicodeString::format` of a Turkish, German or French string return negative** — which
`format_va` turns into a thrown `ERROR_OUT_OF_MEMORY`. Every localisation but English would have
aborted on the first formatted message, on the Mac only, with nothing in the log naming why. It
would have survived review, because reading the code tells you nothing about it.

Fixed with `uselocale()` — POSIX-2008, sets `LC_CTYPE` for the calling thread only. It must stay
thread-local: the engine formats from more than one thread.

**`Recorder.cpp` has an M2 blocker that is not B1's.** It opens the replay `"wb"` and then mixes
`fprintf`/`fwrite` with `fwprintf`/`fputwc`/`fgetwc` on the same `FILE*`. That is undefined. MSVC
tolerates it; a POSIX C library sets the stream's orientation on first use and then **fails every
call of the other kind**. The replay writer needs a byte-oriented rewrite before it runs on a Mac
at all. Recorded in C1.

**42 wide format strings carry a `%ls`/`%ws` and pass a `WideChar*`.** Those arguments are in the
`va_list` and no funnel can reach them — same sweep as the 91 narrow ones, and they should be done
together.

## The `%ls` sweep is split, decided 2026-09-22

The 138-odd `%ls`/`%ws`/`%S` specifiers are **two problems**, not one, and only the first is B1's.

**The 91 in NARROW format strings are mechanical and are being done** (option b: wrap the argument,
change `%ls` to `%s`). Two things about them that were not obvious:

- **`AsciiString::translate` cannot be the helper**, though it is what anyone would reach for.
  `AsciiString.cpp:181` carries its own admission — `/// @todo srj put in a real translation here;
  this will only work for 7-bit ascii` — and line 185 is `concat((char)stringSrc.getCharAt(i))`. A
  truncating cast: `Ç` becomes byte `0xC7`, `İ` becomes `0x30`, the digit zero. Using it would make
  the logs **worse** than today's `%ls`. The sweep needs a real UTF-8 conversion with storage that
  lives for the call, because three sites carry two `%ls` in one statement
  (`LanguageFilter.h:58`, `Recorder.cpp:1116`, `:1121`) and a static scratch buffer would have the
  second overwrite the first.
- **It removes a locale dependency rather than adding one.** `printf("%ls", p)` on macOS in the C
  locale fails with `EILSEQ` on any non-ASCII, the same root cause as the `vswprintf` finding
  above. So the current spelling is broken on Mac twice over — wrong width *and* locale-dependent.
  A UTF-8 helper is pure code-unit arithmetic with no CRT conversion, so it has no locale behaviour
  to get wrong.
- It is **not** a no-op on Windows: `%ls` converts through the current ANSI codepage today and the
  log will carry UTF-8 after. For a Turkish or German player name that is different bytes in
  `DebugLogFile.txt`, which is the file users send back. An improvement — today a name outside
  CP1254 is `?` — but a change, and it gets a debt row saying so.

**The 42 in WIDE format strings are deferred and need a design decision.** They are correct on MSVC
today and stay correct on Windows after the flip, because `char16_t` and `wchar_t` are
layout-compatible there. They break only on macOS, and they break *inside* the funnel:
`WideCharFormatV` widens the format string, but the argument is a `char16_t*` in the `va_list`
where nothing can reach it. Every cheap fix is wrong on one platform — `%hs` mangles non-ASCII on
MSVC; widening at 42 call sites is exactly the scattering this task forbids; making the funnel
parse the format and consume the `va_list` is correct and platform-neutral and is a hand-written
mini-printf. That becomes its own task once the options are written up.

## Scope correction, 2026-09-22: B1 reaches WW3D2

The survey said WWVegas' wide strings are "self-contained, and not reached by `WideChar`". That is
true of `wwstring.h` and `widestring.h`, **which is all it opened.** It is false of WW3D2 — and
WW3D2 is where the engine draws every character it displays.

Three WW3D2 headers carry a `WCHAR` interface — `render2dsentence.h`, `render2d.h`, `font3d.h` —
and the engine reaches them at five sites:

| Site | Call |
|:--|:--|
| `W3DDisplayString.cpp:200` | `Build_Sentence( getText().str(), ... )` |
| `W3DDisplayString.cpp:286` | `font->Get_Char_Spacing( ch )` — and `:281` declares `WideChar ch;` |
| `W3DDisplayString.cpp:369` | `Get_Formatted_Text_Extents( getText().str() )` |
| `W3DGameWindow.cpp:549` | `Build_Sentence( m_instData.getText().str(), ... )` |
| `W3DGameWindow.cpp:586` | `Get_Text_Extents( m_instData.getText().str() )` |

`getText()` returns a `UnicodeString`, so all five hand a `const WideChar*` to a `const WCHAR*`.
They compile today only because both are `wchar_t`. After the flip: five hard errors.

**Retype `WCHAR` to `WideChar` in those three headers** rather than converting at the boundary —
converting means a `wchar_t` buffer allocated per string per frame in the text renderer.
`FontCharsClass` then follows down to `Store_GDI_Char( WCHAR ch )`, whose Windows-only GDI
rasterisation is D-track's, not B1's.

**`WideStringClass` is NOT on this path, and an earlier claim that it was is withdrawn.** It
appears in zero GameEngine files; `INIClass::Get_Wide_String` has no callers; `Xfer.cpp:209` xfers
a `UnicodeString`. Its width reaches no file format and no checksum. The two wide-string classes
are easy to conflate and only one of them is on the lockstep path.

**D-track overlap, decided:** `render2d.cpp` has 29 `DX8Wrapper::` sites and
`render2dsentence.cpp` has 2, so D1's later PRs will touch these files. B1 goes first — D1's PRs
2–8 need a Windows machine for their pixel comparison and are stalled after PR1, so there is no
live race. Whoever resumes D1 rebases onto the retype.

## Do

1. `typedef char16_t WideChar`. `char16_t` is exactly 2 bytes on every conforming compiler, which
   is what the file formats have always assumed and what MSVC happened to give them.
2. `L"..."` becomes `u"..."`. Mechanical, and a script can do it, but **read the diff** — a
   handful of these are passed to Win32 `W` APIs that genuinely want `wchar_t`, and those need a
   conversion at the boundary rather than a retyped literal. `MessageBoxW`, `CreateFileW` and the
   registry calls are where to look.
3. The `wcs*` calls have no `char16_t` equivalent in the C library. Write them — a small
   `Libraries/Include/Lib/WideCharFns.h` with `WideChar`-typed `wcslen`/`wcscmp`/`wcsncpy` and the
   rest. They are a few lines each and being able to read them is worth more than being clever. On
   Windows they can forward to the CRT with a cast; on Mac they are the implementation.
4. `_vsnwprintf` (`UnicodeString.cpp:312,323`, `InGameUI.cpp:3453` and neighbours) has no portable
   `char16_t` form at all. Format into a `wchar_t` or UTF-8 buffer and narrow to `char16_t` on the
   way out, in one place, behind one function. Do not scatter this.
5. Where a Win32 `W` API is genuinely being called, convert at the call. `char16_t` and `wchar_t`
   are layout-compatible on Windows, so a `reinterpret_cast` is honest there and only there —
   comment each one with which API wants it.

## Done when

- `sizeof(WideChar) == 2` is asserted at compile time, once, in `BaseType.h`, next to the typedef,
  with a comment naming `XferCRC.cpp` as the reason.
- A new test in `Tests/test_gameengine.cpp` builds a `UnicodeString` with non-ASCII content (the
  Turkish localisation is 3,853 lines of it and is right there in `Data/Turkish`), round-trips it
  through `XferSave`/`XferLoad`, and checks the `XferCRC` value against a committed constant. That
  constant is the thing that proves a Mac build and a Windows build agree.
- A `.csf` loads and a known string compares equal, on both platforms.
- Windows: full build, full `ctest`, **and `replay-check.ps1` passes**. A moved replay checksum
  here means the typedef changed a CRC, which is precisely what this task exists to prevent.

## Do not

- Do not use `std::u16string`. `UnicodeString` is a refcounted class with its own allocator
  discipline and `MAX_UITEXT_LENGTH` buffers all over the UI; swapping the container is a
  different, larger change and not this one.
- Do not "fix" the encoding while you are here. The `.csf` files are UTF-16 and stay UTF-16.
  Surrogate pairs, normalisation and whatever the Turkish text does with them are out of scope —
  this task changes a width, not a text model.
- Do not let a script land unreviewed. 724 literals is small enough to read.

## Notes

Do this early and alone. It touches the string class, it touches the checksum, and anything merged
on top of it will conflict. It is also the task most likely to be finished and then found wanting
three weeks later at E1, so its test is the deliverable as much as the change is.
