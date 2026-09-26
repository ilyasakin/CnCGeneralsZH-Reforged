# E1 — Determinism gate

- **Milestone:** M1
- **Depends on:** B6
- **Blocks:** E2
- **Status:** in progress: the POSIX harness, the Mac baseline over a real fight, and defect #20 found and fixed (below); cross-platform parity needs a Windows run
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
these three, not a general audit. (Since 2026-09-26, decision 2's GPU rule gives a POSIX first launch
the High preset instead of Low. The trace above is why that is not expected to move a replay: the
full ctest run on that change, `replay_check` included, passed.)

## The POSIX harness, 2026-09-26 (-18)

`GeneralsMD/Code/Tools/replay-check.sh` is `replay-check.ps1`'s POSIX twin. For each seed it runs the
`-randommap`/`-autoskirmish -observer` match headless, moves the replay aside, plays it back with
`-replay`, then runs the same seed a second time, and compares the three runs' last
`HEADLESS CRC: 0x... at frame N`.
- **Rule 9:** it roots every run at its own farm of the install in `$TMPDIR`, with the fork's
  `Code/Data` overlay (decision 9, until packaging carries the overlay). Each run's log has its own
  `-logPrefix`, and everything is removed afterwards.
- **`--control` edits the kept replay's game seed** before the playback. The harness must then fail
  the seed on the playback, and not on the second run.
- **The IDLE guard:** a seed whose AI built nothing after frame 0 fails. An idle match agrees with
  itself and proves nothing.
