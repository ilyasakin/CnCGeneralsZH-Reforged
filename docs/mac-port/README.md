# Porting Zero Hour Reforged to macOS

This is the working plan for a macOS build. It is written to be picked up piecemeal: every task
under `tasks/` says what it touches, what it has to prove before it is done, and what it must not
touch, so an agent or a contributor can take one without reading the rest.

Read this file, then read the one task file you are doing. Nothing else here is required reading.

## The goal

`Generals.app` on Apple Silicon, playing a retail Zero Hour install's data, staying
lockstep-compatible with the Windows build: the same replay plays to the same checksum on both, and
a Mac and a Windows machine can share a network game.

Lockstep parity is a requirement, not a nice-to-have. It is also most of the reason this port is
worth attempting rather than starting over, because the hard half of it is already done — see
"Determinism" below.

## Not in scope

- **Intel Macs.** arm64 only. A universal binary can come later; it changes nothing structural.
- **The `Generals/` tree.** Only `GeneralsMD/Code` has a `CMakeLists.txt` and only Zero Hour is
  built. Unchanged here.
- **The Windows tools.** WorldBuilder, GUIEdit, ImagePacker and the rest of `Tools/` stay Windows.
- **GameSpy online.** The service has been dead since 2014. `gamespy` has to compile and link, and
  that is all it has ever had to do here. LAN over UDP is in scope.
- **Behaviour changes of any kind.** No rebalancing, no AI changes, no renderer "improvements"
  smuggled in behind a port. A commit from this plan changes where code runs, not what it decides.

## State of play

Measured against the tree at the time of writing, so you know which problems are real and which are
folklore.

**Already portable, and more of it than you would expect.** These are mostly side effects of the
x64 work, and they are what makes this plan finite:

| | |
|:--|:--|
| `GameEngine/Source/GameLogic` | 264 `.cpp`, **zero** Windows types |
| All of `GameEngine/Source` | 21 of 604 files touch `HWND`/`HRESULT`/`DWORD`/`windows.h` — 8 in Common, 7 in GameClient, 6 in GameNetwork |
| `windows.h` includes, engine + device + WWVegas | 58 files of 2,138 |
| Inline assembly | **two places, not none — B2 corrected this line.** `WWMath/vp.cpp`'s blocks are expanded only inside `#if defined(__ICL)` and are dead. `PerfTimer.h:75`'s was **live** in every `_DEBUG`/`_INTERNAL` build and under `-DPERF_TIMERS=ON`, which on an x64-only project MSVC cannot compile at all; B2 replaced it with `Lib/Clock.h`. Nothing else is x86-shaped, but `__rdtsc()` intrinsics remain in `PerfTimer.cpp` and `WWLib/mpu.cpp`. |
| Video | FFmpeg already |
| Validation | 38 test suites, `-headless`, replay checksum comparison |

**Determinism.** `Libraries/Source/WWVegas/WWMath/dettrig.h` replaced the CRT transcendentals with
integer arithmetic over a committed table, precisely so two machines cannot disagree by a bit.
`test_gameengine` has a `simulation_uses_no_runtime_trig` case that reads the engine's sources back
off disk to keep it that way. IEEE pins `+ - * /` and `sqrt`; x64 and arm64 both compute at declared
width with no x87. What is left is FMA contraction, which clang will do on arm64 and MSVC does not —
`-ffp-contract=off` is mandatory and E1 proves it.

**The work that is left**, largest first:

| Area | Size |
|:--|:--|
| Renderer | 2,630 `DX8Wrapper::` call sites across 93 files; 22 WW3D2 headers expose D3D9 types; 58 files include `d3d8.h`/`d3d9.h`; 5,231 lines mention D3D types |
| `WideChar` | `typedef wchar_t` — 2 bytes on MSVC, 4 on clang. 48 files, 724 `L"` literals, ~20 distinct `wcs*` calls |
| Platform layer | `Win32Device` is 10 files / 2,756 lines. Plus `WinMain.cpp`, DirectInput, XAudio2, SEH crash dump |
| Toolchain | `CMakeLists.txt` is MSVC-only: `/EHa /permissive /MP /Zi`, `CMAKE_MSVC_RUNTIME_LIBRARY`, `.lib` paths, MIDL. 335 `#pragma optimize`, 67 `#pragma warning`, 16 `#pragma pack`, 6 `#pragma comment` |
| Time | `timeGetTime` in 60 files, `QueryPerformanceCounter` in 28, `GetTickCount` in 20 |

