# B7 — W3D file format layout

Deliverable for [B7](tasks/B7-w3d-layout-asserts.md): `GeneralsMD/Code/Tests/test_w3dlayout.cpp`,
wired into `ctest` as `test_w3dlayout`.

This document records what the measurement found, which is not what B7 was scoped to expect.

> ## The headline: B4 recon was wrong, and the `.w3d` format does not survive clang today
>
> B7 exists because B4 recon concluded that `w3d_file.h` was *"safe by EA's design rather than by
> luck — every data member is a width-pinned bittype typedef."* The PM asked me to re-verify that
> claim rather than inherit it. **It is false.** I wrote it and it is wrong.
>
> `Libraries/Source/WWVegas/WWLib/bittype.h:46`, with no platform guard of any kind:
>
> ```c
> typedef unsigned long	uint32;
> typedef signed long		sint32;   // :51
> ```
>
> `long` is **4 bytes under Windows' LLP64** and **8 bytes under Apple's LP64**. `uint32` is not a
> width-pinned type; it is a name that looks like one. That is precisely the trap: I checked the
> *spelling* of the member types in B4 and never opened the typedef.
>
> **Measured consequence: 50 of the 77 structs in `w3d_file.h` have a different layout under
> AppleClang on arm64 than they do on Windows.** `ChunkHeader`, the struct every single `.w3d`
> chunk is read through, goes from 8 bytes to 16. `W3dMeshHeader3Struct` goes from 116 to 160.
>
> A Mac build would not misread the occasional model. It would fail to read the first chunk header
> of the first file and never recover.

## What was measured, and how

Not replicas this time — the real headers. The numbers below come from clang's own record layouts
for the actual declarations:

```sh
clang++ -std=c++17 -fsyntax-only -w -I WWLib -I WW3D2 \
        -Xclang -fdump-record-layouts probe.cpp
```

`-fdump-record-layouts` only emits records that are actually laid out, so the probe declares one
variable of each of the 77 struct types to force it.

Two runs, differing in one thing only:

| Run | `uint32` / `sint32` | Represents |
|:--|:--|:--|
| **as-is** | `unsigned long` / `signed long` (8 bytes) | what a Mac build gets today |
| **fixed** | `uint32_t` / `int32_t` (4 bytes) | what Windows produces, and what the on-disk format is |

The "fixed" run is a faithful stand-in for MSVC *for this specific question*, because on Windows
`unsigned long` **is** 32 bits — so forcing 32-bit `long` under clang reproduces the Windows
layout exactly for structs built from these typedefs. It is not a general MSVC simulation and no
claim beyond layout should be read into it.

## Result

**27 of 77 identical. 50 different.** Full table in the commit; the load-bearing ones:

| Struct | as-is (clang) | on disk / MSVC | delta |
|:--|--:|--:|--:|
| `ChunkHeader` *(chunkio.h)* | 16 | **8** | +8 |
| `W3dChunkHeader` | 16 | 8 | +8 |
| `W3dMeshHeader3Struct` | 160 | **116** | +44 |
| `W3dHierarchyStruct` | 48 | **36** | +12 |
| `W3dVertexMaterialStruct` | 40 | **32** | +8 |
| `W3dPivotStruct` | 64 | 60 | +4 |
| `W3dAnimHeaderStruct` | 56 | 44 | +12 |
| `W3dTriStruct` | 48 | 32 | +16 |
| `W3dTextureInfoStruct` | 24 | 12 | +12 |
| `W3dMeshAABTreeHeader` | 64 | 32 | +32 |
| `W3dEmitterInfoStructV2` | 208 | 124 | +84 |
| `W3dMeshDamageStruct` *(w3d_obsolete.h)* | 64 | 32 | +32 |

The 27 that are identical are the ones built purely from `uint8`, `uint16`, `float32` and `char`
arrays — `W3dRGBStruct`, `W3dShaderStruct`, `W3dTexCoordStruct`, `W3dVertInfStruct`, the
`IOVector*` family and so on. They are unaffected because they contain no `uint32` at all.

### `ChunkHeader` is the one that matters most

