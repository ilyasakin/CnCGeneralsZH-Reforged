# B15 — Remove the wide-format `%ls`

- **Milestone:** M1
- **Depends on:** nothing (no typedef flip needed)
- **Blocks:** nothing, but it clears B1's last loose end
- **Status:** in progress: write-up and first removals merged; live `%ls` sites remain (was -3a)
- **Size:** 32 specifiers in 24 formats. Sized as a morning.

## Why

The answer to "should the funnel parse the format and consume the `va_list`?" is **no — remove the
sites instead.** Analysis at `docs/mac-port/B1-wide-format-sites.md`.

The 42 from the original survey included commented-out code. Live: 32 in 24 formats, and they are
**two unrelated problems that were being counted together**:

- **25 are `%ls` with a `WideChar*` argument** — the width problem.
- **7 are `%S` with a genuinely narrow `char*` argument** — not a width problem at all. Inside a
  wide format, `%S` means "narrow string" on MSVC, and all seven really are `AsciiString::str()` or
  `const char*`.

Of the 25:

| Count | Shape | What it actually is |
|--:|:--|:--|
| 7 | `mapDisplayName.format(L"%ls", mapData->m_displayName.str())` — the same line in six files | an assignment written as a printf |
| 7 | `format(L"[%ls] %ls", a, b)` | a concatenation written as a printf; `UnicodeString::concat` already does it |
| 2 | carry a `%d` alongside | split trivially into a format and a concat |
| 2 | `ConnectionManager.cpp:706`/`:718` | **see the warning — do not touch these** |

Sites that genuinely need a wide format to carry a `WideChar*` after those removals: **zero.** Do
not build the format parser.

## ⚠ DO NOT "SIMPLIFY" ConnectionManager.cpp:706 AND :718

```cpp
TheInGameUI->message(UnicodeString(L"%ls"), unitext.str());
```

This looks like the most obviously removable site of the 25 — a roundabout `message(unitext)`. **It
is the exact opposite, and collapsing it introduces a remotely triggerable format-string bug.**

`InGameUI::message(UnicodeString format, ...)` treats its first argument as a printf format.
`unitext` at those two lines is `"[" + player name + "] " + chat text`, and **both halves arrive
from the network** in `processChat`. Passing the constant `L"%ls"` with the text as an *argument*
is precisely what stops a remote player's chat line being interpreted as a format string. Whoever
wrote it knew.

Leave both sites exactly as they are, and put a comment on each saying why, so the next person who
spots the "redundant" format does not remove it either.

**Related, and out of scope but worth knowing:** about a dozen callers pass a
`TheGameText->fetch(...)` result as a format string — `InGameUI.cpp:280`, `:339`, `:7855` and
others. That is safe only because the shipped `.csf` strings contain no `%`, which is one
translator away from not being true. None is user- or network-controlled today. Flagged, not
actioned; it deserves its own look.

## Do

1. The 7 assignments become assignments. The 7 concatenations become `concat`. The 2 mixed ones
   split.
2. The 7 `%S` sites: these are correct on MSVC today and need a different fix from the `%ls` ones —
   decide per site whether the argument should be widened or the format narrowed.
3. Leave the 2 `ConnectionManager` sites alone, commented.
4. Verify as B1's sweep did: re-parse every touched format, count conversion specifiers against
   arguments, and finish with a scan proving no `%ls` or `%S` remains in any live wide format
   anywhere in the tree.

## Done when

No `%ls` or `%S` in a wide format anywhere, the two `ConnectionManager` sites carry a comment
explaining why they stay, and the specifier/argument count is clean.

## Do not

- Do not build a format parser. That was the design this analysis exists to avoid.
- Do not touch the five genuinely `wchar_t` files (`simpleplayer.cpp`, `urllaunch.cpp`,
  `PeerThread.cpp`, `BuddyThread.cpp`, and the `std::wstring` sites). They are correct.
