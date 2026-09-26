# B4 — Pragma audit

> **Verdict (2026-09-26, -47): no pragma splits the Windows and POSIX simulation arithmetic.** 335 of the
> 338 `#pragma optimize` lines are commented out; the 3 live ones are `_DEBUG`/`_INTERNAL`-only, so neither
> Release build keeps them; and there is no `float_control`, `fenv_access`, `fp_contract` or `STDC` FP
> pragma anywhere, live or commented. E1 need not model any pragma-induced divergence.

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** done (-47, 2026-09-26): audited, the missing wire-layout asserts added, `-Wno-unknown-pragmas`; see "Result" at the end
- **Size:** 335 `#pragma optimize`, 67 `#pragma warning`, 16 `#pragma pack`, 6 `#pragma comment`,
  3 `#pragma warn`, 1 `#pragma inline_depth` (936 `#pragma once` are fine everywhere)

## Why

clang warns on every unknown pragma and ignores it. Three of these four kinds are noise. One of
them is not.

## Recon findings, 2026-09-22 — read before scoping

The audit has been done (`docs/mac-port/B4-pragma-audit.md`). Four corrections to this file:

- **The E1 optimisation risk is CLOSED.** 332 of the 335 `#pragma optimize` are commented out. The
  three live ones are `Debug.cpp:74` (behind `_INTERNAL`, which `CMakeLists.txt` defines nowhere)
  and a balanced pair at `aabtree.cpp:732/795` (behind `_DEBUG`, so Release never sees them).
  `WWMath` has none at all, and all 129 in `GameLogic` are the identical commented line. E1 does
  not need to model optimisation-level divergence; `-ffp-contract` remains the real concern.
- **16 is a count of lines; it is 8 push/pop regions**, all already push/pop. The "convert bare
  `pack(n)` to push/pop" work item has nothing to convert.
- **All 8 are wire formats. None is a file format.** The `.w3d` exposure this task was written to
  cover lives in `w3d_file.h`, which has no pack pragma at all — that is now **B7**.
- **Two of the 8 wrap dead structs** (`CommandPacket`, `ConnectionMessage`), so 6 need asserts. Do
  not assert `CommandPacket`: its size depends on `sizeof(GameMessage)`, a class with a vtable,
  which is legitimately allowed to differ between MSVC x64 and clang arm64.

Item 2 is nearly empty: there is no CMake work. The one real item is `debug_debug.h:38`'s
`#pragma comment(linker,"/include:...")`, which forces a symbol past dead-stripping and needs
`-Wl,-u,<symbol>` on clang. That belongs to B6.

Item 4: it is 67 lines, not 70. There is no `#pragma warn`; the 3 are Watcom-form lines already
inside the 67, which MSVC has silently ignored for twenty years. 34 files hold all 67 and 32 are
under WWVegas, so `WWLib/always.h` is the home. Suppress rather than translate — the 62 MSVC-form
lines are written 38 different ways.

Two findings the implementer must act on:

1. `LANAPI.h:436`'s assert is `<=`. That is wrong for a wire format: it would accept a build 40
   bytes smaller that cannot talk to anyone. Tighten to `==` and add `offsetof` asserts on the
   union arms, since handlers read fields at fixed offsets.
2. `NetworkDefs.h:79` derives `MAX_PACKET_SIZE` — a wire-protocol constant — from
   `sizeof(TransportMessageHeader)`, a packed struct. A layout disagreement there produces no
   diagnostic at all: both builds run and simply cannot talk to each other.

## Scope

All of `GameEngine`, `GameEngineDevice`, `Main`, `Libraries/Source/WWVegas`.

## Do

Take them in order of how much they matter, which is the reverse of how many there are.

1. **`#pragma pack` — 16 sites, and these are the ones that matter.** Every one of them is a
   struct whose layout is a file format or a wire format. clang honours `#pragma pack(push, n)` /
   `pack(pop)` with MSVC-compatible semantics, so most will simply work — but *verify* rather than
   assume. For each one, find what reads or writes the struct, and add a `static_assert` on
   `sizeof` and on the `offsetof` of at least the last member. That assert is the deliverable; it
   is what stops a Mac build silently misreading a `.w3d` or a network packet. Any site using the
   older `#pragma pack(n)` / `pack()` form without push/pop gets converted to push/pop while you
   are there.
2. **`#pragma comment(lib, ...)` — 6 sites.** These are hidden link dependencies; `profile.lib` is
   called out in `CMakeLists.txt` as one of them. clang ignores them, so the link fails later and
   confusingly. Find each, and make the dependency explicit in `CMakeLists.txt` on both platforms.