It is not in `w3d_file.h` — it is `chunkio.h:91`, and it is the framing primitive for the entire
format:

- `ChunkLoadClass::Open_Chunk`, `chunkio.cpp:426` —
  `File->Read(&HeaderStack[StackIndex], sizeof(ChunkHeader))`
- `ChunkSaveClass`, `chunkio.cpp:129` and `:163` — `File->Write(&chunkh, sizeof(chunkh))`
- **`chunkio.cpp:465` — `PositionStack[StackIndex - 1] += csize + sizeof(ChunkHeader)`**

That last line is the killer. The *seek arithmetic* is expressed in `sizeof(ChunkHeader)`, so an 8
vs 16 disagreement does not merely misread a header; it desynchronises the file position for every
subsequent chunk in the file.

### A correction to the task file

`tasks/B7-w3d-layout-asserts.md` names `W3dChunkHeader` as one of "the load-bearing four", on my
B4 recommendation. **It is dead** — zero references anywhere outside its own declaration at
`w3d_file.h:521`. It is a documentation duplicate of `chunkio.h`'s `ChunkHeader`, which is what the
loaders actually use. The test file pins `ChunkHeader` and says why `W3dChunkHeader` is skipped.

## The member-type census, redone properly

B4's claim had two halves. One survives, one does not.

I re-derived this from clang's AST (`-ast-dump=json`, walking every `FieldDecl` in every `W3d*` /
`IO*` record) rather than from grep, so nested structs and unusual formatting are included.

**Survives — there are no problematic member *categories*.** Across all 77 structs there is no
`bool`, no `enum` member, no pointer, no `double`/`float64`, no `wchar_t`, no `long long`, no
`size_t`/`time_t`, and no bitfield. The five `bool`s B4 noticed are return types on `operator==` /
`operator!=`, not members. Every member is `uint8`, `uint16`, `uint32`, `sint32`, `float32`, one
raw `float` (`W3dAdaptiveDeltaAnimChannelStruct::Scale`), a `char` array, or a nested struct of
those.

**Does not survive — "width-pinned".** Two of those typedefs are `long`-based and therefore not
width-pinned at all. The census by frequency: 123 `uint32`, 78 `float32`, 46 `uint8`, 25 `uint16`,
1 `sint32`, plus `char` arrays and nested structs. **124 of the ~280 members are `long`-based.**

The lesson worth carrying: a census of member *type names* proves nothing when the type names are
project typedefs. Resolve to the underlying type, or measure.

## Blast radius: this is much bigger than `.w3d`

`bittype.h` is not a W3D header. `uint32`/`sint32` appear **701 times across 98 files**:

| Library | Files |
|:--|--:|
| `WW3D2` | 44 |
| `WWAudio` | 21 |
| `WWSaveLoad` | 15 |
| `wwshade` | 9 |
| `WWLib` | 5 |
| `WWMath` | 4 |

`bittype.h` also defines `DWORD`, `ULONG` and `BOOL` the same way, which collide with the Windows
SDK spellings and are used more widely still.

Most of those 701 uses are ordinary arithmetic where an 8-byte integer is merely wasteful. The ones
that bite are (a) struct members in a serialised format — this task, (b) anything reading or
writing `sizeof(uint32)` bytes to a file, of which `wwshade/shdsubmesh.cpp:842` and
`shddefmanager.cpp:227`/`:241` are live examples, and (c) `WWSaveLoad`, which is C5's and is
discussed below.

