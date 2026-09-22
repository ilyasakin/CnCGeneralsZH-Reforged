# B12 — SSE2 in WWMath, and the rounding nobody must change

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** wwdebug, wwmath, ww3d2 — i.e. the four libraries M1 is trying to build
- **Status:** not started
- **Risk:** determinism. Read the whole of "Why" before writing a line.

## Why

Created 2026-09-22 from A2/B9's finding. `WWMath/wwmath.h:49` is an unconditional
`#include <emmintrin.h>`. On arm64 that drags in `mmintrin.h` and dies on ~20 undeclared
`__builtin_ia32_*` identifiers. **It is the first thing that kills `wwdebug`, `wwmath` and `ww3d2`
in a full build**, and no existing task is shaped to own it.

It is the only SIMD include left in the tree. But it is not a stray include, and this is the part
that matters:

```cpp
WWINLINE long WWMath::Float_To_Long(float f)
{
    // cvtss2si, not a cast: EA's fistp rounds to nearest even and a C cast truncates, and callers
    // in GameLogic depend on the rounding.
    return (long)_mm_cvtss_si32(_mm_set_ss(f));
}
```

That comment was written by this project, deliberately, and it is the warning label on a trap.
`_mm_cvtss_si32` rounds **to nearest, ties to even**. A C cast `(long)f` **truncates toward zero**.
They differ for every value with a fractional part. 18 call sites.

**If you replace this with a cast, the simulation changes and the port desyncs against Windows** —
and it will not announce itself. It will look like units arriving on slightly different tiles.
This is the same class of hazard as `-ffp-contract`, which A1 proved rather than assumed, and it
deserves the same treatment.

## Do

1. Guard the include. `<emmintrin.h>` is x86-only and nothing else in the tree needs it.
2. **Replace `Float_To_Long` with something that rounds to nearest, ties to even, on both
   platforms.** Candidates, in rough order of preference:
   - `std::lrint` / `lrintf` — rounds per the current rounding mode, which is round-to-nearest-even
     by default and which nothing in this codebase changes on macOS. On arm64 clang emits `fcvtns`,
     which is exactly the instruction wanted. Check that `FPUControl.h` does not alter the mode on
     the Windows side.
   - `__builtin_roundeven` — explicit, mode-independent, no reliance on the environment.
   - ARM intrinsics via `<arm_neon.h>` — works, but a NEON dependency for one scalar conversion is
     worse than a builtin.
   Keep the x86 path exactly as it is; do not "unify" the two onto a new implementation that
   changes the Windows answer.
3. **Prove the rounding, do not assert it.** A test over a table of committed values — halfway
   cases both directions (`0.5`, `1.5`, `2.5`, `-0.5`, `-1.5`), values either side of a halfway,
   negatives, and large magnitudes — with the expected results written down. That test is the
   deliverable as much as the fix. Run it here; it will run on Windows the day there is a machine.
4. Check `Float_To_Long(double)` (`_mm_cvtsd_si32`) the same way. It has no comment and the same
   exposure.

## Done when

`wwmath.h` compiles on arm64, the rounding test passes with committed expected values, and
`wwdebug`/`wwmath` get past this file. A `WINDOWS-DEBT.md` row at **high** severity, because
rounding is a simulation behaviour and this is the one change in M1 most able to desync a match
without failing anything.

## Do not

- **Do not use a C cast.** Not as a first cut, not "temporarily", not behind a TODO.
- Do not change the x86 path.
- Do not widen this into a general SIMD or vectorisation task. `vp.cpp`'s SSE is all inside
  `#if 0`; this is one include and one pair of functions.
