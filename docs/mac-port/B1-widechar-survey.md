# B1 — survey of the wide-character conversion surface

Reconnaissance for [B1](tasks/B1-widechar-char16.md). **No code is changed by this document.** It
exists so that B1 proper — which has to run alone, because it rewrites `UnicodeString` and anything
merged on top of it conflicts — can be done in one sitting by someone who already knows where every
site is.

Measured against `feature/mac-port` at `4b08cb58`, `GeneralsMD/Code` only. Every count below was
produced by reading the tree, and the method is given where the number disagrees with the task file.

No Windows machine was available. Nothing here was compiled by MSVC. Where this document says MSVC
and clang disagree, that is read off the language rules and the source, not off a build.

---

## 0. Summary — what the task file gets right, and the five things it misses

The task file's four named dangers are all real and all confirmed. What the survey adds:

| # | Finding | Where |
|:--|:--|:--|
| 1 | **The `L"` count is wrong.** 724 is a naive `grep -o 'L"'`, which also counts every narrow string ending in a capital L (`"ALL"`, `"CONTROL"`). The real figure is **536** wide string literals in 80 files. | §1 |
| 2 | **1,021 `L'x'` wide *character* literals exist and are unmentioned.** 919 are in `Keyboard.cpp` alone, assigned to `WideChar` fields. Unlike `L"…"`, these **convert silently** — the compiler will not find them for you. | §1.3 |
| 3 | **Formatting is three problems, not one.** `_vsnwprintf` is the one the task names. `swscanf` and `fwprintf` have the same no-`char16_t`-form problem, and **138 `%ls`/`%ws`/`%S` specifiers across 53 files** feed `WideChar*` to narrow `printf`, which is the failure that compiles clean on both platforms and prints garbage on one. | §3 |
| 4 | **Four `sizeof(WideChar)` arithmetic sites are missing from the PM's list**, including `Xfer.cpp:209` (the base-class xfer, sibling to the `XferCRC` line) and `NetPacket.cpp:365/5109` (the **network command wire format**). | §4.1 |
| 5 | **A whole second class of width arithmetic does not say `sizeof(WideChar)` at all** and no grep for it will find it: `len*2`, `sizeof(UnsignedShort)`, `result/2`, and `sizeof(LANMessage)`. Nine sites, six of them on the network wire. | §4.2 |

And one piece of good news: §5 shows the Win32 `W`-API boundary is **eleven call sites in seven
files**, not the diffuse problem the task file's "a handful of these, read the diff" implies. They
are listed exhaustively below, so the `L"` → `u"` sweep can be run over everything else.

---

## 1. The literals

### 1.1 Method and counts

`L"` preceded by an identifier character is not a wide literal. Counting with that exclusion, over
`*.cpp` and `*.h` under `GameEngine`, `GameEngineDevice` and `Main`:

| | Wide string literals `L"…"` | Files |
|:--|--:|--:|
| `GameEngine` | 451 | 73 |
| `GameEngineDevice` | 84 | 6 |
| `Main` | 1 | 1 |
| **Total** | **536** | **80** |

Files naming `WideChar`: **57** in those three trees (61 including `Libraries` and `Tests`). The
task file says 48; the tree has grown since.

Densest files, which is where the review time goes:

```
  72  GameEngine/Source/GameClient/MessageStream/CommandXlat.cpp
  63  GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp
  20  GameEngine/Source/GameClient/InGameUI.cpp
  19  .../GUICallbacks/Menus/WOLBuddyOverlay.cpp
  17  .../GUICallbacks/Menus/PopupPlayerInfo.cpp
  17  .../GUICallbacks/Menus/WOLQuickMatchMenu.cpp
  17  .../GUICallbacks/Menus/ScoreScreen.cpp
  14  .../GUICallbacks/ControlBarPopupDescription.cpp
```

All 536 are pure ASCII. **No wide string literal in the tree contains a non-ASCII byte or a
`\u`/`\x` escape**, so the sweep cannot change any character's value — only the literal's type.

### 1.2 The ten that must stay `L"…"`

These are the whole of the "handful" the task file warns about. Everything not in this table
becomes `u"…"`.

| Site | Literal | Why it stays |
|:--|:--|:--|
| `Main/WinMain.cpp:1242` | `L"-multiInstance"` | `findEarlyCommandLineOption(const wchar_t*)` — reads `GetCommandLineW()` |
| `Common/System/Debug.cpp:168` | `L"-headless"` | same |
| `Common/System/Debug.cpp:403` | `L"-logPrefix"` | same |
| `Common/System/JobSystem.cpp:113` | `L"-jobthreads"` | same |
| `Common/Audio/urllaunch.cpp:22` | `L"file://"` | `#define FILE_PREFIX`, consumed by `wcscpy`/`wcslen` on `LPWSTR` |
| `Common/Audio/urllaunch.cpp:36` | `L"://"` | `wcsstr(LPWSTR, …)` |
| `Common/Audio/urllaunch.cpp:46,90` | `L" #$%&\\+,;=@[]^{}"` | `wcspbrk(LPWSTR, …)` |
| `Common/Audio/urllaunch.cpp:116` | `L"%%%02x"` | `swprintf` into `WCHAR*` |
| `Common/Audio/simpleplayer.cpp:227,228,229` | `L"\\\\"`, `L":\\"`, `L"://"` | `wcsstr(LPCWSTR, …)` |
| `Common/Audio/simpleplayer.cpp:592` | `L"%s&filename=%s&embedded=false"` | `swprintf` into `WCHAR[]` |