> ### Recommendation: fixing `bittype.h` is its own task, and it is not B7's
>
> The fix is small and obvious — `#include <cstdint>` and define `uint32` as `uint32_t`,
> `sint32` as `int32_t`, leaving the LLP64 result on Windows byte-identical. But it changes the
> meaning of a type used 701 times across six libraries, it interacts with the Windows SDK's own
> `DWORD`/`ULONG`, and it wants a `static_assert` block of its own next to the typedefs.
>
> That is a deliberate change with a real blast radius, and B7's mandate is to *assert* the layout,
> not to change the types. **I have not made that change.** It should be its own task, or fold into
> B3 (CRT and string shims), which is already the "make the scalar types behave" task.
>
> **What I did do is verify it works.** Applying it locally turns `test_w3dlayout` from a wall of
> errors into `1 tests, 4 checks, 0 failed`; the change was then reverted and is not in this
> commit. So the follow-up task starts from a known-sufficient fix.
>
> Two things for whoever takes it, neither of which B7 can settle:
>
> - **On Windows the width is unchanged, but the *type identity* is not.** `unsigned long` and
>   `unsigned int` are distinct types even where both are 32 bits, so the change can move overload
>   resolution, template specialisation and `printf`-format diagnostics on MSVC. Using
>   `<cstdint>`'s `uint32_t` has the same property — it is `unsigned int` on both toolchains. This
>   is the reason it needs a full Windows build rather than a reviewer's eye.
> - **The blast radius beyond layout is unmeasured.** 701 uses across 98 files were compiled by
>   nobody in this task; only the 28 structs in `test_w3dlayout` were checked. Expect new
>   conversion warnings at minimum.
>
> B7's asserts are what make that task provable: they are red now and go green when it lands.

## The deliverable

`Tests/test_w3dlayout.cpp` — 92 `static_assert`s (28 `sizeof`, 59 `offsetof`, 5 on the scalar typedefs) over 28 structs, plus a runtime `TEST` case so
`ctest` reports a green line rather than nothing.

Per the task's step 2, each struct gets `sizeof`, the first member's `offsetof`, the last member's
`offsetof`, and **every member that follows a smaller-width neighbour** — which is where a padding
disagreement actually appears. Those are individually commented, e.g.
`W3dTextureInfoStruct::FrameCount` (a `uint32` after two `uint16`s) and
`W3dMeshHeader3Struct::NumTris` (a `uint32` after a `char[16]`).

The file also carries a block asserting `sizeof(uint8/uint16/uint32/sint32/float32)` directly, with
a comment pointing at `bittype.h` — so that the first thing a reader sees is the root cause rather
than sixty consequences.

**Wired in** at `CMakeLists.txt` next to `test_ww3d2`, as `add_lib_test(test_w3dlayout)` with no
library dependencies. It is header-only on purpose: it stays buildable on a platform where `ww3d2`
itself does not compile yet, which is the whole of M1.

### Proved in both directions, in the tree

A1 landed while this was in progress, so the final verification is not a replica. The test was
built against the real headers with the include directories its CMake target declares:

```sh
clang++ -std=c++17 -I Tests -I Libraries/Source/WWVegas/{WW3D2,WWLib,WWMath,WWDebug,WWSaveLoad,Wwutil} \
        Tests/test_w3dlayout.cpp Tests/test_main.cpp -o test_w3dlayout
```

Per the project's rule 5 — put the old behaviour back once and watch the test go red:

| Tree state | Result |
|:--|:--|
| `bittype.h` as it is today | **fails to compile**, first error `sizeof(ChunkHeader) == 8` |
| `bittype.h` with `uint32`/`sint32` made 4 bytes | **builds and runs green** — `1 tests, 4 checks, 0 failed` |

That second row is not a prediction. The two-line change to `bittype.h` was applied locally, the
test rebuilt and run, and then the change reverted — it is **not** part of this commit. So the
recommended fix below is verified to be sufficient for all 28 structs, rather than merely likely.

**Update: A2 landed, so this now runs under `ctest` for real.** With the vendored sources fetched
by `Tools/vendor.sh`, `cmake` configures and:

```
$ ctest -R test_w3dlayout --output-on-failure
1/1 Test #2: test_w3dlayout ...................   Passed    0.34 sec
100% tests passed out of 1
```

and with `bittype.h`'s two typedefs put back, the same target fails to build with 20
`static_assert` errors. Both directions, in the real build system.

**A placement bug the real build caught.** The first version of this wired `add_lib_test` in next
to `test_ww3d2` — which A1 had since wrapped in `if(ZH_PLATFORM_WINDOWS)`, because everything from
there down links the renderer. So the target existed on Windows and silently did not exist on
macOS: `cmake --build --target test_w3dlayout` said *"No rule to make target"*, and `ctest` simply
never listed it. A header-only test that only runs on the platform it was written to protect is
worse than no test, because the absence looks like a pass. It now sits above that guard, next to
`test_compression`, with a comment saying why it must stay there. **This is a good argument for
running a test you have just added rather than trusting that adding it was enough** — the
hand-built verification below was green the whole time the CMake target did not exist.