- **The map size is the generator's own** (normal for the player count). `--cells` overrides it.
  `replay-check.ps1` passed 128, below even the small size, and at 128 seed 1's two starts land 124
  units apart (defect #19).
- **`replay_check`** (ctest, Skipped without data, about 90 s):
  - seed 0 at 1,200 frames agrees;
  - the seed-edit control fails on the playback only;
  - seed 1 at 128 cells is reported IDLE (the guard's control);
  - seed 1 at 12,000 frames agrees: fourteen checkpoint saves and still the recording's world, which
    is defect #20's end-to-end check.

- **`--extended`** (not in ctest) is the wider backstop the xfer audit asked for: seeds 2 to 5, each at
  2 and at 4 players, 12,000 frames, each checked against its checkpointed playback and a second run.
  Each match prints its fight, per player. The results are below.
- **Known limit, until P1:** the farm is the install plus `Code/Data`. It does not have the fork's own
  art archives (`ReforgedTextures.big`, `ReforgedNormals.big`, `ReforgedTerrain.big`, which vendor.sh
  puts in `Run/`), so the harness plays without them. Whether they reach anything the logic reads is
  not measured here. P1 switches the harness to `-overlay`.

**What it proves:** same-machine determinism only. A recording, its playback and a second run from
one seed agree on this machine and this build. It says nothing about agreement with a Windows build,
which needs a replay recorded on Windows.

**The Mac baseline**, macOS 27 arm64, M3 Pro, Release, feature/mac-port 33f968c9 plus defect #20's fix.
Default arguments: 2 players, brutal, the generator's size (248 cells), 12,000 frames. Each seed agreed
with its own checkpointed playback and with a second run. The install listing is identical before and
after.

| name | seed | frame | HEADLESS CRC | built after frame 0 | buildings / units built / peak units / lost, per side |
|:--|:--|:--|:--|:--|:--|
| `mac_baseline_seed0` | 0 | 12000 | 0x845181C8 | 46 structures | 26 / 45 / 54 / 8 and 20 / 61 / 85 / 8 |
| `mac_baseline_seed1` | 1 | 12000 | 0xB5B11D73 | 52 structures | 25 / 95 / 135 / 25 and 29 / 82 / 118 / 17 |

**The extended baseline** (`--extended`), same machine and build (feature/mac-port bcf6d8c4 plus the
View::xfer fix, which is client only). Eight matches at 12,000 frames on the generator's own map size.
Every one agreed with its checkpointed playback and with a second run. The install listing is
identical before and after. The last column is per side: units built / lost / kills / peak units,
then buildings.

| name | seed | players | HEADLESS CRC | built after frame 0 | per side: units built/lost/kills/peak, buildings |
|:--|:--|:--|:--|:--|:--|
| `mac_baseline_extended_s2_p2` | 2 | 2 | 0x1F9140F1 | 35 | 68/8/11/83, 16; 85/11/8/100, 20 |
| `mac_baseline_extended_s2_p4` | 2 | 4 | 0x7D64BB42 | 90 | 78/13/24/101, 26; 73/14/10/98, 22; 73/12/9/97, 25; 81/26/9/106, 20 |
| `mac_baseline_extended_s3_p2` | 3 | 2 | 0x26FBBCCE | 28 | 79/13/13/94, 13; 45/13/13/65, 16 |
| `mac_baseline_extended_s3_p4` | 3 | 4 | 0x16E34D91 | 78 | 72/25/27/83, 14; 74/12/14/91, 25; 78/23/8/67, 28; 47/26/36/72, 15 |
| `mac_baseline_extended_s4_p2` | 4 | 2 | 0x6403793D | 62 | 91/22/21/96, 37; 68/21/22/80, 27 |
| `mac_baseline_extended_s4_p4` | 4 | 4 | 0xB301D456 | 108 | 99/33/11/100, 33; 35/24/11/48, 22; 110/15/31/135, 27; 52/7/26/75, 30 |
| `mac_baseline_extended_s5_p2` | 5 | 2 | 0x17BD8906 | 39 | 62/17/19/77, 23; 57/19/17/76, 18 |
| `mac_baseline_extended_s5_p4` | 5 | 4 | 0x5FBD1BDE | 84 | 47/13/17/56, 25; 48/20/9/67, 20; 45/11/6/57, 20; 69/16/28/102, 23 |

With seeds 0 and 1, that makes 10 matches and 140 checkpoint saves with no divergence. The same
limits apply: this machine, this build, no Windows comparison, and no Reforged*.big archives.

**Replaced:** the first pair (0xAC31075F, 0xEE8A5309) was taken at 128 cells. There, seed 1's starts
sit 124 units apart and neither AI can build, so that pair covered an idle match (defect #19).

**What the harness found on the way: defect #20.** With a real fight, seed 1's playback parted from its
recording at frame 8,260. One object differed: a Stinger Soldier's rotation, 1-2 ULP off. The cause
was the replay viewer's checkpoint saves, which set every object's transform back through a setter that
recomputes its angle. Found by bisecting on `-maxframes`, then ruling out the wall-clock W3D clock,
then isolating the checkpoints. Fixed (README, defect #20).

## The architecture axis, 2026-09-26 (-18)

The whole game, not only E3's maths, built for **x86_64** with the same clang (`CMAKE_OSX_ARCHITECTURES=x86_64`,
FFmpeg's configure told to cross-compile) and run under Rosetta 2 beside the arm64 build.

- **x86_64 first could not start a match.** An `AsciiString` copied over itself (`nextToken` → overlapping
  `strcpy`) lost a random subset of the archive directory under x86_64's `strcpy`. That is undefined
  behaviour ARM64 and MSVC happen to tolerate; fixed with `memmove` (README, latent UB). With it,
  `test_gameengine` (444 tests, 521,993 checks) and `test_bigfilesystem` (hash c8140abc27b4d05d) pass on
  x86_64 exactly as on arm64.
- **Same seed, same world, on both architectures.** Seeds 0 and 1 at 12,000 frames, each agreeing with
  its own checkpointed playback and a second run:

  | seed | arm64 | x86_64 (Rosetta) |
  |:--|:--|:--|
  | 0 | 0x845181C8 | 0x845181C8 |
  | 1 | 0xB5B11D73 | 0xB5B11D73 |

- **Cross-play.** Each architecture's recording was played back on the other: seed 0 and seed 1, arm64 →
  x86_64 and x86_64 → arm64. All four reached the recording's HEADLESS CRC at frame 12,000, with
  "Start of a replay game" in the log and no "out of sync".

**What this covers:** the CPU axis, two instruction sets computing one simulation, in a full match
with real fights (46 and 52 structures, hundreds of units).
**What it does not:** the compiler axis. Both builds are clang with this project's flags; Windows
players run MSVC's code generation, `long` of 32 bits and MSVC's CRT. Rosetta is x86-64 semantics on
Apple hardware, not a Windows machine. The known compiler-side differences stay where E3 and the
WINDOWS-DEBT rows put them. A Windows-recorded replay is still the only thing that closes E1.

**The harness guards the install itself now.** Rule 9, after a script elsewhere wrote through a farm
link into the real install. replay-check.sh hashes every install file (sha256, 510 files, about 11 s)
before building its farm and again at exit however the run ends, and fails with exit 99 and the
differing files if anything changed. `REPLAY_CHECK_CONTROL_INSTALL=1` spoils the saved snapshot (the
install untouched) as the check's armed control, the fifth check of `replay_check`.
