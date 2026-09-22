# B17 — The D3DX maths that reaches GameLogic

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6, and M2 leans on it
- **Status:** not started
- **Risk:** the highest left in M1. This is arithmetic on the replay and network CRC path.

## Why

Created 2026-09-22 by B6 prep, and it corrects a claim **this plan made and told people to act on.**

B6's task file said `gameengine` needs WW3D2 "for `d3dx9math.h` alone", that the header is vendored
and header-only, and "if that is the whole of it… the fix is one line. Check before you believe
it." The first half is true — GameEngine includes **zero** WW3D2 headers except `d3dx9math.h`,
verified by intersecting all 146 WW3D2 header names against every include in GameEngine.

The conclusion was wrong. `d3dx9math.h:127`:

```cpp
extern D3DXVec4TransformFunction	D3DXVec4Transform;
```

A **function pointer**, defined at `d3dx9runtime.cpp:41` and filled at `:133` with
`GetProcAddress(D3DX9Module, "D3DXVec4Transform")` out of **`d3dx9_43.dll`**. So `gameengine`
needs `ww3d2` for a symbol and a Windows DLL at runtime to make it non-null.

**And it reaches the simulation.** `BezierSegment.cpp:112` and `BezFwdIterator.cpp:69-71` call it;
`DumbProjectileBehavior.cpp` is the only consumer of `BezierSegment` in the tree, and it lives in
`GameEngine/Source/GameLogic/Object/Behavior/`. It constructs the curve a shell flies along at
`:453`.

The header says so itself, at `:28`:

> Binding rather than reimplementing is deliberate. `D3DXVec4Transform` and `D3DXVec4Dot` reach
> GameLogic through `BezierSegment`, which `DumbProjectileBehavior` steers a shell with, so their
> arithmetic is part of the network and replay CRC. **A hand-written 4x4 inverse or transform would
> be a rounding difference nobody could see until a replay diverged.**

So this task reimplements, on macOS, a function whose author deliberately refused to reimplement it,
and that author wrote down exactly how it goes wrong.

## Why it is nevertheless tractable

Three things, all found by B6 prep:

1. **The matrix is exact.** `s_bezBasisMatrix` (`BezierSegment.cpp:241`) is the standard Bezier
   basis and every entry is a small integer: `-1 3 -3 1 / 3 -6 3 0 / -3 3 0 0 / 1 0 0 0`. Nothing
   about the representation is approximate.
2. **The semantics are already pinned by a test.** `test_dx9_smoke.cpp:374-395` runs this exact
   basis matrix through the real DLL and asserts the result, commenting "Each output component is
   the sum of one column" — so it is **row-vector × matrix**:
   `out.x = v.x*m[0][0] + v.y*m[1][0] + v.z*m[2][0] + v.w*m[3][0]`, not the transpose. That test is
   also a ready-made golden-reference generator the day a Windows machine exists.
3. **The precedent exists and is correct.** `D3DXVec4Dot` and `D3DXMatrixIdentity` were already
   written out inline at `:152`, "copied from `d3dx8math.inl` so the arithmetic that reaches a
   replay CRC is the arithmetic that always reached it." That is both the method and the standard.

## Do

1. Reimplement the functions GameLogic reaches, following the `D3DXVec4Dot` precedent exactly.
2. **Do not write the transform in whatever order reads nicely.** Copy the SDK's term order
   literally. Floating-point addition is not associative; a re-association is a rounding difference,
   and a rounding difference here is a replay divergence. This is the single instruction in this
   task that matters.
3. `-ffp-contract=off` is already mandatory and covers the FMA half. Summation order is the half
   that is yours.
4. Use E3's harness. It compares arm64 against real x86_64 on this machine in about a second, and
   it is the only tool here that can tell you whether your arithmetic agrees with x86's.
5. **Second, smaller blocker in the same file:** `d3dx9math.h:39` is `#include <d3d9.h>`, so
   `BezierSegment.h` drags the D3D9 header into GameLogic today. That has to go too.

## Done when

The functions are reimplemented, E3 reports no new differences, and the `WINDOWS-DEBT.md` row is at
**high** severity and says plainly that this is CRC-path arithmetic verified against one
architecture and not against the DLL it replaces.

**E1 must diff this against a Windows capture before M2 leans on it.** `test_dx9_smoke.cpp:374-395`
is how — it already runs the real DLL and asserts the answer.

## Do not

- Do not "improve" the arithmetic. Not a fused multiply-add, not a reassociation, not a more stable
  summation. The goal is bit-identity with a DLL, not numerical quality.
- Do not let this be judged by a test that only runs on macOS. That is how the difference stays
  invisible until a replay diverges, which is precisely what the header's author warned about.
