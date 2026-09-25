# B17 — The D3DX maths that reaches GameLogic

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6, and M2 leans on it
- **Status:** in review (B17). macOS half merged (`cc93d068`); the Windows flip (decision 1 in the README) awaits its second read
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

## What was done (B17, 2026-09-25)

### The premise was wrong, and the DLL showed how

"Copy the SDK's term order" and "bit-identity with the DLL" turned out to be two different targets.
`D3DXVec4Transform` is not inline in any SDK header; it lives only in the DLL. Microsoft's
June 2010 x64 redistributable (sha256 `84b900db…67b4`) is on disk in every Steam install's
`_CommonRedist/DirectX/Jun2010/`, and its disassembly shows a CPU dispatch. The scalar fallback
and the non-Intel SSE body sum left to right; the **GenuineIntel** SSE body sums pairwise. The
table and the numbers are in the README's defect #7. So the Windows build already disagrees with
itself across CPU vendors, and a Mac can match one vendor or the other, never both.

`d3dxportable.h` uses the left-to-right order: the SDK's reference C, every non-Intel machine, and
Wine. Windows is unchanged. Whether Windows should stop taking this function from the DLL is the
user's decision; see defect #7 for what that change would cost.

### What exists now

| | |
|:--|:--|
| `WW3D2/d3dxportable.h` | the two functions, floats only, term order copied from Microsoft's machine code, `#pragma clang fp contract(off)` inside each |
| `WW3D2/d3dx9math.h` | old text inside `#if defined(_WIN32)` unchanged; a non-Windows branch with the four names GameEngine uses and no `d3d9.h` |
| `Tests/d3dx_golden.h` | 15 rows of outputs from Microsoft's bodies, as bit patterns, with the Intel body's lane x beside them |
| `d3dxportable_selfcheck` | asserts the golden rows on every build, needs no DLL. It also checks that the table can tell the two orders apart. Rule 5 was run: switching to pairwise gives 21 failures, and skipping the basis' zero products is caught by the signed-zero row |
| `d3dx_oracle` (ctest) | runs Microsoft's three bodies under Rosetta when `ZH_D3DX9_X64` names the cab or the DLL, and skips otherwise. 1,000,000 inputs: 0 differ from the scalar body, and the non-Intel body matches it on every lane. The Intel body differs only in lane x over the basis, 35.7% of the time |
| E3 `d3dx` section | 66 rows. The sweep fingerprint `0x18b774e8` equals the oracle's, so on those inputs arm64 matches the DLL's left-to-right bodies. Three `inf-w` rows differ by design (arm64 generates the NaN `0x7FC00000`, x86 generates `0xFFC00000`) and are recorded. No unrecorded difference |
| `dx9_smoke` | on Windows, prints every golden row as the real DLL computes it, plus the CPU vendor and which body ran. That is E1's capture, and the one check that can confirm the dispatch reading |

### What none of it proves

- **Which body a Windows process runs.** That was read out of the dispatch, never observed.
  `dx9_smoke`'s capture is how to observe it.
- **Anything about the game's own call sites running.** `BezierSegment.cpp` and
  `BezFwdIterator.cpp` cannot compile on macOS yet: `PreRTS.h:50` needs `atlbase.h`. With a
  scratch `PreRTS.h` that includes only C headers and `Lib/BaseType.h`, both files and
  `DumbProjectileBehavior.cpp` produce **zero** errors in the Bezier sources or either D3DX header.
  The errors that remain are all in B1/B3/B5 territory (`UnicodeString.h`, `STLTypeDefs.h`,
  `GameMemory.h`, `GameState.h`). Against the original header, all three stop at
  `d3dx9math.h:37`, `'d3d9.h' file not found`. That is a front end with a stand-in PCH, not a build.
- **MXCSR in a running game** (FTZ/DAZ). The sweep's inputs are normal numbers.
- **The `d3d9.h` include on Windows.** It still reaches GameLogic through `BezierSegment.h`
  there, deliberately. Removing it on Windows is header hygiene nobody here can compile, and it
  buys nothing on the platform where the header exists.

### Dead ends, kept

- **The contraction pragma does not beat `-ffp-contract=fast`.** Measured with `-mfma`: under
  clang's default and `-ffp-contract=on`, the unguarded expression fuses 12 times and the
  guarded function 0. Under `fast`, the guarded function still fuses 3 times, because clang
  documents `fast` as overriding the pragma. The build flag is the real protection; the pragma
  only covers a TU that loses the flag and falls back to clang's default.
- **E3 hid the NaN difference twice, through constant folding.** The first `inf-w` probe used a
  `const` array; the second a `static` array that nothing wrote, which clang also treats as a
  constant. Both folded to `0x7FC00000` on both architectures, and E3 passed. A ten-line
  `volatile` check showed Rosetta does produce x86's `0xFFC00000`, and the probe now reads a
  `volatile`. **A differential probe over a constant input measures the compiler, not the
  hardware.** That is a fourth blind spot for E3's header, alongside the three it already lists.

### The Windows flip (2026-09-25)

Decided by the PM under the user's delegation; the reasoning is decision 1 in the README.
`D3DXVec4Transform` and `D3DXVec4Dot` are one pair of inline functions after the platform split in
`d3dx9math.h`, going to `d3dxportable.h` everywhere. The DLL's transform stays bound as
`D3DXVec4TransformFromDLL`, a name the SDK does not own, and only `dx9_smoke` calls it, so the
capture still observes Microsoft's code and not ours.

For the second reader: each SDK name resolves once on Windows. A BezFwdIterator-shaped TU down the
`_WIN32` branch `static_assert`s that `D3DXVec4Transform` and `D3DXVec4Dot` are functions and the DLL
entry is a pointer. mingw-w64 accepts it; against the pre-flip header it fails. Nothing else loses
the DLL's version: outside the headers, the only callers of either name are the two Bezier files and
`dx9_smoke`, and `dx9_smoke`'s transform calls now name the DLL pointer explicitly. `D3DXVec4Dot` was
never the DLL's, and its expression is unchanged.