**The seam that already exists.** `dx11backend.cpp` takes the engine's D3D9-shaped calls and
resolves them at draw time into state objects, samplers, generated shaders and input layouts. That
is the exact shape a Metal backend needs, and D2 generalises it rather than inventing it. Its own
header notes the funnel is unfinished — the engine still reaches past the wrapper — which is what D1
is for.

## Prior art in the repository

Checked so nobody checks again. Every renderer branch upstream is **merged into `main`** —
`wip/native-render-backends` included, which is where `dx11backend.cpp` and the rest of the D3D11
work came from (`bfb60e17 feat(w3d): native Direct3D 11 backend behind the D3D9 seam`). There is no
unmerged renderer work to fold in, and the D-track tasks are scoped against `main` as it stands.

Two upstream branches are **not** merged, one commit each and ~210 commits behind `main`:
`feature/game-control-mcp` and `feature/influence-map-viewer`. Both extend `ControlServer`, which is
already on `main`.

**`-control` is worth knowing about.** `GameEngine/Source/Common/System/ControlServer.cpp` (840
lines, on `main`) is a loopback WebSocket server that reads game state and plays as the local
player — posted mouse input, injected keys, world clicks, selection, orders, command bar presses.
The two unmerged branches add an MCP wrapper, a smoke test that walks menus and a skirmish, and
read-only influence/unit/terrain queries.

That makes it a validation instrument for this port, not just a feature to carry across: it can
drive a Mac build through menus and a real match from a script and compare the result against
Windows, which is exactly what M2 and M4 need and what neither has another way to do. B5 ports it
(the Winsock calls in it are plain BSD sockets). Whether to rebase either unmerged branch onto
`main` is a separate question from this plan, but if `-control` is going to be the test harness,
the smoke test on `feature/game-control-mcp` is the part to want.

## Milestones

Each milestone is a thing that works, not a percentage.

**Resequenced 2026-09-22.** A1 delivered a clean configure on arm64, but its own acceptance —
building `wwdebug`, `wwutil`, `wwmath`, `compression` — turned out to be unreachable from A1,
because all four need a `windows.h` story that belongs to B3 and B5. `always.h` alone uses `size_t`
with no `<stddef.h>` and spells `__cdecl` 13 times; `compression` hits `__int64`, `<windef.h>` and
`<emmintrin.h>` (SSE2 on arm64). So the real order is **A1 → B3 + B5 + B9 → the four libraries and
the two self-checks**, and that last step is a follow-up rather than part of A1.

**M1 — headless Mac build.** No renderer, no window, no sound. `gameengine` and the portable
libraries compile under clang on arm64, and the test suites that do not need a device run green.
This is where the toolchain, `WideChar`, the shims and the link-surface trimming all get proved, and
it is the milestone that tells you whether determinism survives clang before anyone writes a line
of Metal.
→ A1 A2 A3 B1 B2 B3 B4 B5 B6 E1

**M2 — headless Mac game.** `MacGameEngine` boots, mounts `.big` files, runs a skirmish under
`-headless`, and its replay checksum matches the Windows build's on the same seed. Playable by a
machine, not by a person.
→ C1 C2 C5

**M3 — renderer funnel.** Done on Windows, where the picture can still be A/B'd against the
existing D3D11 path. Every D3D call goes through `DX8Wrapper`; the backend interface is abstract;
the shader generators emit an IR rather than HLSL text.
→ D1 D2 D3

**M4 — it draws.** Metal backend, textures, the window. The game is visible on a Mac.
→ D4 D5 C3

**M5 — it is a game.** Sound, input polish, LAN, packaging, CI.
→ C4 E2, then whatever M4 left behind

## Dependency graph

```
A1 ─┬─> B1 ──> B4 ─┐                  B4 now waits on B1: see the note below
    ├─> B2 ────────┤
    ├─> B3 ────────┼─> B6 ──> E1 ──> [M1]
    ├─> B5 ────────┤            │
    ├─> B7 ────────┤            v
    └─> B8 ────────┘         C1 ─┬─> C2 ──> [M2]
A2 ──> A3                      C5 ┘
                                  │
D1 ──> D2 ──> D3 ──> [M3]         │
                 │                │
                 v                v
               D4 ─┬─> D5 ─┬─> [M4]
                   └─> C3 ─┘
                            C4 ──> E2 ──> [M5]
```

**B4 depends on B1, which the first version of this graph missed.** `LANAPI.h:50` sizes the LAN
broadcast's option buffer with `*2` literals that are `sizeof(WideChar)` written by hand. Measured:
with 4-byte `wchar_t`, `sizeof(LANMessage)` is 536 against a 476-byte limit; with `char16_t` it is
471. So the existing `static_assert` at `LANAPI.h:436` fails and a naive Mac build dies there
before reaching anything B4 adds. B1 first, always.

