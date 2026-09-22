# The `%ls` and `%S` in wide format strings

The companion to the narrow `%ls` sweep, which is done. This is the other half: the conversions
inside a **wide** format string, which the sweep deliberately left alone because they are a
different problem with no cheap correct answer.

Written to answer one question the PM asked: **could these simply be removed rather than fixed?**

**Yes. Almost all of them, and the two that cannot be removed do not need removing.** The
hand-written mini-printf this was heading towards is not necessary. Details below, because the
obvious simplification is wrong at two of the sites and getting that wrong would be worse than
leaving the whole thing alone.

---

## The real numbers

The survey said 42. That count included commented-out code. Live, in `GameEngine`,
`GameEngineDevice` and `Main`:

| | Formats | Specifiers |
|:--|--:|--:|
| Wide format strings carrying `%ls`, `%ws` or `%S` | 26 | 34 |
| less the two in `WideCharFns.cpp` itself, which are the funnel and correct by construction | 24 | 32 |

And those 32 are **two unrelated problems**, which is the first thing the survey got wrong by
counting them together:

| | Specifiers | What the argument is | Why it is a problem |
|:--|--:|:--|:--|
| `%ls` / `%ws` in a wide format | 25 | `WideChar*` | The width. Correct on MSVC today and after the flip, since `char16_t` and `wchar_t` are layout-compatible there. Broken on macOS, and broken *inside* `WideCharFormatV`: that funnel widens the **format** to `wchar_t` but the **argument** is still a `char16_t*` sitting in the `va_list` where nothing can reach it. |
| `%S` in a wide format | 7 | `char*` | Not a width problem at all. `%S` inside a wide format means "narrow string" on MSVC, and every one of these really is passed an `AsciiString::str()` or a `const char*`. On POSIX the conversion runs through `mbrtowc`, so it is locale-dependent in the same way `vswprintf` was, and the engine's own bytes are not UTF-8. |

Verified argument types for all seven `%S`: `GameState.cpp:1219` is `AsciiString mapLabel`
(`GameState.h:105`); `PopupSaveLoad.cpp:465,467` are `const char *` from
`AsciiString::reverseFind`; `W3DDisplay.cpp:1689,1783,1832` are `ThingTemplate::getName().str()`
and `ModelConditionFlags::getBitNames()`, which is `static const char**`.

---

## The 25 `%ls`, by shape

### Seven are an assignment written as a printf

```cpp
mapDisplayName.format(L"%ls", mapData->m_displayName.str());
```

`LanGameOptionsMenu.cpp:350`, `:1043`; `WOLGameSetupMenu.cpp:905`, `:2208`, `:2356`;
`LANAPI.cpp:780`; `LANAPICallbacks.cpp:161`. Seven copies of the same line, in six files, and every
one of them means

```cpp
mapDisplayName = mapData->m_displayName;
```

No format string, no `va_list`, no funnel, nothing to port. This is the largest single group and it
disappears.

### Seven are a concatenation written as a printf

```cpp
unitext.format(L"[%ls] %ls", name.str(), msg->getText().str());   // ConnectionManager.cpp:676, :698
fullMsg.format( L"%ls %ls", name.str(), msg.str() );              // Chat.cpp:301, GameSpyChat.cpp:431
fullMsg.format( L"[%ls] %ls", name.str(), msg.str() );            // Chat.cpp:305, GameSpyChat.cpp:435
tmp.format(L"\n%ls %ls", TheGameText->fetch(sideName).str(), TheGameText->fetch(rankName).str());
                                                                  // WOLLobbyMenu.cpp:331
```

Fourteen specifiers. `UnicodeString` already has `concat` for both a `UnicodeString` and a
`WideChar*`, so each is three or four straight-line calls with no format string. These disappear too.

### Two must be left exactly as they are

```cpp
TheInGameUI->message(UnicodeString(L"%ls"), unitext.str());              // ConnectionManager.cpp:706
TheInGameUI->messageColor(&rgb, UnicodeString(L"%ls"), unitext.str());  // ConnectionManager.cpp:718
```

These look like the most obviously removable of the lot and they are the opposite. **This is the
safe idiom and the "simplification" is a remote format-string bug.**

`InGameUI::message(UnicodeString format, ...)` treats its first argument as a `printf` format.
`unitext` here is `"[" + player name + "] " + chat text`, and both halves arrive **from the
network** (`ConnectionManager::processChat`, from a `NetChatCommandMsg`). Writing

```cpp
TheInGameUI->message(unitext);      // NO
```

hands a remote player's chat line to `_vsnwprintf` as a format string with no arguments behind it.
A `%s` in someone's chat message then reads a pointer that was never passed. Passing the constant
`L"%ls"` as the format, with the text as the argument, is precisely what stops that, and whoever
wrote it knew what they were doing.

So these two stay. They are also the only two sites where a wide format really does need to carry a
`WideChar*` argument through the funnel, and the fix for them is local: `InGameUI` could grow a
plain `message(const UnicodeString&)` overload that does no formatting at all, which is what the
call site actually wants and what every other caller of `message` with a variable is already
relying on by accident (`InGameUI.cpp:280`, `:339`, `:7855` and a dozen more pass a
`TheGameText->fetch(...)` result as a format string; that is safe only because the shipped `.csf`
strings contain no `%`, which is a translator away from not being true).

### Two keep a format string because they have a number in it

```cpp
munkee.format(L"\t%d: %ls", i, slot->getName().str());        // WOLGameSetupMenu.cpp:2174
room.m_translatedName.format(L"%ls %d", names[nameIndex].str(), timesThrough);  // PeerDefs.cpp:339
```

Two specifiers, and both split trivially — format the number, concatenate the name. Neither needs
the format string to carry the wide argument.

---

## Answer to the question

**The mini-printf is not needed.** Counting what would be left after the removals above:

| | Sites | Needs new machinery? |
|:--|--:|:--|
| `format(L"%ls", x)` → assignment | 7 | no |
| `format(L"…%ls…%ls…")` → `concat` | 7 | no |
| number + wide string → split | 2 | no |
| `message(UnicodeString(L"%ls"), text)` | 2 | no — leave alone, or add a non-formatting `message` overload |
| **`%ls` sites requiring a wide format to carry a `WideChar*`** | **0** | |

The seven `%S` are a separate, smaller task: they convert `char*` to wide, they are all debug or
label text, and the same `WideCharFromUtf8`-shaped helper would serve them — or, since six of the
seven are again pure interpolation, `UnicodeString::translate(AsciiString)` already does exactly
this and is the existing idiom. Only `W3DDisplay.cpp:1783` genuinely mixes narrow strings with
`%.3f` floats, and it is inside the debug display.

## Recommendation

Make it a task, sized as a morning rather than two days:

1. Replace the seven `format(L"%ls", x)` with assignment.
2. Replace the seven concatenations with `concat`.
3. Split the two that carry a number.
4. Leave `ConnectionManager.cpp:706` and `:718` alone, with a comment saying why, because the next
   person to read them will want to simplify them. Optionally add `InGameUI::message(const
   UnicodeString&)` and use it there, which removes the trap rather than commenting on it.
5. Handle the seven `%S` with `translate`, except `W3DDisplay.cpp:1783`.

None of it needs the typedef flip, none of it needs a format parser, and after it there is no
`%ls` or `%S` left in a wide format anywhere in the tree.

**Not verified on Windows.** Nothing in this document is a code change; the argument types were
read from their declarations and the call sites from the source.
