# B10 — bittype.h integer widths

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B7's asserts go green, C1, D4, and anything that reads a `.w3d`
- **Status:** done: merged; not verified on Windows (was -14)
- **Size:** two typedefs; **701 uses across 98 files in six libraries**
- **Risk:** the highest in M1, and it is the one that cannot be verified here

## Why

Created 2026-09-22 from B7, which was checking a claim B4 had made and found it wrong.

`Libraries/Source/WWVegas/WWLib/bittype.h:46`, with no platform guard:

```cpp
typedef unsigned long	uint32;
typedef signed long	sint32;      // :51
typedef unsigned long	DWORD;       // :58
typedef unsigned long	ULONG;       // :65
```

`long` is **4 bytes under Windows' LLP64 and 8 under Apple's LP64**. So `uint32` is a name that
looks width-pinned and is not. Measured on this machine:

```
long=8  uint32=8  sint32=8  DWORD=8  ChunkHeader=16
```

B4 concluded the `.w3d` structs were safe because every member is a width-pinned bittype typedef.
That was right about the spelling and wrong about the typedef — it checked the member types and
never opened the header that defines them. **The same trap waits for anyone who greps a codebase
for member types that are project typedefs.**

**50 of the 77 structs in `w3d_file.h` lay out differently** under AppleClang arm64.

### Why this is a rout rather than a misread

The worst one is not in `w3d_file.h`. `ChunkHeader` (`WWLib/chunkio.h:91`) is two `uint32`s — 8
bytes on disk, **16 under clang** — and it is the framing struct every `.w3d` chunk is read
through. `chunkio.cpp:465` does the seek arithmetic with it:

```cpp
PositionStack[StackIndex - 1] += csize + sizeof(ChunkHeader);
```

A Mac build would not misread the occasional model. **It would lose file position at the first
chunk of the first file and never recover.**

Others: `W3dMeshHeader3Struct` 116→160, `W3dEmitterInfoStructV2` 124→208, `W3dHierarchyStruct`
36→48, `W3dVertexMaterialStruct` 32→40.

## Scope

`bittype.h` is the **sole** definer of `uint32` in the tree and reaches 37 files across
`GameEngine`, `GameEngineDevice` and `WWVegas`. Usage of `uint32`/`sint32`: WW3D2 44 files,
WWAudio 21, WWSaveLoad 15, wwshade 9, WWLib 5, WWMath 4.

The B4 network structs are **not** affected — `NetworkDefs.h` uses none of these typedefs. Verified.

## Do

1. `uint32` → a genuinely 32-bit type, and the same for `sint32`, `DWORD`, `ULONG`. Prefer
   `<cstdint>`'s `uint32_t`/`int32_t`.
2. **This is NOT the free no-op it looks like on Windows.** The *width* there is unchanged, but the
   *type identity* is not: `unsigned long` and `unsigned int` are distinct types even where both
   are 32 bits. That can move overload resolution, template specialisation and printf-format
   diagnostics on MSVC. `uint32_t` has the same property. This is precisely why it needs a full
   Windows build and not a reviewer's eye.
3. Expect new conversion warnings at minimum. B7 compiled 28 structs; nobody has compiled the 701
   use sites.
4. **Coordinate with B3 and B5.** `bittype.h` also defines `DWORD`, `ULONG`, `BOOL`, `WORD`,
   `BYTE`, `UINT`, `LPCSTR`. On Windows these coexist with `windows.h`'s. On macOS, once B5 has
   guarded `windows.h` out, **bittype.h's `DWORD` becomes the only `DWORD`** — at 8 bytes where
   Windows' is 4. Any `DWORD` B5 leaves behind silently doubles in width. Agree with B5 who owns
   which name.

## Result, 2026-09-22

Done. `uint32` -> `uint32_t`, `sint32` -> `int32_t`. `DWORD` and `ULONG` **removed** rather than
retyped, because a non-verbatim duplicate of an SDK typedef is a hard
`typedef redefinition with different types` on MSVC — so step 1's "and the same for `DWORD`,
`ULONG`" could not be done as written. `WW3D2/agg_def.h` was the only file in the tree that used
them without reaching `windows.h`; its two uses are now `uint32`, which is what
`W3dAggregateMiscInfo::OriginalClassID` was declared as all along.

The six remaining SDK names (`WORD`, `BYTE`, `BOOL`, `USHORT`, `UINT`, `LPCSTR`) are left alone:
they are 2/1/4/2/4 bytes and `const char *`, correct on both toolchains, so removing them would be
the duplication tidying step 4's "Do not" rules out.

**Verified in a real build**, not by replica — A2 landed mid-task, so this is `cmake` + `ctest`:

| | |
|:--|:--|
| `ctest -R test_w3dlayout` | **Passed**, 0.34s — B7's test, unedited |
| same test with the two typedefs put back | 20 `static_assert` failures, first `sizeof(ChunkHeader) == 8` |
| warnings across the 34 objects that compile | **byte-identical** before and after: 104 either way |
| the three `ctest` failures that remain | `windows.h` not found — identical with and without B10 |

Coordination (B5, B3) resolved: `bittype.h` reaches only WWVegas and `Tools/WW3D`, and nothing in
`GameEngine`/`GameEngineDevice`/`Main`, so B5 and B10 never overlapped. B5 is under instruction
not to create a `WinTypes.h`, so the SDK names could not be handed to it; deleting them was the
alternative and it needs nothing from B5. B3's shim deals only in `size_t`/`int`/`const char *`.

**What is NOT verified:** 701 uses across 98 files, of which 34 objects compile on macOS today.
The rest are blocked on `windows.h` (B5) and `_interlockedbittestandset` (B8). Nothing here has
been near MSVC.

## Done when

`Tests/test_w3dlayout.cpp` — already written by B7 and **red today** — goes green without being
edited. That test is the acceptance criterion and it exists before the fix, which is exactly the
project's own rule about watching a test fail before trusting it.

A `WINDOWS-DEBT.md` row at **high** severity. Be specific: the risk is not width, it is type
identity and overload resolution, and a Windows reviewer should be pointed at template
specialisations and `printf` format strings over `uint32` rather than at sizes.

## Do not

- Do not add `#ifdef _WIN32` around the typedefs to keep `long` on Windows. That preserves the bug
  on one platform and makes the two builds disagree about a type name forever.
- Do not widen the scope to `BOOL`/`LPCSTR` tidying. Widths only.