Fourteen literals on ten lines. Note the shape of it: **it is not scattered, it is three files plus
one header's four callers.**

- `GameEngine/Include/Common/EarlyCommandLine.h` is `wchar_t` from top to bottom on purpose — its own
  header comment explains that `GetCommandLineW` is the only intact copy of the command line by the
  time `WinMain` has tokenized `lpCmdLine` in place. It takes `const wchar_t*`, so its four callers'
  literals stay `L"`. **Do not retype this header.** It is Windows-only and C1 will replace it on the
  Mac side, not port it.
- `urllaunch.cpp` and `simpleplayer.cpp` (both in `CMakeLists.txt:500-501`, so both really are built)
  are Windows Media / shell-association code written against `LPWSTR`, `LPCWSTR` and `WCHAR`. They
  never mention `WideChar` and never touch a `UnicodeString`. They are the reason for the hard
  constraint in §2.2: **the shims must not be macros over the `wcs*` names**, or these two files
  break.

### 1.3 The 1,021 wide *character* literals the task file does not count

`L'x'` has type `wchar_t`. Assigning it to a `char16_t` is an integral conversion: it **compiles,
with no diagnostic**, for every value that fits in 16 bits — and §1.1 established that every value
in this tree does. So unlike `L"…"`, these will not be found for you.

```
 919  GameEngine/Source/GameClient/Input/Keyboard.cpp
  28  GameEngine/Source/GameClient/LanguageFilter.cpp
  18  GameEngine/Source/GameNetwork/NetworkUtil.cpp
  17  GameEngine/Source/GameNetwork/GameSpyChat.cpp
   9  .../GUI/Gadget/GadgetTextEntry.cpp
   5  .../Menus/WOLLoginMenu.cpp,  5  .../ControlBar/ControlBar.cpp
   4  .../GameSpy/Thread/ThreadUtils.cpp
   3  .../Menus/LanLobbyMenu.cpp,  2  GameClient/GameText.cpp
   1 each: EarlyCommandLine.h, Recorder.cpp, InGameUI.cpp, GameConsole.cpp, HotKey.cpp,
           ControlBarCallback.cpp, ReplayMenu.cpp, LANAPICallbacks.cpp, LobbyUtils.cpp,
           W3DTankDraw.cpp, W3DTankTruckDraw.cpp
```

`Keyboard.cpp`'s 919 are the keyboard layout tables — `_set_keyname_(L'4', L'$', …)` into
`KeyboardIO`'s `WideChar stdKey / shifted / shifted2` (`GameClient/Keyboard.h:160`). They should all
become `u'…'`, mechanically, for the same reason the strings do. `EarlyCommandLine.h:63`'s single
`L'-'` **stays**, per §1.2.

**Two `L'…'` sites want a human, not a script:**

- **`Keyboard.cpp:467`** is `_set_keyname_(L'4', L'$', L'�', KEY_4)` — the third argument is
  literally U+FFFD REPLACEMENT CHARACTER, three raw UTF-8 bytes `EF BF BD` in the file. Read against
  line 466 (`L'3'`, `0x00A3` = `£`) this is the UK layout, and the lost character is almost certainly
  `€`. It is already broken, and **it is already broken differently on each compiler**: `CMakeLists.txt`
  passes no `/utf-8`, so MSVC reads this UTF-8 file in the system ANSI codepage and sees a
  three-character multicharacter literal (C4066), while clang sees U+FFFD. Fix it to `u'€'` in
  B1 and say so, or leave it and write it down — but do not let a script turn it into `u'�'`
  and call that a port.
- **`NetworkUtil.cpp:451-463`** is the player-name sanitizer, and its literals are deliberate Unicode
  boundaries: `L'\x007f'`, `L'\x009f'`, `L'\x2028'`, `L'\x2029'`, **`L'\xd800'`–`L'\xdfff'`**,
  `L'\xa0'`, `L'\x1680'`, `L'\x2000'`–`L'\x200a'`, `L'\x202f'`, `L'\x205f'`, `L'\x3000'`. Every value
  fits in 16 bits, so `char16_t` holds them all — but the surrogate range test at line 453 is code
  whose *meaning* is "this is UTF-16". It is an argument for the typedef, and it should be read
  rather than rewritten. Confirmed: **no wide literal anywhere in the tree escapes above `0xFFFF`.**

---

## 2. The wide CRT surface, and `WideCharFns.h`

### 2.1 Census

Every wide-CRT call in `GameEngine` + `GameEngineDevice` + `Main`, with the split that matters:
**W** = operates on `WideChar`, needs a shim; **w** = operates on `wchar_t`/`LPWSTR`, must keep
calling the CRT unchanged.