**It does not need `ww3d2`.** The PM flagged that asserts living in a header only `ww3d2` includes
would not be exercised until B3 and B5 get that library compiling. That was the reason for making
this a header-only target with no library dependencies: `test_w3dlayout` builds and runs today,
against headers `ww3d2` cannot yet compile.

### Structs deliberately left unasserted

Listed in full at the bottom of the test file so the list travels with the code. In summary:

- **`W3dChunkHeader`** — dead, see above.
- **~45 structs in `w3d_file.h` that no loader reads in bulk.** They are either written only by the
  Max exporter in `Tools/` (out of scope, staying Windows), or read field-by-field through micro-
  chunks, which carry their own length and do not depend on C struct layout. Pinning them would
  assert layouts nothing relies on.
- **`w3d_obsolete.h`** — `W3dMeshDamageStruct` and friends *are* bulk-read (`meshdam.cpp:121`,
  `:209`, `:245`; `meshmdlio.cpp:669`, `:706`) and `W3dMeshDamageStruct` does differ, 64 vs 32.
  Omitted only because it is the pre-3.0 format and no shipping asset is believed to use it. This
  is a judgement call, not a proof — if anyone finds a retail asset on those paths, they belong in
  the file. Worth a follow-up; not a blocker.
- **`WWSaveLoad`** — C5's, and open. See below.

## Save games: still OPEN, and the five-minute look says do not assume it is fine

B4 recon flagged that nobody had audited whether the save format blits structs. The task file
records it as explicitly open and assigned to C5, and **this document does not answer it.** What
follows is a five-minute look, reported as a signal for C5 and nothing more.

The good news is that the primary mechanism is typed and field-by-field. `Common/Xfer.h:139-157`
declares `xferInt`, `xferReal`, `xferBool`, `xferAsciiString`, `xferCoord3D` and so on — layout-
independent by construction.

**But there is a raw-blit escape hatch and it is heavily used.** `Xfer.h:174` declares
`virtual void xferUser(void *data, Int dataSize)`, and there are **249 call sites**. A census of
what gets passed through it:

- Real structs: `GameClientRandomVariable` (27 sites), `Matrix3D` (7), `Coord3D` (7), `Vector3` (4),
  `RadiusDecal` (3), `DoorInfo`, `IRegion2D`, `WindMotion`, `TFade`.
- Around 90 distinct enum types, one site each — `sizeof(Relationship)`, `sizeof(DamageType)`,
  `sizeof(VeterancyLevel)` and so on.
- `sizeof(WideChar)` at 3 sites — **which makes this a B1 interaction as well.** If `WideChar`
  changes width, those three sites change the save format.

So the honest state is: the save format is *not* purely field-by-field, it has ~249 raw-byte paths,
and at least three of them are coupled to B1. Whether any of those structs or enums actually
changes size between MSVC x64 and clang arm64 is **not something I checked** — several of them
(`Matrix3D`, `Vector3`, `Coord3D`) are float-only and almost certainly fine, and enum size is 4 on
both under normal settings. The question is open and it is C5's.

**Do not let this section be cited as "save games were checked".** It is a list of places to look.

## Windows

Nothing here has been run on Windows. Every number in this document is a clang measurement; the
MSVC column is the 32-bit-`long` run, which reproduces the Windows layout for these particular
structs by construction but is not MSVC. A row is added to
[`WINDOWS-DEBT.md`](WINDOWS-DEBT.md) recording that the asserts themselves are unverified there —
though note that this is the benign direction: if MSVC disagrees with any of these numbers, the
Windows build fails loudly at compile time rather than misreading a file.

`CMakeLists.txt` gains one `add_lib_test` and one `target_include_directories`. It adds a target;
it changes no flag and no existing target, so the compiler command line for everything that
existed before is unchanged.
