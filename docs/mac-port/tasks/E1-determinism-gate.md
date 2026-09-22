# E1 — Determinism gate

- **Milestone:** M1
- **Depends on:** B6
- **Blocks:** E2
- **Status:** not started
- **Size:** a test and a script; the change it forces could be anywhere

## Why

This is the task that decides whether the port is worth finishing, and it is deliberately placed at
the end of M1 rather than at the end of the project.

Zero Hour is lockstep. Every machine runs the same simulation from the same seed and exchanges
orders, not state. If a Mac build and a Windows build disagree about one float in one frame, they
are playing different games a second later, and the game reports it as a desync — which is exactly
the class of false report this project already fixed once for replays.

The good news is that the hard part is done. `dettrig.h` replaced the CRT transcendentals with
integer arithmetic over a committed table, because "IEEE 754 requires +, -, *, / and sqrt to be
correctly rounded... It says nothing about sin, cos, atan2, asin or acos". `test_gameengine`'s
`simulation_uses_no_runtime_trig` case reads the engine's sources back off disk to keep it that way.
x64 and arm64 both compute at declared width; neither has x87.

What is left is small, specific, and has to be proved rather than assumed:

- **FMA contraction.** clang contracts `a*b+c` into a single fused multiply-add on arm64. MSVC does
  not. The fused result is *more* accurate, which is worse — it is different. A1 sets
  `-ffp-contract=off`; this task proves it is set and stays set.
- **`-ffast-math` anywhere.** It must not be, not in any target, not from a dependency's flags.
- **Library calls that are not `DetTrig`.** `powf`, `expf`, `fmodf`, `logf` have the same
  under-specification as `sinf` and are not covered by the existing check.
- **`#pragma optimize` sites in maths code** that B4 was asked to list — if EA turned optimisation
  off to stop MSVC reassociating an expression, clang at `-O2` may do it.
- **Struct layout**, which B4's `#pragma pack` asserts cover, and **`WideChar` width**, which B1's
  CRC test covers.

## Do

1. Extend `simulation_uses_no_runtime_trig` to cover the rest of libm: `powf`, `expf`, `logf`,
   `fmodf`, `sqrtf` is fine (IEEE pins it), and whatever else the sweep finds. Same technique —
   read the sources back off disk.
2. Add a compile-time or startup check that FMA contraction is off. A test that computes a known
   `a*b+c` whose fused and unfused results differ in the last bit, and asserts the unfused one, is
   blunt and effective.
3. **The real gate: cross-platform checksum parity.** Run the same headless skirmish on Windows and
   macOS from the same seed and compare. `replay-check.ps1` does this for two runs on one machine;
   this needs the two-machine version. `README.md` says a 23-minute skirmish plays out headless in
   38 seconds and the same way every run, so the experiment is cheap.
4. Commit the expected checksums for a small set of seeds so the comparison is a test rather than a
   ritual, and so a regression six months from now is caught by `ctest` and not by a player.

## Done when

The same seed produces the same simulation checksum on Windows x64 and macOS arm64, for at least
three seeds, over a full-length match. Checked in, running from `ctest` on both platforms.

If it does **not** match, that result is the deliverable. Bisect to the first diverging frame, find
the expression, and write what it was in this file — that finding is worth more to this project than
the rest of M1 put together, and it is why this task sits at the end of the first milestone instead
of the last.

## Do not

- Do not "fix" a mismatch by loosening a comparison or by rounding. A lockstep simulation is
  bit-exact or it is not lockstep.
- Do not skip this to get to M2 faster. Everything after this point assumes it passed.