| Function | n | W | w | Locations (w-column sites named) |
|:--|--:|--:|--:|:--|
| `wcsncpy` | 24 | 23 | 1 | LANAPI ×11, LANAPIhandlers ×4, BuddyThread ×2, WOLBuddyOverlay ×2, ScoreScreen; *w:* urllaunch:108 |
| `wcslen` | 14 | 10 | 4 | UnicodeString.cpp ×6, UnicodeString.h:319, LanguageFilter:63, ThreadUtils:68; *w:* EarlyCommandLine:42, urllaunch:61/65/85, simpleplayer:250 |
| `iswspace` | 14 | 10 | 4 | UnicodeString.cpp ×2, GameText ×4, LanLobbyMenu ×2, PopupHostGame, WOLLoginMenu; *w:* EarlyCommandLine ×4 |
| `wcscpy` | 10 | 7 | 3 | UnicodeString.cpp ×3, GameText:843, LanguageFilter:92, LANAPI:957/960; *w:* urllaunch:84/97, simpleplayer:258 |
| `wcscmp` | 9 | 9 | 0 | UnicodeString.h ×8 (the whole comparison operator set), WOLBuddyOverlay:517 |
| `wcsstr` | 6 | 2 | 4 | ThingFactory:545, LanguageFilter:98; *w:* urllaunch:36, simpleplayer:227/228/229 |
| `MultiByteToWideChar` | 6 | 5 | 1 | *see §5* — IMEManager ×5 write into `WideChar[]`; ThreadUtils:38 |
| `fwprintf` | 5 | 5 | 0 | Recorder:590/602/604/674/677 — **§3.3** |
| `_vsnwprintf` | 5 | 5 | 0 | UnicodeString.cpp:312/323, InGameUI:3453/3478/3503 — **§3.1** |
| `_wcsicmp` | 4 | 4 | 0 | UnicodeString.h:261/265/407/414 |
| `wcschr` | 4 | 4 | 0 | WOLLoginMenu:947, ControlBar:4854, ThreadUtils:42/51 |
| `wsprintf` | 4 | 0 | 0 | **None are wide.** StackDump:560 formats into `char[512]`; urllaunch:163/260/264 are `_T()`/TCHAR. This is `wsprintfA`. The task file's "`wsprintf` 6" should be struck from the scope. |
| `WideCharToMultiByte` | 3 | 2 | 1 | ThreadUtils:68/72; *w:* EarlyOptions.h:79 (`PWSTR` from `SHGetKnownFolderPath`) |
| `wcscat` | 2 | 2 | 0 | UnicodeString.cpp:92/118 |
| `wcsicmp` | 2 | 2 | 0 | LobbyUtils:505/508 |
| `swscanf` | 2 | 2 | 0 | NetworkDirectConnect:122/150 — **§3.2** |
| `wcspbrk` | 2 | 0 | 2 | *w:* urllaunch:46/90 |
| `swprintf` | 2 | 0 | 2 | *w:* urllaunch:116, simpleplayer:592 |
| `wcsspn` | 1 | 1 | 0 | UnicodeString.cpp:340 |
| `wcscspn` | 1 | 1 | 0 | UnicodeString.cpp:343 |
| `wcsrchr` | 1 | 1 | 0 | LanguageFilter:154 |
| `towupper` | 1 | 1 | 0 | ControlBar:4857 |
| `_wcsnicmp` | 1 | 0 | 1 | *w:* EarlyCommandLine:46 |

Not found in these trees at all, despite being in the task file's list: `wcstok`, `wcsncmp`,
`wcslcpy`, `wcstrim`, `wcstol`, `wcstod` as *calls*. `wcslcpy` and `wcstrim` live in
`Libraries/Source/WWVegas/WWLib` (`stringex.h:90`, `trim.h:46`), are `wchar_t`-typed, and are used
only by `WWLib`'s own `widestring.h`/`readline.cpp`. **They are out of B1's scope** — leave them
`wchar_t`.

`wcstol`, `wcstod`, `wcstok`, `wcsncmp` and eleven more appear only as **macro definitions** — see
next.

### 2.2 `Language.h` already is the indirection layer

`GameEngine/Include/Common/Language.h:77-99` defines a `Game*` family over the wide CRT:

```
GameStrcpy→wcscpy   GameStrncpy→wcsncpy  GameStrlen→wcslen    GameStrcat→wcscat
GameStrcmp→wcscmp   GameStrncmp→wcsncmp  GameStricmp→wcsicmp  GameStrnicmp→wcsnicmp
GameStrtok→wcstok   GameSprintf→swprintf GameVsprintf→vswprintf
GameAtoi→wcstol     GameAtod→wcstod      GameItoa→_itow       GameSscanf→swscanf
GameStrstr→wcsstr   GameStrchr→wcschr    GameIsDigit→iswdigit GameIsAscii→iswascii
GameIsAlNum→iswalnum GameIsAlpha→iswalpha
```

Only five of them are actually called (`GameStrlen` at `IMEManager.cpp:1139`; `GameIsAlpha` at
`Keyboard.cpp:946`; `GameIsDigit`/`GameIsAscii`/`GameIsAlNum` at `GameWindowGlobal.cpp:199/209/219`),
but the other sixteen are a loaded gun: each is a `wchar_t` entry point one `#include` away from any
engine file, and several of them (`_itow`, `wcsicmp`, `wcsnicmp`) do not exist on macOS under any
spelling.

