# E3 — x86_64 / arm64 differential harness

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** strengthens E1
- **Status:** done: merged (was -21)

## Why

This recovers part of what the no-Windows decision gave up, and nobody realised it was available.

**Rosetta 2 runs x86_64 binaries on this machine, and clang cross-compiles to x86_64.** So a single
Mac can build the same source twice — once arm64, once x86_64 — and run both. That does not give us
MSVC, and it does not give us Windows. It gives us the **architecture** half of the determinism
question, which is the half most likely to differ, and it gives it today rather than whenever a
Windows machine appears.

It is not hypothetical. B12 built a throwaway version of exactly this and it immediately found a
real defect in `DetRound.h` that B3's own 400,000-value sweep could not: **11 of 75 rows differ
between the real `cvtss2si`/`cvtsd2si` instructions and the arm64 path.** Every in-range value and
every tie agreed — the rounding is right — but NaN, infinity and out-of-range values do not.

The reason the sweep missed it is the lesson worth keeping: **it compared against the C library's
round-to-nearest, and the C library on this machine has the same 64-bit `long` and the same
saturation behaviour as the arm64 path. The reference and the code under test agreed with each
other while both differed from the x86 instruction.** A reference that shares the bug proves
nothing. Run the other architecture instead.

## Do

1. Productionise B12's harness (about 40 lines, runs in a second): a reference table, both binaries,
   and a row-by-row comparison. Ask agent 21 for it rather than rebuilding it.
2. Wire it into `ctest` so it runs with everything else. It must **skip cleanly, not fail**, where
   Rosetta or the cross-toolchain is absent — this is a developer-machine capability, not a
   requirement.
3. Start with the scalar conversions, since that is where the known defect is. Then widen to the
   things E1 actually cares about: `DetTrig`'s outputs, `DetRound`'s, and any float expression the
   simulation reaches.
4. **Be explicit in the harness's own header about what it does and does not prove.** It compares
   two architectures built by ONE compiler. It says nothing about MSVC's codegen, its optimiser, or
   its `long`. A green run here is not a green run against Windows, and the file should say so in
   as many words, because the temptation to treat it as such will be strong.
5. Where the two disagree, the harness reports rather than asserts a winner — which architecture is
   "right" is a judgement about what the Windows build does, and that is a separate question from
   whether they differ.

## Done when

Both builds run from one `ctest` invocation, the known `DetRound.h` rows are reported, and the file
says plainly what it does not cover.

## Do not

- Do not let this be described as closing the determinism gap. It closes the architecture half.
  `WINDOWS-DEBT.md`'s standing item about E1 stands unchanged, and this task should add a line
  saying which part of it is now covered and which is not.
- Do not use a C library function as the reference for anything the C library also implements. That
  is the exact mistake this task exists because of.
