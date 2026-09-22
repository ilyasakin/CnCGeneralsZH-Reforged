# B7 — W3D file format layout asserts

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** nothing (but C1 and D4 both rely on the guarantee)
- **Status:** not started
- **Size:** `w3d_file.h`; the structs are read by bulk binary reads across the asset loaders

## Why

Created 2026-09-22 from B4 recon's finding. B4's stated purpose was "stop a Mac build silently
misreading a `.w3d`" — and B4 turned out not to cover that at all. All 8 of its `#pragma pack`
regions are **wire** formats; not one is a file format.

The actual `.w3d` exposure is here, and it has no `#pragma pack` anywhere: `w3d_file.h` declares
the model, mesh, material and hierarchy structs, and they are read with bulk binary reads —
`distlod.cpp:304` does `cload.Read(&lodStruct, sizeof(W3dLODStruct))` and it is not alone. Layout
is load-bearing and **nothing asserts it**.

B4 recon's analysis says it is safe by EA's design rather than by luck: every data member is a
width-pinned bittype typedef (115 `uint32`, 63 `float32`, 43 `uint8`, 33 `char`, 21 `uint16`), a
char array, or a nested struct of the same. No `double`, no pointer, no `long`, no `bool`, no
`wchar_t`, no enum member — so every field is naturally aligned at its own width and there is no
padding for two compilers to disagree about.

That analysis is convincing and it is still only an analysis. These structs have a far stronger
claim on a `static_assert` than `DelayedTransportMessage` does, because a `.w3d` misread produces
wrong geometry rather than a failed build.

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