**Recommendation: `Language.h` is where `WideCharFns.h` should be wired in.** Repoint these twenty
macros at the new functions in one commit and the five live call sites port themselves; the sixteen
dead ones stop being a trap. Say so in the header, because the next person will otherwise add a
parallel set.

### 2.3 Proposed surface of `Libraries/Include/Lib/WideCharFns.h`

**The naming constraint is the important part of this design.** `urllaunch.cpp` and
`simpleplayer.cpp` call `wcsstr`, `wcscpy`, `wcsncpy`, `wcslen`, `wcspbrk` and `swprintf` on genuine
`LPWSTR`, in the same translation unit family as everything else. So:

- **No `#define wcslen …`.** A macro over a CRT name breaks those two files and does it at a
  distance, which is the worst way to find out.
- **No overloads on the `wcs*` names either.** On Windows `char16_t` and `wchar_t` are distinct
  types, so overloading is legal — but it puts the resolution of a Win32 call and an engine call on
  the same name, which is exactly the ambiguity this task exists to remove.
- **Distinct names.** Below they are spelled `WideChar*`; `Language.h`'s `Game*` macros can point at
  them so the existing call sites read unchanged.

```cpp
// Libraries/Include/Lib/WideCharFns.h
//
// WideChar is char16_t (see BaseType.h).  The C library has no char16_t string functions on any
// platform, and the wcs* family is wchar_t - 2 bytes on MSVC, 4 on clang - so calling it on engine
// text is a width bug waiting for a different compiler.  These are the engine's own.
//
// Deliberately NOT named wcs*, and deliberately not macros: Common/Audio/urllaunch.cpp and
// simpleplayer.cpp call the real CRT wcs* on real LPWSTRs from the Windows shell and Windows Media,
// and must keep doing so.  On Windows these forward to the CRT through a reinterpret_cast, which is
// honest because char16_t and wchar_t are layout-compatible there.  On macOS they are the
// implementation.
#pragma once
#include "Lib/BaseType.h"

// --- length, copy, concatenate -------------------------------------------------------
size_t    WideCharLen    ( const WideChar *s );
WideChar *WideCharCpy    ( WideChar *dst, const WideChar *src );
WideChar *WideCharNCpy   ( WideChar *dst, const WideChar *src, size_t n );
WideChar *WideCharCat    ( WideChar *dst, const WideChar *src );

// --- compare -------------------------------------------------------------------------
int  WideCharCmp    ( const WideChar *a, const WideChar *b );
int  WideCharNCmp   ( const WideChar *a, const WideChar *b, size_t n );
int  WideCharICmp   ( const WideChar *a, const WideChar *b );   // ASCII case folding only; see note
int  WideCharNICmp  ( const WideChar *a, const WideChar *b, size_t n );

// --- search --------------------------------------------------------------------------
const WideChar *WideCharChr  ( const WideChar *s, WideChar c );
const WideChar *WideCharRChr ( const WideChar *s, WideChar c );
const WideChar *WideCharStr  ( const WideChar *hay, const WideChar *needle );
size_t          WideCharSpn  ( const WideChar *s, const WideChar *accept );
size_t          WideCharCSpn ( const WideChar *s, const WideChar *reject );

// --- classification ------------------------------------------------------------------
// Take and return an Int, not a WideChar: the CRT's isw* family is wint_t-based and every one of
// these call sites already relies on that (ControlBar.cpp:4857 casts the result back).
Bool WideCharIsSpace ( Int c );
Bool WideCharIsDigit ( Int c );
Bool WideCharIsAlpha ( Int c );
Bool WideCharIsAlNum ( Int c );
Bool WideCharIsAscii ( Int c );
Int  WideCharToUpper ( Int c );

// --- formatting: see B1-widechar-survey.md section 3.  One funnel, declared here, defined once. --
Int WideCharFormatV ( WideChar *out, size_t outCount, const WideChar *format, va_list args );
```

Notes for whoever writes the bodies:

- **`WideCharICmp` must be defined as ASCII-only case folding, and commented as such.** `_wcsicmp`
  is locale-sensitive on MSVC. It is called from `UnicodeString::compareNoCase`
  (`UnicodeString.h:261/265/407/414`) and from the lobby game-name sort (`LobbyUtils.cpp:505/508`).
  Nothing in the simulation depends on it, but "the same two strings compare differently on a Mac"
  is the kind of thing that gets found at E1 and blamed on something else. Pin it.
- `WideCharNCpy` should keep `wcsncpy`'s exact semantics, pad-with-zeros and all: 23 of the 24 call
  sites are `wcsncpy(dst, src, N); dst[N] = 0;` on fixed-size wire and UI buffers and they rely on it.
- `WideCharSpn`/`WideCharCSpn` exist for `UnicodeString::nextToken` alone (`UnicodeString.cpp:340,343`)
  and must return `size_t`, since `nextToken` does pointer arithmetic with the result.
- Everything here is a loop over a `char16_t*`. Resist improving on that; the task file is right that
  being able to read them is worth more than being clever.

---

## 3. Formatting — three problems, not one