D1, D2 and D3 are Windows-side work and depend on nothing in tracks A, B or C. If two agents are
free, one should be on D1 from day one — it is the long pole and it does not wait for M1.

## Status board

Claim a task by editing its row here and the `Status:` line in its own file, in one commit, before
you start. That commit is the lock.

| ID | Task | Milestone | Depends on | Status | Owner |
|:--|:--|:--|:--|:--|:--|
| A1 | [CMake toolchain split](tasks/A1-cmake-toolchain-split.md) | M1 | — | **configure done** | -95 |
| A2 | [POSIX vendor script](tasks/A2-vendor-posix.md) | M1 | — | in review (zlib reopen) | -21 |
| A3 | [build.sh](tasks/A3-build-sh.md) | M1 | A2 | in review | -21 |
| B1 | [WideChar to char16_t](tasks/B1-widechar-char16.md) | M1 | A1 | in progress (steps 1-3) | -3a |
| B2 | [Time shim](tasks/B2-time-shim.md) | M1 | A1 | not started |  |
| B3 | [CRT and string shims](tasks/B3-crt-shims.md) | M1 | A1 | in progress | -95 |
| B4 | [Pragma audit](tasks/B4-pragma-audit.md) | M1 | A1 **B1** | recon done, waits on B1 |  |
| B5 | [Win32 scalar types](tasks/B5-win32-types.md) | M1 | A1 | recon done, held for -83 |  |
| B6 | [Trim the gameengine link surface](tasks/B6-gameengine-link-surface.md) | M1 | B1 B2 B3 B4 B5 B7 B8 | not started |  |
| B7 | [W3D file format layout asserts](tasks/B7-w3d-layout-asserts.md) — **see [findings](B7-w3d-layout.md)** | M1 | A1 | **done, held for B10** | -14 |
| B8 | [JobSystem thread pool](tasks/B8-jobsystem-threads.md) | M1 | A1 | in review | -83 |
| B9 | [Backslash include paths](tasks/B9-backslash-includes.md) | M1 | — | in progress | -21 |
| B10 | [bittype.h integer widths](tasks/B10-bittype-widths.md) | M1 | A1 | in progress | -14 |
| B11 | [CriticalSection](tasks/B11-criticalsection.md) | M1 | A1 | in review | -83 |
| B13 | [AsciiString's refcount](tasks/B13-asciistring-refcount.md) | M1 | A1 | in review | -83 |
| B14 | [WWVegas' threading primitives](tasks/B14-wwvegas-threading.md) | M1 | A1 | not started | |
| B15 | [Remove the wide-format %ls](tasks/B15-wide-format-removal.md) | M1 | — | in progress | -3a |
| B16 | [wwdebug's Windows dependency](tasks/B16-wwdebug-windows.md) | M1 | — | in progress | -14 |
| B17 | [D3DX maths on the CRC path](tasks/B17-d3dx-math-on-the-crc-path.md) | M1 | A1 | not started | |
| B12 | [SSE2 in WWMath and Float_To_Long](tasks/B12-simd-float-to-long.md) | M1 | A1 | in progress | -21 |
| E3 | [x86_64/arm64 differential harness](tasks/E3-arch-differential-harness.md) | M1 | A1 | in progress | -21 |
| C1 | [MacGameEngine and file systems](tasks/C1-mac-game-engine.md) | M2 | B6 | not started | |
| C2 | [Entry point](tasks/C2-entry-point.md) | M2 | C1 | not started | |
| C3 | [Input](tasks/C3-input.md) | M4 | C2 D4 | not started | |
| C4 | [Audio](tasks/C4-audio.md) | M5 | C2 | not started | |
| C5 | [Crash reporting](tasks/C5-crash-reporting.md) | M2 | B6 | not started | |
| D1 | [Finish the DX8Wrapper funnel](tasks/D1-dx8wrapper-funnel.md) | M3 | — | recon done; PR1 in progress | -8d |
| D2 | [Abstract the backend interface](tasks/D2-backend-interface.md) | M3 | D1 | not started | |
| D3 | [Shader generators emit an IR](tasks/D3-shader-generators-ir.md) | M3 | D2 | not started | |
| D4 | [Metal backend](tasks/D4-metal-backend.md) | M4 | D3 | not started | |
| D5 | [Texture formats](tasks/D5-texture-formats.md) | M4 | D4 | not started | |
| E1 | [Determinism gate](tasks/E1-determinism-gate.md) — **degraded, see note** | M1 | B6 | not started | |
| E2 | [CI matrix](tasks/E2-ci-matrix.md) | M5 | E1 | not started | |

Status is one of: `not started`, `claimed`, `in progress`, `in review`, `done`, `blocked: <why>`.

### Defects found in the shipping Windows game

Not port artefacts. These were found by porting, because porting means reading code with a compiler
that has different opinions — and they are arguably worth more to this project than the port is.
Each was verified independently before being recorded here.

**1. Debug and `_INTERNAL` builds do not compile, and have not since the x64 migration.**
`CMakeLists.txt:652` defines `_DEBUG` → `GameCommon.h:64` defines `DUMP_PERF_STATS` →
`PerfTimer.h:61` compiles `GetPrecisionTimer` → `PerfTimer.h:59` is `#define NO_USE_QPF`, so
`USE_QPF` is never defined → the `__asm { RDTSC }` block is the **live** branch. MSVC has no
`__asm` on x64, and CMake refuses to configure anything else. `-DPERF_TIMERS=ON` reaches the same
code in Release. Invisible only because nobody builds those configurations. *This plan asserted the
opposite on day one — see the state-of-play table's correction.*

**2. A double free in `AsciiString`, reproduced under AddressSanitizer.**
`AsciiString.h:394` and `:459`:

```cpp
InterlockedDecrement((long *)&m_data->m_refCount);   // return value discarded
if (!m_data->m_refCount)                             // separate, unsynchronised re-read
    freeBytes();
```

`InterlockedDecrement` **returns** the new value. Both sites throw it away and re-read the field, so
two threads dropping the last two references both observe zero and both call `freeBytes()`. Under
ASan the re-read itself is a heap-use-after-free — it touches the block the other thread has already
freed. Fixed in B13 by using what the decrement returns, so exactly one caller sees 1.

**3. An animation picks the wrong frame from 33 upward.**
`hrawanim.cpp`'s `Float_To_Long(frame - 0.499999f)` floor idiom is exact only below 33. Float
spacing doubles at 32, so from there up the subtraction lands on an exact tie, which rounds to the
even neighbour below — an odd frame returns `frame-1`. Every architecture agrees, so it does not
block the port. Found because a test written for the port went red against real code, and the test
was right. D-track.

**4. A Japanese player's auto-saved replay may fail to write.**
`StatsCollector.cpp:345` and `Recorder.cpp:1582` build a **file name** from a player's name through
`%ls`, which renders an unmappable character as `?` — illegal in a Windows filename. Reasoned from
the code, not observed. UTF-8 introduces no path-illegal byte, so B1's sweep incidentally fixes it.

**5. A format string one translator away from being attacker-controlled.**
About a dozen callers pass a `TheGameText->fetch(...)` result as a printf format
(`InGameUI.cpp:280`, `:339`, `:7855` and others). Safe only because the shipped `.csf` strings
contain no `%`. None is user- or network-controlled today. Flagged, not actioned.

Related and **not** a defect, because someone got it right: `ConnectionManager.cpp:706`/`:718` pass
a constant `L"%ls"` with network chat as the *argument*. It looks like a redundant format and it is
the thing stopping a remote player's text being interpreted as one. B15 says so in capitals.

### "ctest is green" was not what it looked like

Recorded 2026-09-22, because this plan's own status reports leaned on it. A1's `PENDING_MACOS`
keyword built five targets `EXCLUDE_FROM_ALL` and then **skipped `add_test` entirely**, so
`ctest -N` listed **11 on macOS against 16 on Windows** and nothing said so. `test_wwmath`,
`test_wwlib`, `test_wwsaveload`, `test_wwutil` and `test_gameengine` did not exist as far as ctest
was concerned. Green was green over a smaller set than anyone knew.

Separately, B7's own target sat inside a Windows guard, so the test written to protect the `.w3d`
format ran only on the platform that did not need it.

Both are now fixed: deferred tests are registered `DISABLED` rather than dropped, configure prints
how many, and the output distinguishes "Not Run (Disabled)" from a plain "Not Run" so a deferred
test and a broken one no longer look alike.

**The audit deliberately did not make ctest green.** Three tests fail on macOS and were left
failing, because their targets carry no deferral marker and so are expected to build here today.
Marking them disabled would have hidden three real regressions behind the exact move the audit
existed to stop.

### M1 has doubled, and that is the finding

It opened with six tasks. It has eleven. **Five of the six additions came from agents checking an
assumption in this plan and finding it wrong**, which is the strongest argument available that the
recon passes were worth the time:

| Task | Found by | What the plan had assumed |
|:--|:--|:--|

B10 is the one that would have hurt. Nothing about it fails loudly: the build succeeds, the game
starts, and every `.w3d` read walks off its own chunk boundary.

The pattern worth carrying into M2–M5: **the dangerous findings were all in things named to look
safe.** `uint32` that is not 32 bits. `#pragma pack` that covers the wrong formats. A "type leak"
task that was really a threading task. None was found by reading the plan; all were found by
someone measuring what the plan asserted.

## Rules for anyone working this plan

These are not style preferences. Breaking one of them costs somebody else a day.

1. **The Windows build never regresses — but nobody here can prove it.** Read this whole rule
   before your first commit; it overrides the "Windows full build and `ctest` green" line in every
   task file.

   **There is no Windows machine on this project.** Decided 2026-09-22, with the risk accepted
   deliberately and knowingly. Windows verification is therefore **deferred, not waived**. What
   that means for you, concretely:

   - You still must not knowingly break Windows. Before you change a line, read the MSVC branch
     around it. Most of the MSVC-specific code in this tree carries a comment explaining why it is
     there; those comments are the closest thing to a Windows reviewer you have.
   - Platform-specific changes go behind the platform guard, and the **Windows branch keeps its
     existing behaviour unchanged**. When you move an MSVC flag or an `#ifdef`, the goal is that a
     Windows build produces the same compiler command line it did before. Say so in the PR and
     show the reasoning.
   - **Every change you cannot verify gets a line in [`WINDOWS-DEBT.md`](WINDOWS-DEBT.md).** That
     file is the whole point of accepting this risk with open eyes: it converts an invisible
     problem into a list somebody can work through in an afternoon once a Windows machine exists.
     A task is not done until its debt is written down.
   - **Do not claim verification you did not perform.** Write "not verified on Windows" in the pull
     request, plainly. A green Mac build described as if it were both is how this port silently
     forks into two games.

   The moment a Windows machine or a CI runner appears, E2 is promoted to the top of the queue and
   `WINDOWS-DEBT.md` is worked from the top down.

2. **No `#ifdef _WIN32` scattered through game code.** Platform differences go behind a named
   header with a reason in its comment, the way `dettrig.h` does it. If you are adding the tenth
   `#ifdef` to a game source file, the abstraction is in the wrong place — stop and say so on the
   task.

3. **Nothing the simulation touches changes behaviour.** If your diff reaches
   `GameEngine/Source/GameLogic` or the maths under it, `replay-check.ps1` runs and the checksums
   match. A port commit that moves a replay checksum is a bug, not a port.

4. **One task per pull request**, per `CONTRIBUTING.md`. Conventional Commits, imperative, under 72
   characters, scope named after the library:
   `build(cmake): configure under AppleClang on arm64`
   `refactor(gameengine): carry text in char16_t rather than wchar_t`
   A new `macport` scope is fine for work that belongs to no existing library.

5. **Leave a test behind.** Same rule as the rest of the project: put the old behaviour back once
   and watch the new test go red before you trust it. For port work the test is usually "this
   target builds and runs on both", which belongs in `ctest`, not in a comment.

6. **Write down what did not work.** This project keeps its dead ends — reverted tree shadows, the
   first group-movement rework. If you try MoltenVK and abandon it, the task file gets a paragraph
   saying why. The next person needs it more than they need your success.

7. **Do not touch `CHANGELOG.md`** for port work. It is written for players, and none of this is
   visible to one until M4. The milestone that ships gets one entry.

## Open questions that need an answer before the milestone that depends on them

- **Metal and BC textures.** The art is DXT/BC (44 files in WW3D2 reference it). Apple Silicon
  exposes `MTLDevice.supportsBCTextureCompression`, but which Macs and which macOS versions is
  worth confirming rather than assuming. If the answer is patchy, D5 decompresses on load and eats
  the memory. **Owner: D5, answer before D4 finishes.**
- **Metal vs MoltenVK.** This plan assumes native Metal. MoltenVK buys a Linux port for free and
  costs a dependency plus a second translation layer. **Owner: D2, decide in D2, record the reason
  in the task file either way.**
- **Where the game data comes from.** There is no Mac Zero Hour. Users will have to bring `.big`
  files from a Windows or Steam install, and the launcher's install flow assumes a local one.
  **Owner: unassigned. Needs a product answer before M5, not an engineering one.**
- **Code signing and notarisation.** Unsigned is fine for M1–M4 and not fine for a release.
  **Owner: E2.**