3. **`#pragma warning` — 67, plus 3 `#pragma warn`.** Wrap in a small header that expands to the
   clang spelling or to nothing. Do not translate them one by one into `_Pragma("clang diagnostic
   ...")` by hand; most are suppressing a warning clang does not have.
4. **`#pragma optimize` — 335, and almost all noise.** Overwhelmingly `optimize("", off)` around
   2003-era compiler workarounds. Suppress the warning and leave them alone. But grep for the ones
   that turn optimisation *off* in maths or simulation code and list them on this task — if EA
   disabled optimisation to stop MSVC reassociating a float expression, clang at `-O2` may do the
   thing they were preventing, and that is a determinism risk E1 needs to know about.

## Done when

- A clang build produces no unknown-pragma warnings.
- Every `#pragma pack` struct has a `sizeof` and `offsetof` assert, and they hold on both
  platforms.
- Every `#pragma comment(lib)` is an explicit CMake link dependency.
- The `#pragma optimize` sites that sit in maths or simulation code are listed in the pull request,
  even if the answer is "harmless".
- Windows full build and `ctest` green.

## Do not

- Do not delete a `#pragma pack`. Not one. Every one is load-bearing until its assert says
  otherwise, and then it is still load-bearing.

## Result (-47, 2026-09-26)

**Method.** Every `#pragma` in GameEngine, GameEngineDevice, Main, WWVegas, Libraries/Include and
Compression was swept, with comments detected. Each line was resolved through `unifdef` in MSVC's
Release view and in the clang Release view (`windows_view_diff.py`'s macro sets, plus `_RELEASE`,
`-U_DEBUG`, `-U_INTERNAL`).

**Floating point.**
- `optimize`: 338 lines, 335 of them commented out (129 in GameLogic, 75 GameClient, 46 Common,
  45 W3DDevice, 24 GameNetwork, 8 WWVegas, the rest scattered). The 3 live ones are `Debug.cpp:86`
  (`_INTERNAL`) and the `aabtree.cpp:732/795` pair (`_DEBUG`); neither Release view keeps them.
- No `float_control`, `fenv_access`, `fp_contract` or `STDC FP_CONTRACT`/`FENV_ACCESS` anywhere.
- `#pragma clang fp contract(off)` ×2 in `d3dxportable.h`: clang-only, restating the global
  `-ffp-contract=off`.
- `intrinsic(memcpy, memset, _rotl)` in `_lzhl.h`: MSVC-only, integer, compression code.
- `inline_depth(255)` in `visualc.h`: MSVC-only; it affects inlining, not arithmetic.
- With MSVC's default `/fp:precise` and no `/arch` (SSE2, no FMA), and clang's `-ffp-contract=off`
  with no fast-math, neither compiler reassociates or fuses, so the optimisation level cannot change
  a per-operation IEEE result. The FP risks left are the maths library (`dettrig.h`) and runtime
  FP control (`test_fpucontrol`), not pragmas.

**Unknown pragmas.** The project's flags never enabled `-Wunknown-pragmas`, so a clang build showed
none. With it forced (a `-fsyntax-only` pass over all 1,067 game translation units) there were 39
distinct sites, 19,621 occurrences, all MSVC `#pragma warning` diagnostics lines. `CMakeLists.txt` now
says `-Wno-unknown-pragmas` explicitly, so a future `-Wall` stays usable. Two `#pragma message` lines
still print by default (`-W#pragma-messages`): `colmathobbobb.cpp`'s "Fatal assert disabled for demo"
and one TODO. They are informational and were left.

**`#pragma pack`.** All 8 regions are push/pop.
- Present before: `TransportMessageHeader` == 6, `LANMessage` == 471 (plus `<=` the datagram),
  `ManglerData` == 20, `ManglerMessage` == 30.
- Added: `sizeof` of `TransportMessage` (16 + `MAX_PACKET_SIZE`, 1110) and `DelayedTransportMessage`
  (4 + that); `offsetof` of every packed struct's members up to its last; and `LANMessage`'s fields in
  every union arm (the union at 34, each arm's post-`gameName` fields at 68 or later,
  `GameInfo.isDirectConnect` the last byte at 470).
- Every value is derived in a comment from the definition at pack(1), and checked by a probe on this
  build. Armed: an expected 470 changed to 469 fails the compile.
- The dead `CommandPacket` (whose size depends on `sizeof(GameMessage)`) and `ConnectionMessage` are
  not asserted, as the recon said. MSVC now compiles these asserts too (a WINDOWS-DEBT row, medium): a
  pack(1) layout that differed there fails the Windows build loudly, which is the point.

**`#pragma comment`.** All 6 are under `_DEBUG`/`_INTERNAL` (GameMemory ×4, StackDump, W3DGranny),
so there is no Release link dependency to make explicit.