### 3.1 `_vsnwprintf` — the funnel the task file asks for

Five callers, exactly as the task file says, and no others in the tree:

| Site | Buffer |
|:--|:--|
| `Common/System/UnicodeString.cpp:312` | `WideChar buf[MAX_FORMAT_BUF_LEN]` — `format_va(const UnicodeString&, va_list)` |
| `Common/System/UnicodeString.cpp:323` | same — `format_va(const WideChar*, va_list)` |
| `GameClient/InGameUI.cpp:3453` | `WideChar buf[…]` — message from a string-manager string |
| `GameClient/InGameUI.cpp:3478` | same — `message(const WideChar* format, …)` |
| `GameClient/InGameUI.cpp:3503` | same — `messageColor(…)` |

All five are `_vsnwprintf(buf, sizeof(buf)/sizeof(WideChar)-1, fmt, args)` followed by a manual NUL.
**The single funnel is `WideCharFormatV` from §2.3.** All five become one line each.

Implementation shape, so it is decided once rather than argued twice:

> Widen the `char16_t` format string into a `wchar_t` stack buffer, call the platform's
> `vswprintf` (POSIX) / `_vsnwprintf` (MSVC), narrow the `wchar_t` result back to `char16_t`.
> On Windows both widen and narrow are `memcpy`-equivalent and the compiler will see that.
>
> The narrowing step is where a `>0xFFFF` character would be lost. The task file forbids fixing the
> text model here, and it is right to: `MAX_FORMAT_BUF_LEN` buffers all over the UI already truncate,
> and no `.csf` string or format literal in this tree is outside the BMP (§1.1). Substitute U+FFFD
> and leave a `DEBUG_CRASH`, so if it ever happens somebody hears about it.

Note that this path already has a behavioural difference to write down for Windows: MSVC's
`_vsnwprintf` returns negative on truncation and does not NUL-terminate; C99 `vswprintf` returns
negative on *encoding error* and returns the would-be length on truncation. The call sites
`throw ERROR_OUT_OF_MEMORY` on negative. Make `WideCharFormatV` present MSVC's contract on both
platforms, or three call sites change behaviour on the Mac and nobody will notice until a long
player name hits the UI.

### 3.2 `swscanf` — the same problem, unmentioned

`GUICallbacks/Menus/NetworkDirectConnect.cpp:122` and `:150`:

```cpp
Int numFields = swscanf(newIP.str(), L"%d.%d.%d.%d", &(n1[0]), …);
```

`newIP.str()` is `const WideChar*`. There is no `char16_t` `swscanf` either, and this one parses the
**direct-connect IP address**, so it is not cosmetic. It needs the same treatment and the same
funnel discipline: one `WideCharScanIPv4`-shaped helper, or widen-and-forward alongside
`WideCharFormatV`. Two call sites, one format, and the format is the same at both — this is the
cheapest of the three.

### 3.3 `fwprintf` — wide file output

`Common/Recorder.cpp:590, 602, 604, 674, 677` write the replay's text header:

```cpp
fwprintf(m_file, L"%ws", replayName.str());
```

`m_file` is a `FILE*` in wide orientation and the argument is a `WideChar*`. This needs the same
widen-on-the-way-out treatment. It is the **replay file header**, so B1's own acceptance test
(`replay-check.ps1`, and the committed-CRC test the task file asks for) runs straight through it.
Do this one first and the rest of B1 gets a working canary.

### 3.4 The 138 `%ls` / `%ws` / `%S` specifiers — the quiet one

This is the class that worries me most, because **it compiles clean on both platforms and is wrong
on exactly one of them**.

| | Sites | Files |
|:--|--:|--:|
| **Narrow** format strings (`printf`, `DEBUG_LOG`, `CRCDEBUG_LOG`, `AsciiString::format`) carrying `%ls`/`%ws`/`%S` with a `WideChar*` argument | **91** | 35 |
| **Wide** format strings carrying them | 42 | 17 |
| Total | 138 | 53 |

The narrow 91 are the problem. `printf("%ls", p)` tells the CRT that `p` is a `wchar_t*`. On Windows
with `WideChar == char16_t` that stays true by layout accident and the output is unchanged. On macOS
`wchar_t` is 4 bytes, so the CRT reads a `char16_t` buffer as UCS-4 and prints garbage, or walks off
the end. `PlayerList.cpp:341` even has a comment congratulating itself on the trick.

Concentrations:

```
 10  GameNetwork/NAT.cpp              7  GameLogic/System/GameLogic.cpp
  9  Common/Audio/simpleplayer.cpp    7  Common/Recorder.cpp
  6  GameSpy/Thread/PeerThread.cpp    4  Common/RTS/Player.cpp
  4  GameNetwork/ConnectionManager.cpp
```

`simpleplayer.cpp`'s nine are genuine `wchar_t` and stay. The rest do not.

Four of `Player.cpp`'s are `CRCDEBUG_LOG`, which is the **desync-diagnosis log**. A port that leaves
those printing garbage takes away the instrument you would use to debug the thing B1 exists to
prevent.

