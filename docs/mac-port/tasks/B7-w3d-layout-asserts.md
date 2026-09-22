# B7 — W3D file format layout asserts

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** nothing (but C1 and D4 both rely on the guarantee)
- **Status:** in review — see [B7-w3d-layout.md](../B7-w3d-layout.md)
- **Size:** `w3d_file.h`; the structs are read by bulk binary reads across the asset loaders

## Why

Created 2026-09-22 from B4 recon's finding. B4's stated purpose was "stop a Mac build silently
misreading a `.w3d`" — and B4 turned out not to cover that at all. All 8 of its `#pragma pack`
regions are **wire** formats; not one is a file format.

The actual `.w3d` exposure is here, and it has no `#pragma pack` anywhere: `w3d_file.h` declares
the model, mesh, material and hierarchy structs, and they are read with bulk binary reads —
`distlod.cpp:304` does `cload.Read(&lodStruct, sizeof(W3dLODStruct))` and it is not alone. Layout
is load-bearing and **nothing asserts it**.

B4 recon's analysis said it was safe by EA's design rather than by luck: every data member a
width-pinned bittype typedef, no `double`, no pointer, no `long`, no `bool`, no `wchar_t`, no enum
member — so every field naturally aligned and no padding for two compilers to disagree about.

> ## Recon findings, 2026-09-22 — that analysis was WRONG. See [B7-w3d-layout.md](../B7-w3d-layout.md).
>
> Half of it holds: there is no `bool`, `enum`, pointer, `double`, `wchar_t` or bitfield member
> anywhere in the header (re-derived from clang's AST, not grep).
>
> The "width-pinned" half is false. `WWLib/bittype.h:46` has, with no platform guard:
> `typedef unsigned long uint32;` (and `sint32` from `signed long` at `:51`). `long` is 4 bytes
> under Windows' LLP64 and 8 under Apple's LP64, so **`uint32` is a name that looks width-pinned
> and is not**. 124 of the ~280 members in the header are `long`-based.
>
> **Measured: 50 of the 77 structs in `w3d_file.h` lay out differently under AppleClang arm64.**
> `ChunkHeader` (`chunkio.h`, the framing struct every `.w3d` chunk is read through, and the one
> `chunkio.cpp:465` does its seek arithmetic with) goes 8 → 16. `W3dMeshHeader3Struct` goes
> 116 → 160. A Mac build would not misread the occasional model; it would lose the file position
> at the first chunk and never recover.
>
> Two consequences for this task:
>
> - **`W3dChunkHeader` is dead** — zero references outside its own declaration. Item 3 below names
>   it one of the load-bearing four on B4's recommendation; the real one is `chunkio.h`'s
>   `ChunkHeader`. The test pins that instead and records why.
> - **Fixing `bittype.h` is NOT part of B7** and has not been done. `uint32`/`sint32` are used 701
>   times across 98 files in six libraries, and `DWORD`/`ULONG`/`BOOL` come from the same header.
>   That is its own task, or it folds into B3. B7's asserts are what make it provable: they are red
>   now and go green when it lands.

These structs have a far stronger claim on a `static_assert` than `DelayedTransportMessage` does,
because a `.w3d` misread produces wrong geometry rather than a failed build.

## Scope

- `Libraries/Source/WWVegas/WW3D2/w3d_file.h` — the declarations
- The loaders that read them in bulk. Start from `distlod.cpp:304` and sweep for
  `Read(&..., sizeof(W3d...))`
- New: a test file, sitting with the other suites in `Tests/`

## Do

1. Enumerate every struct in `w3d_file.h` that is read or written as raw bytes. A struct only
   accessed field-by-field does not need an assert; say which those are so the list is honest.
2. For each one that is: `static_assert` on `sizeof`, and on the `offsetof` of at least the first
   member, the last member, and every member that follows a smaller-width neighbour. That last
   category is where a padding disagreement would actually appear.
3. Prioritise `W3dChunkHeader`, `W3dMeshHeader3Struct`, `W3dVertexMaterialStruct` and
   `W3dHierarchyStruct`, which B4 recon named as the load-bearing four.
4. Re-verify the "no problematic member types" claim yourself rather than inheriting it. One
   `bool`, one `enum` member or one `long` anywhere in that header changes the conclusion.
5. Do **not** add `#pragma pack` to `w3d_file.h`. The structs are naturally aligned by design and
   packing them would change the layout you are trying to pin.

## Done when

The asserts exist, they hold under AppleClang arm64, and the list of structs deliberately left
unasserted is written down with the reason. A row in `WINDOWS-DEBT.md` records that MSVC has not
checked them — if MSVC disagrees, the build fails loudly there, which is the good outcome arriving
on someone else's machine.

## Do not

- Do not touch the `.big` reader. `Win32BIGFileSystem.cpp:251` builds `ArchivedFileInfo`
  field-by-field and has no layout exposure at all. C1 should keep it that way.
- Do not extend into save games. B4 recon explicitly did not audit them and flagged that the
  question of whether the save format blits structs is **open**, and C5's. Do not record it as
  answered.
