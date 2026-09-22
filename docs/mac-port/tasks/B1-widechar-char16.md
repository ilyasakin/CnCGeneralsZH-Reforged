# B1 — WideChar to char16_t

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** not started
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
| **`Common/System/XferCRC.cpp:355`** | **the checksum.** `sizeof(WideChar) * len` feeds the CRC that replays and network games compare. A Mac build with 4-byte `WideChar` desyncs against Windows on the first unit with a name, and reports it as a desync rather than as a bug. |

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