**This does not have to be solved in B1, but it has to be decided in B1.** Three options, in
increasing order of work: (a) leave them, accept that `DEBUG_LOG` of a player name is wrong on Mac,
and write it in `WINDOWS-DEBT.md`'s sibling; (b) mechanically wrap every `WideChar*` argument to a
narrow format in a `WideCharToUtf8()` scratch helper and change `%ls` to `%s`; (c) make `DEBUG_LOG`
and friends variadic templates that convert `WideChar*` arguments themselves. (b) is 91 edits and no
cleverness, and it is what I would recommend — it is the only one that leaves the CRC log readable.
Whichever is chosen, **say so in the task file**, because the grep that finds them is not obvious.

---

## 4. `sizeof(WideChar)` arithmetic

### 4.1 Verification of the PM's list — it is incomplete

The five sites given were `GameText.cpp:1056`, `DataChunk.cpp:368/981`, `XferSave.cpp:335`,
`XferLoad.cpp:232`, `XferCRC.cpp:355`. **All five are real and correctly characterised.** The
complete list of literal `sizeof(WideChar)` in `GameEngine` + `GameEngineDevice` + `Main` + `Libraries`
is 22 occurrences; classified:

**Width-carrying — these decide how many bytes cross a file, a socket or a checksum:**

| Site | What it is | On PM's list? |
|:--|:--|:--:|
| `GameClient/GameText.cpp:1056` | `.csf` string read | yes |
| `Common/System/DataChunk.cpp:368` | map/scenario chunk **write** | yes |
| `Common/System/DataChunk.cpp:981` | map/scenario chunk **read** | yes |
| `Common/System/DataChunk.cpp:982` | `decrementDataLeft(len*sizeof(WideChar))` — the read cursor | **no** |
| `Common/System/XferSave.cpp:335` | save game write | yes |
| `Common/System/XferLoad.cpp:232` | save game read | yes |
| **`Common/System/XferCRC.cpp:355`** | **replay / network checksum** | yes |
| **`Common/System/Xfer.cpp:209`** | **`Xfer::xferUnicodeString` — the base implementation the three above override** | **no** |
| **`GameNetwork/NetPacket.cpp:365`** | **`ARGUMENTDATATYPE_WIDECHAR` size in a command packet** | **no** |
| **`GameNetwork/NetPacket.cpp:5109`** | **the same, in the second packet builder** | **no** |

`DataChunk.cpp:982` is cosmetic next to its sibling on 981 but it is a second arithmetic site on the
same value and it would be easy to change one and not the other.

**`Xfer.cpp:209` is the real miss.** It is `xferImplementation( unicodeStringData->str(), sizeof(WideChar) * getLength() )` in the
base class — the same expression as `XferCRC.cpp:355`, one level up, reached by every `Xfer`
subclass that does not override it.

**`NetPacket.cpp:365` and `:5109` are the more dangerous miss.** They size the `WideChar` argument of
a `GameMessage` in the command packet — which is to say, in the **lockstep command stream**, the
thing the whole port is being kept compatible for. The matching read is `NetPacket.cpp:5535-5542`
(`WideChar c; memcpy(&c, data+i, sizeof(c)); i += sizeof(c);`), which is self-consistent and so is
safe, but the sizing at 365/5109 must agree with the far machine's. This belongs in the same
sentence as `XferCRC.cpp:355` in the task file, and is not there.

**Self-consistent ratios — safe under the width change, listed so nobody churns them:**

`UnicodeString.cpp:96,103` (allocation, `numChars*sizeof(WideChar)` and its inverse — these are the
"string class changes shape" sites the task file names, and they are correct at any width);
`UnicodeString.cpp:312,323` and `InGameUI.cpp:3453,3454,3455,3478,3479,3480,3503,3504,3505`
(`sizeof(buf)/sizeof(WideChar)` where `buf` is a `WideChar[]` — an element count, correct at any
width). Twelve sites. **Leave them alone**; they are noise in the diff and every line of noise is a
line the reviewer does not spend on `Xfer.cpp:209`.

### 4.2 The class that does not say `sizeof(WideChar)` — nine more sites

**No grep for `sizeof(WideChar)` finds any of these.** Each is width arithmetic expressed some other
way, and each works today only because `sizeof(wchar_t) == sizeof(UnsignedShort) == 2` under MSVC.
The typedef change makes them correct again — but they are correct by coincidence, they are mostly
on the wire, and they are the sites that would silently halve a message on a clang build that had
*not* done B1.

