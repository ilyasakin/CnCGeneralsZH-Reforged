# E1 — Determinism gate

- **Milestone:** M1
- **Depends on:** B6
- **Blocks:** E2
- **Status:** in progress: the POSIX harness and a first Mac baseline (below); the skirmish AI does not build yet
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

## What this task can and cannot do right now

**Read this before scoping the work.** There is no Windows machine on this project, and E1 was
designed around having one. Its central experiment — same seed, both platforms, same checksum —
**cannot be performed**. See [`../WINDOWS-DEBT.md`](../WINDOWS-DEBT.md).

What remains possible, and is still worth doing:

- Everything in steps 1 and 2 below (the libm sweep and the FMA contraction check). These are
  single-platform tests and they are the ones most likely to catch a real bug early.
- Step 3 becomes: run the headless skirmish on macOS, **record** the checksums for a set of seeds,
  and commit them as the Mac baseline. Self-consistency across runs on one machine is a genuine
  property worth testing — it catches uninitialised memory, map iteration order and address-
  dependent behaviour, which are real bugs this would find.

What is **not** established by any of that: whether a Mac build desyncs against a Windows one. Do
not describe this task's output as determinism parity, in a commit message, a test name or a
comment. Name the committed values `mac_baseline_*` so nobody later mistakes them for agreed ones.

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

## The simulation reads LOD (found 2026-09-26, B5/A2): trace before trusting any POSIX replay

Found by -18 while answering the POSIX device's chipset question. Graphics detail reaches logic in
at least three places:

- `GameLogic.cpp:1874-1885`: `forceFluffToProp` depends on the STATIC LOD preset, except when
  `TheRecorder->isMultiplayer()`, which pins it. So in skirmish and solo replays, the logic-side
  props depend on the recording machine's preset.
- `ObjectCreationList.cpp:1365`: LOGIC debris objects are skipped by the DYNAMIC LOD's skip mask,
  which follows the frame rate.
- `SlowDeathBehavior.cpp:240, 391`: death timing is scaled by the LOD's `m_slowDeathScale`.

Not yet traced: whether replays and network games pin the last two. If they do not, two Windows
machines with different frame rates or presets already disagree, which would be a defect of the
shipping game. For the port, it matters because the POSIX device reports DC_UNKNOWN with PS 0.0 caps,
so its STATIC preset can differ from the Windows recording's (every POSIX machine gets LOW until a
renderer reports a chipset, B6's finding). Before E1 compares any POSIX replay with a Windows one,
trace all three. For each, either show that it's pinned in replays and network games, or make the
replay header carry what the recording machine used and have playback use that. Search for further
readers of TheGameLODManager, TheGlobalData's LOD fields and the dynamic-LOD skip masks from
GameLogic, by symbol.

**Traced 2026-09-26 (-18, by symbol, read-only): all three are pinned or neutralised for every game
that can be replayed or played over a network. No defect.**
- `getSlowDeathScale()` is always 1.0: since `1b8279c6` ("close every desync the second audit found"),
  `applyDynamicLODLevel` no longer copies it, and only the constructors write it.
- `isDebrisSkipped()` never skips: the same commit stopped copying the skip mask, and only the
  constructors write it (0), so `x & 0 == 0`. The frame-rate-driven dynamic LOD now changes only
  client-side particle settings.
- `forceFluffToProp` and `useTrees` are pinned TRUE whenever `TheRecorder->isMultiplayer()`, which
  holds for LAN, internet and skirmish while recording, and for any replay with an occupied slot
  during playback. The LOD-dependent path is single player and Generals Challenge only, and those
  modes are not recordable (`isRecordableGameMode`).
So E1's `-autoskirmish` runs are unaffected by the POSIX device's DC_UNKNOWN preset. What this does
NOT cover: other client-only settings read by logic that nobody was looking for. It is a trace of
these three, not a general audit.

## The POSIX harness, 2026-09-26 (-18)

`GeneralsMD/Code/Tools/replay-check.sh` is `replay-check.ps1`'s POSIX twin. For each seed it runs the
`-randommap`/`-autoskirmish -observer` match headless, moves the replay aside, plays it back with
`-replay`, then runs the same seed a second time, and compares the three runs' last
`HEADLESS CRC: 0x... at frame N`.
- **Rule 9:** it roots every run at its own farm of the install in `$TMPDIR`, with the fork's
  `Code/Data` overlay (decision 9, until packaging carries the overlay). Each run's log has its own
  `-logPrefix`, and everything is removed afterwards.
- **`--control` edits the kept replay's game seed** before the playback. The harness must then report
  DIVERGED, on the playback and not on the second run.
- **`replay_check`** (ctest, Skipped without data) runs seed 0 for 1,200 frames and the control for 600.
  With data it passes in about 30 s: 0xEB822AF0 at frame 1,200. The control gives the playback
  0xFF469310 against the live 0xE21F0AC9 at frame 600.

**What it proves:** same-machine determinism only. A recording, its playback and a second run from
one seed agree on this machine and this build. It says nothing about agreement with a Windows build,
which needs a replay recorded on Windows.

**The first Mac baseline**, macOS 27 arm64, M3 Pro, Release, at feature/mac-port d71941d3 plus this
harness. Default arguments: 2 players, brutal, 128 cells, 12,000 frames. The install listing is
identical before and after.

| name | seed | frame | HEADLESS CRC |
|:--|:--|:--|:--|
| `mac_baseline_seed0` | 0 | 12000 | 0xAC31075F |
| `mac_baseline_seed1` | 1 | 12000 | 0xEE8A5309 |

**Read these with the finding below.** They are self-consistent, but the match they cover barely
moves.

**Finding: the skirmish AI does not build.** Over 12,000 frames (6.7 minutes of game time), seed 1's
two brutal AIs trained 5 and 2 units, spent 4,500 each, and never placed a second structure. The log has
the frame-0 command centres (`AI BUILT frame 0`) and then no `AI ECONOMY`, `AI WAVE` or `AI TACTICS`
line at all, so the AI's economy never acts. The runs reach 2,000-5,000 logic fps, where the README's
Windows figure (a 23-minute skirmish in 38 s) is about 1,100 with a real fight. Until this is
explained, a CRC here covers a nearly idle world, and a divergence in movement, combat or the AI could
not show up in it. Not yet diagnosed.