| Site | Expression | What it carries |
|:--|:--|:--|
| `Common/System/UnicodeString.cpp:350` | `memcpy(tmp, start, len*2)` | **`nextToken`** — a bare magic `2` in the string class itself |
| `GameNetwork/NetPacket.cpp:566` | `msglen += textmsglen * sizeof(UnsignedShort)` | disconnect-chat packet length |
| `GameNetwork/NetPacket.cpp:603` | same | chat packet length |
| `GameNetwork/NetPacket.cpp:1483,1484` | `memcpy(buffer+offset, unitext.str(), length * sizeof(UnsignedShort))` and the matching `offset +=` | chat text **write** |
| `GameNetwork/NetPacket.cpp:1585,1586` | same | chat text write |
| `GameNetwork/NetPacket.cpp:3463,3464` | same | chat text write |
| `GameNetwork/NetPacket.cpp:3577,3578` | same | chat text write |
| `GameNetwork/NetPacket.cpp:5767,5768` | `memcpy(text, data+i, length*sizeof(UnsignedShort))` into `WideChar text[256]` | disconnect-chat **read** |
| `GameNetwork/NetPacket.cpp:5794,5795` | same | chat **read** |
| `GameClient/GUI/IMEManager.cpp:1116` | `m_compositionStringLength = result/2` | IMM32 byte count → char count |
| `GameClient/GUI/IMEManager.cpp:1123,1189` | `MAX_COMPSTRINGLEN*2` | char count → IMM32 byte count |
| `Common/System/QuotedPrintable.cpp:176` | `UnicodeString out((const WideChar*)dest)` over an `unsigned char[]` filled two bytes at a time | quoted-printable decode |
| `GameClient/LanguageFilter.cpp:169,178` | `file1->read(&c, sizeof(UnsignedShort)); buf[index] = c;` | the bad-word file, a UTF-16 file format |

`NetPacket.cpp` is the notable one: **it uses both idioms for the same field.** Lines 365 and 5109
size a `WideChar` with `sizeof(WideChar)`; lines 566, 603, 1483, 1585, 3463, 3577 and the two reads at 5767 and 5794
size wide text with `sizeof(UnsignedShort)`. Its own comments at 5760-5762 and 5786-5788 explain the
history — VC6 typedef'd `wchar_t` to `unsigned short`, so `UnicodeString::set` took the buffer
directly — and note that the byte counts "still use `sizeof(UnsignedShort)`, which is the same two
bytes". That comment is exactly right and exactly the problem: it is true only for as long as
`WideChar` is two bytes, and B1 is the commit that makes it true *by construction* instead of by
luck. **Normalising these twelve to `sizeof(WideChar)` is the highest-value cleanup in B1 after the
typedef itself**, and it is a no-op on Windows.

### 4.3 `LANMessage` — a whole wire struct, and the tripwire that saves it

`GameNetwork/LANAPI.h:307-434` is `#pragma pack`'d and contains ten `WideChar[N]` arrays (the player
name, per-slot names, game names, chat). It goes on the wire raw:

```cpp
// LANAPI.cpp:189, 199, 206
m_transport->queueSend(ip, lobbyPort, (unsigned char *)msg, sizeof(LANMessage));
```

`sizeof(LANMessage)` is `sizeof(WideChar)` arithmetic by another name, on the LAN protocol, and
`LANAPI.h:50` computes the options budget with a hand-written `*2`:

```cpp
static const Int m_lanMaxOptionsLength = MAX_LANAPI_PACKET_SIZE
    - ( 8 + (g_lanGameNameLength+1)*2 + 4 + (g_lanPlayerNameLength+1)*2 + … );
```

**The good news, and it is worth knowing before B1 starts:** `LANAPI.h:436` already carries

```cpp
static_assert(sizeof(LANMessage) <= MAX_LANAPI_PACKET_SIZE,
    "LANMessage must fit in a single LAN datagram");
```

At a 4-byte `WideChar` the struct roughly doubles and **this assert fires at compile time**. It is
the one width-sensitive wire format in the tree that refuses to be got wrong quietly, and it is the
closest thing B1 has to a pre-existing test. Leave the `*2` on line 50 alone or replace it with
`sizeof(WideChar)`, but either way do not weaken that assert — and mention it in the B1 PR, because
it is evidence.

---

## 5. The Win32 `W`-API boundary — the complete list

Eleven call sites, seven files. Every one of them is a place where a `WideChar*` (or a `WideChar`
buffer) meets a Windows `W` entry point, and the task file's rule applies: `reinterpret_cast` at the
call, commented with which API wants it, and **only** here.

| Site | API | Direction | Note |
|:--|:--|:--|:--|
| `Common/System/Debug.cpp:886` | `MessageBoxW(NULL, mesg.str(), prompt.str(), …)` | WideChar → Win32 | the crash box |
| `Win32Device/Common/Win32OSDisplay.cpp:110` | `MessageBoxW(NULL, mesgStr.str(), promptStr.str(), …)` | WideChar → Win32 | the in-game OS dialog |
| `GameClient/GameText.cpp:465` | `SetWindowTextW(ApplicationHWnd, ourName.str())` | WideChar → Win32 | window title |
| `SaveGame/GameState.cpp:240` | `GetDateFormatW(…, wchar_t dateBuffer, …)` then `displayDateBuffer.set(dateBuffer)` | Win32 → WideChar | **`.set()` takes `const WideChar*`; this becomes a hard type error.** Good — the compiler finds it. |
| `SaveGame/GameState.cpp:273` | `GetTimeFormatW(…)` then `.set(timeBuffer)` | Win32 → WideChar | same |
| `GUICallbacks/Menus/ReplayMenu.cpp:680` | `FormatMessageW(…, wchar_t buffer, …)` then `errorStr.set(buffer)` | Win32 → WideChar | same |
| `GUICallbacks/Menus/PopupReplay.cpp:291` | `FormatMessageW(…)` then `.set(buffer)` | Win32 → WideChar | same |
| `GUICallbacks/Menus/PopupReplay.cpp:316` | `FormatMessageW(…)` then `.set(buffer)` | Win32 → WideChar | same |
| `GUI/IMEManager.cpp:1112` | `ImmGetCompositionStringW(m_context, GCS_COMPSTR, m_compositionString, …)` | Win32 → WideChar | **takes `LPVOID`. No type error. Silent.** |
| `GUI/IMEManager.cpp:1178` | `ImmGetCompositionStringW(…, m_resultsString, …)` | Win32 → WideChar | **silent, same reason** |
| `GUI/IMEManager.cpp:1398`→`1452` | `ImmGetCandidateListW(…)` then `m_candidateString[i].set((WideChar*)string)` | Win32 → WideChar | already a cast; it just needs the right one and a comment |

Plus the `MultiByteToWideChar` / `WideCharToMultiByte` sites that write into or read from `WideChar`
buffers, which are the same boundary in narrow clothing:

| Site | Note |
|:--|:--|
| `IMEManager.cpp:1065, 1129, 1195, 1230` | `MultiByteToWideChar(…, WideChar *dest, …)` — `LPWSTR` parameter, **hard type error, the compiler finds it** |
| `GameSpy/Thread/ThreadUtils.cpp:38, 68, 72` | `MultiByteToWideCharSingleLine` / `WideCharStringToMultiByte` — see below |

**Two of these deserve more than a cast:**

- **`IMEManager.cpp` is the dangerous file**, because `ImmGetCompositionStringW` and
  `ImmGetCandidateListW` take `LPVOID`. Nothing in this file's Win32→`WideChar` direction will
  produce a diagnostic. It is also the one file where the `/2` and `*2` of §4.2 live. Read it
  line by line; do not trust the build.
- **`ThreadUtils.cpp:33-78` mixes `WideChar` and `std::wstring` in one function.**
  `MultiByteToWideCharSingleLine` allocates `WideChar *dest`, fills it via `MultiByteToWideChar`,
  walks it with `wcschr`, and then does `std::wstring ret = dest;`. That last line is a hard error
  at `char16_t` and cannot be fixed by a cast at the boundary — the *return type* is wrong. Its
  callers want the GameSpy thread's text; either the function returns `std::u16string`, or it keeps
  `std::wstring` and converts once on the way out. **This is the only site in the survey where B1
  has to change a signature rather than a cast**, so decide it before the sweep rather than during.

All eleven `W`-API sites are inside `#ifdef`-able Windows code or in files C1/C3 will replace. None
of them is in `GameLogic`. **The simulation does not touch a Win32 wide API anywhere.**

---

## 6. Suggested order for the solo run

Not a plan — B1's own file has that — but the dependency order the survey suggests, so the build is
never broken for longer than one step:

1. `Language.h`'s twenty macros → `WideCharFns.h` (§2.2, §2.3). Nothing changes yet; the layer exists.
2. `WideCharFormatV` + the `swscanf` and `fwprintf` helpers (§3.1-3.3). Still `wchar_t` underneath.
3. **`typedef char16_t WideChar`** + the `static_assert(sizeof(WideChar) == 2)` the task file asks
   for, naming `XferCRC.cpp`. Now the compiler produces the worklist: every `L"…"` in a `WideChar`
   context, every `.set(wchar_t*)`, `ThreadUtils`' `std::wstring`.
4. Work that list. The ten literals in §1.2 are the ones that stay; everything else the compiler
   names becomes `u"…"`.
5. The 1,021 `L'…'` (§1.3) — the compiler will **not** name these. Sweep them separately, by file,
   with `Keyboard.cpp:467` read by a human.
6. Normalise §4.2's ten `sizeof(UnsignedShort)` / `len*2` sites to `sizeof(WideChar)`. No-op on
   Windows, and it is what makes §4.1's list complete for the next person.
7. The 91 narrow `%ls` (§3.4), whichever option §3.4 decides.

Steps 1, 2 and 6 are no-ops on Windows by construction and can be reviewed as such. Step 3 is the
one that needs `replay-check.ps1`.

---

## 7. What this survey did not establish

- **Nothing here was compiled.** Not by MSVC, not by clang. Every "this is a hard type error" and
  "this is silent" claim is read off the language rules and the declarations in this tree. The first
  clang build of step 3 above is what turns them into facts, and it may well find sites this missed —
  particularly in templates and macros, which a grep does not expand.
- **`Generals/Code` was not surveyed.** The README puts it out of scope and it has no `CMakeLists.txt`.
  It has its own copy of `BaseType.h` with the same typedef. If it is ever built, it needs its own B1.
- **`Libraries/Source/WWVegas`** was surveyed only far enough to establish that `wcstrim`, `wcslcpy`
  and `widestring.h` are `wchar_t`-typed, self-contained, and not reached by `WideChar`. That is
  enough for B1 to leave them alone; it is not enough to say they are portable, which is B6's problem.
- **`Tests/test_gameengine.cpp`** has one `wchar_t` literal (line 8551, a command-line parsing test)
  and no `UnicodeString` round-trip coverage of the kind B1's "done when" requires. The test the task
  file asks for does not exist yet in any form to build on.
