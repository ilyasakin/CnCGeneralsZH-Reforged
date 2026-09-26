# Porting Zero Hour Reforged to macOS and Linux

This is the working plan for a macOS build, and, since 2026-09-25, for Linux alongside it (decision
3 below). macOS is still the first target and the one the milestones are phrased for; Linux is held
to "builds and passes the same tests" at every step, and ships at M5. It is written to be picked up piecemeal: every task
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
  Linux is x86_64 and arm64 both, because that is where Linux players are.
- **Platforms beyond Windows, macOS and Linux** are not targets, but nothing may be written in a
  way that shuts them out. Code is Windows or POSIX first, and Darwin or Linux only as a refinement
  of POSIX; a platform nobody has ported hits an `#error`, never a silent fallthrough. See decision 3.
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

**M1 — headless Mac build. REACHED 2026-09-26** (`81abdf13`): all 606 `gameengine` sources compile
under clang on arm64, and `test_gameengine` passes under ctest on macOS (443 tests, 521,945 checks),
beside every portable library's suite. Not verified: Windows (never compiled by MSVC; see
`WINDOWS-DEBT.md`) and Linux (the milestone-boundary `linux-check.sh` run is still owed). No renderer, no window, no sound. `gameengine` and the portable
libraries compile under clang on arm64, and the test suites that do not need a device run green.
This is where the toolchain, `WideChar`, the shims and the link-surface trimming all get proved, and
it is the milestone that tells you whether determinism survives clang before anyone writes a line
of Metal.
→ A1 A2 A3 B1 B2 B3 B4 B5 B6 E1

**M2 — headless Mac game.** `MacGameEngine` boots, mounts `.big` files, runs a skirmish under
*First slice REACHED 2026-09-26 (C2, `feature/mac-port-C2-w3d`):* `generals -headless` on macOS arm64,
rooted at a read-only symlink farm of the install (rule 9), mounted every archive, generated a random
map, played a two-slot skirmish to 600 frames at 9.5x real time, wrote a replay and exited 0. A second
run with the same seed gave the same HEADLESS CRC (0x78BEA937). Still open for M2: E1's harness,
which checks record/playback, and any comparison with a Windows-recorded replay, which needs a Windows
build (E4).
`-headless`, and its replay checksum matches the Windows build's on the same seed. Playable by a
machine, not by a person.
→ C1 C2 C5

**M3 — renderer funnel.** Done on Windows, where the picture can still be A/B'd against the
existing D3D11 path. Every D3D call goes through `DX8Wrapper`; the backend interface is abstract;
the shader generators emit an IR rather than HLSL text.
→ D1 D2 D3

**M4 — it draws.** The SDL3 GPU backend (Metal underneath on macOS, Vulkan on Linux), textures,
the window. The game is visible on a Mac, and on Linux from the same code.
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
| A1 | [CMake toolchain split](tasks/A1-cmake-toolchain-split.md) | M1 | — | done: configure on arm64; the four libraries its first acceptance named now build (was -95) | |
| A2 | [POSIX vendor script](tasks/A2-vendor-posix.md) | M1 | — | done: merged, including the zlib reopen (was -21) | |
| A3 | [build.sh](tasks/A3-build-sh.md) | M1 | A2 | done: merged with A2 (`build.sh`) (was -21) | |
| B1 | [WideChar to char16_t](tasks/B1-widechar-char16.md) | M1 | A1 | done on macOS: flip merged, `.csf` test (`gametext_csf`) on `feature/mac-port-B1-csf`; Windows half is debt | -47 |
| B2 | [Time shim](tasks/B2-time-shim.md) | M1 | A1 | done: merged; not verified on Windows (was -8d) | |
| B3 | [CRT and string shims](tasks/B3-crt-shims.md) | M1 | A1 | done: merged; later CRT spellings land in `MSVCCompat.h` as found (was -95) | |
| B4 | [Pragma audit](tasks/B4-pragma-audit.md) | M1 | A1 **B1** | recon done, waits on B1 |  |
| B5 | [Win32 scalar types](tasks/B5-win32-types.md) | M1 | A1 | in progress: `GameEngine` half on `feature/mac-port-B5-gameengine`; the `wwlib` pass is done (-a9) | -18 |
| B6 | [Trim the gameengine link surface](tasks/B6-gameengine-link-surface.md) | M1 | B1 B2 B3 B4 B5 B7 B8 | done on macOS: `test_gameengine` passes under ctest (443 tests); Windows is debt | -47 |
| B7 | [W3D file format layout asserts](tasks/B7-w3d-layout-asserts.md) — **see [findings](B7-w3d-layout.md)** | M1 | A1 | done: merged with B10 (was -14) | |
| B8 | [JobSystem thread pool](tasks/B8-jobsystem-threads.md) | M1 | A1 | done: merged (was -83) | |
| B9 | [Backslash include paths](tasks/B9-backslash-includes.md) | M1 | — | done: merged (was -21) | |
| B10 | [bittype.h integer widths](tasks/B10-bittype-widths.md) | M1 | A1 | done: merged; not verified on Windows (was -14) | |
| B11 | [CriticalSection](tasks/B11-criticalsection.md) | M1 | A1 | done: merged (was -83) | |
| B13 | [AsciiString's refcount](tasks/B13-asciistring-refcount.md) | M1 | A1 | done: merged (was -83) | |
| B14 | [WWVegas' threading primitives](tasks/B14-wwvegas-threading.md) | M1 | A1 | done: merged; not verified on Windows (was -8d) | |
| B15 | [Remove the wide-format %ls](tasks/B15-wide-format-removal.md) | M1 | — | superseded for correctness by B1's funnel (2026-09-26); remaining removals optional | |
| B16 | [wwdebug's Windows dependency](tasks/B16-wwdebug-windows.md) | M1 | — | **done** — wwdebug 3/3, wwmath 36/36; not verified on Windows | -18 |
| B17 | [D3DX maths on the CRC path](tasks/B17-d3dx-math-on-the-crc-path.md) — **see defect #7** | M1 | A1 | done: macOS half and the Windows flip (decision 1) merged; not verified on Windows | -47 |
| B19 | [CPU detection and the tick clock on arm64](tasks/B19-cpu-detection-arm64.md) | M1 | B5 | done: merged; tier decided (option (c), decision 2); verifiable once `gameengine` compiles | -a9 |
| B12 | [SSE2 in WWMath and Float_To_Long](tasks/B12-simd-float-to-long.md) | M1 | A1 | done: `Float_To_Long` goes through `Lib/DetRound.h`, no SSE2 header left in WWMath (was -21) | |
| E3 | [x86_64/arm64 differential harness](tasks/E3-arch-differential-harness.md) | M1 | A1 | done: merged (was -21) | |
| E4 | [Windows under CrossOver](tasks/E4-windows-under-crossover.md) | M1 | — | blocked: no game executable for stage 1 (release channel unpublished); stage 2 needs the user to accept Microsoft's licence | -a9 |
| T1 | [Simulation terrain out of W3DDevice](tasks/T1-simulation-terrain.md) | M2 | — | done: T1a (height data and maths in gameengine, three-way golden) and T1c (defect 17 fixed) merged; T1b and T1d dropped by decision 8 | -47 |
| C1 | [MacGameEngine and file systems](tasks/C1-mac-game-engine.md) | M2 | B6 | done: path resolver, POSIX local and BIG file systems, file operations, user-data dir, replay stream, PosixGameEngine (abstract until T1); `test_bigfilesystem` byte-identical over 25,293 files | -a9 |
| C2 | [Entry point](tasks/C2-entry-point.md) | M2 | C1 | claimed | -18 |
| C3 | [Input](tasks/C3-input.md) | M4 | C2 D4 | C3a merged; C3b (the game makes the SDL input via W3DGameClient) on its branch | -47 |
| C4 | [Audio](tasks/C4-audio.md) | M5 | C2 | done on macOS: lower half (the Miles API on miniaudio, -a9) and upper half (MilesAudioManager, -47) merged; not yet heard by a person | -a9, -47 |
| C5 | [Crash reporting](tasks/C5-crash-reporting.md) | M2 | B6 | done: merged; macOS arm64 measured, x86_64 and Linux written but unexecuted | -18 |
| D1 | [Finish the DX8Wrapper funnel](tasks/D1-dx8wrapper-funnel.md) | M3 | — | in progress: recon and PR1 merged; PRs 2-8 need Windows (was -8d) | |
| D2 | [Abstract the backend interface](tasks/D2-backend-interface.md) | M3 | D1 | not started | |
| D3 | [Shader generators target SDL3 GPU](tasks/D3-shader-generators-ir.md) (decision 4) | M3 | — | done: SDL3 target, compile seam, POSIX twin test and D4's contract; 49/49 on Metal and Vulkan (-a9) | |
| D4 | [SDL3 GPU backend](tasks/D4-metal-backend.md) (file keeps its old name) | M4 | D3 | not started | |
| D5 | [Texture formats](tasks/D5-texture-formats.md) | M4 | D4 | not started | |
| D6 | [Text rasterisation off Windows](tasks/D6-text-rasterisation.md) (decision 6) | M4 | — | D6a merged; D6b (FontCharsClass on FreeType) on its branch | -47 |
| D-spike | [One real model through SDL3 GPU](tasks/D-spike-sdl3-gpu-model.md) | M4 | — | done — merged; the Crusader on Metal and on Vulkan (lavapipe); D3's route taken as decision 4 | -a9 |
| V1 | [Video playback off Windows](tasks/V1-video-playback.md) | M4 | A1 | done on macOS: the Bink player and FFmpeg 8.1.2 (built from its tarball, static, LGPL) decode all 70 install movies against their headers and a golden; sound through C4's mix; not yet on screen (A3) | -47 |
| A3b | [FFReference](tasks/A-posix-d3d9-device.md) (the renderer's phase A3b, not the build.sh A3 above) | M4 | A3a | done: FFReference, independent, 29 tests / 280 checks; harness is -a9's | -47 |
| E1 | [Determinism gate](tasks/E1-determinism-gate.md) — **degraded, see note** | M1 | B6 | in progress: POSIX harness (`replay-check.sh`), the Mac baseline over a real fight, defect #20 fixed; parity needs a Windows run | -18 |
| N1 | [Cross-platform build fingerprint for the compatibility CRC](tasks/N1-build-fingerprint.md) (decision 5) | M5 | — | not started | |
| P1 | [Packaging the macOS app](tasks/P1-macos-packaging.md) | M5 | C1 C2 V1 (E1) | in progress: design approved; step 1 (the overlay as a first read root, `-overlay`, CRCs identical to one folder) on its branch; the `.app` held (disk) | -47 |
| E2 | [CI matrix](tasks/E2-ci-matrix.md) | M5 | E1 | not started | |

Status is one of: `not started`, `claimed`, `in progress`, `in review`, `done`, `blocked: <why>`.

### Known gaps, not on a milestone's critical path

- **The app icon needs a 1024 px master (P1, 2026-09-26).** `Main/Generals.ico` tops out at 48 px, and
  a Retina Dock icon is built from 1024 px. Until someone who owns the fork's artwork supplies one, the
  `.icns` comes from the 48 px image and looks soft. Owner: the fork's art, not a port task.
- **`-nodevice` (`m_noRenderDevice`) does not survive a map yet (C2's read, 2026-09-26).**
  - **Why it is not M2's blocker:** it is the device-less path, which is not `-headless`. Windows'
    `-headless` makes a real device and skips only the draw. POSIX `-headless` makes decision 7's
    CPU-backed device with no window (decision 8, refined), so M2 does not run this path.
  - **Background:** CommandLine.cpp:1859-1868 says a match "does not survive map load yet".
    Under `-nodevice`, `DX8Wrapper::Compute_Caps` never runs, so `Get_Current_Caps()` is NULL, and
    `_Get_D3D_Device()` is NULL (citations are GeneralsMD/Code on 2026-09-26's tip).
  - **Crash sites a `-nodevice` run still reaches:**
    - Sized textures (terrain at map load: WorldHeightMap.cpp:1939/1954/1961, BaseHeightMap.cpp:1913)
      go through `_Create_DX8_Texture` into `D3DXCreateTexture` with a NULL device
      (dx8wrapper.cpp:3070). Whether Microsoft's d3dx9_43 fails cleanly there is unverified.
    - `_Create_DX8_ZTexture` calls the device directly (dx8wrapper.cpp:3211).
    - TextureClass's bump-format constructor reads the caps (texture.cpp:1249-1256).
    - `W3DSnowManager` reads the caps on a map that enables snow (W3DSnow.cpp:64).
    - The dynamic vertex and index buffers read the caps, on the draw path only
      (dx8vertexbuffer.cpp:867, dx8indexbuffer.cpp:577).
    - `createVideoBuffer` (W3DDisplay.cpp:3411) is reached under `-nodevice` without `-headless`
      when movies play.
    - An INI `ChipsetType` of 6 or more builds shaders on the NULL device (W3DTreeBuffer).
    - W3DRadar is avoided: `-nodevice` takes HeadlessRadar (Win32GameEngine.h:107-114).
- **Map paths are lowercased whole, user-data prefix included (C2's first headless run, 2026-09-26).**
  A Linux-constraint item: on macOS it is harmless, because APFS is case-insensitive by default.
  - **What happens:** the random map's path is built from `TheGlobalData->getPath_UserData()` and then
    lowercased entirely (RandomMapGenerator.cpp:4867, in `generatedMapPathsFor`), so the log shows
    `/private/tmp/.../-users-ilyasakin-...`. MapUtil lowercases map directories and names the same way
    (MapUtil.cpp:432, 533, 626, 724, 922).
  - **Why it matters:** on a case-sensitive volume (Linux's default, or a case-sensitive APFS), a user
    data folder with a capital letter in its path gives a map path that does not exist.
  - **Not yet measured:** whether a run on a case-sensitive volume then fails to load the map or merely
    fails to cache it. PosixLocalFileSystem's case-insensitive lookup (C1) may or may not cover the
    prefix.

### Decisions taken, 2026-09-25

The user delegated every decision on this port. These were taken by the PM session, with the
reasoning written here so they can be revisited rather than re-argued.

**1. Windows routes the simulation's D3DX maths through the portable implementation too (defect #7).**
Chosen because it is correct *whichever way the unobserved fact turns out*, not because the fact
was confirmed. The oracle (`d3dx_oracle`, run against Microsoft's real `d3dx9_43.dll`) measured that
`d3dxportable.h` is bit-identical to the DLL's scalar and non-Intel bodies — 0 of 1,000,000 inputs
differ. So:
- on a Windows machine that runs the scalar or AMD body, the switch changes **nothing**;
- on one that runs the Intel body, it makes that machine agree with every other.

The dispatch — which CPU gets which body — is still read from disassembly, not observed. But the
choice no longer depends on it: routing Windows through the portable code is a no-op if the reading
is wrong and a desync fix if it is right. The one cost, Intel-recorded replays' checksums moving,
arises only in the world where the desync is real, where those replays already diverge on AMD.
This matches the project's own precedent, `215f84a5`, which took the CRT's per-CPU `log()` choice
away from every machine. What remains unverified is that it **builds** under MSVC; that is a
`WINDOWS-DEBT.md` row, not a reason to wait.

**2. An unmeasurable CPU is treated as fast when choosing the default preset — option (c) of B19.**
Apple Silicon publishes no clock rate. `CPUDETECT_UNMEASURED_PROCESSOR_MHZ` stays `0`, which is
what the measurement class honestly knows; `testMinimumRequirements` treats an unknown CPU as meeting
the top preset. Rejected: (a) leaving it, because first and later launches disagree and the animated
menu background runs once then never; (b) a nominal MHz, because it puts a chosen number in a class
whose job is measurement; (d) a named arm64 CPU type, as more code for the same result. Every Apple
Silicon Mac exceeds a 2003 game's requirements by orders of magnitude, so "top tier" is not in
doubt — only where to say it. Verifiable once `gameengine` compiles, which is B5's GameEngine half.

**3. Linux is a target, and the non-Windows platform layer is chosen for it (taken 2026-09-25).**
The user asked for everything to be abstracted so that Linux and further platforms can follow. What
that decides, task by task:

- **Build (done, `f25b7395`).** CMake knows `ZH_PLATFORM_WINDOWS` and `ZH_PLATFORM_POSIX`, with
  `ZH_PLATFORM_MACOS` and `ZH_PLATFORM_LINUX` only as refinements. The determinism flags
  (`-ffp-contract=off -fsigned-char -fno-strict-aliasing`) key on the *compiler*, Clang or GNU, and
  an unknown compiler is a configure error. Before that commit they sat under `macOS`, and a Linux
  build would have got GCC's default `-ffp-contract=fast` on the CRC path and aarch64's unsigned
  plain `char` — a desync nobody would have seen until a replay diverged.
- **Window, events, timers, entry point — C2, C3: SDL3.** One dependency serves both platforms and is
  the ordinary answer for a C++ game leaving Win32. Rejected: Cocoa plus a separate X11/Wayland
  layer, which is two implementations of the same thing, and GLFW, which has no GPU API (below) and
  no text-input model worth the name. Windows keeps `Win32Device`; nothing here changes the Windows
  build.
- **Audio — C4: miniaudio**, vendored like the other single-file libraries. It is the only
  candidate that brings mixing, 3D panning and MP3/WAV decoding together, which is what
  `MilesAudioManager` asks of the layer beneath it. Rejected: `AVAudioEngine` (macOS only),
  OpenAL Soft (LGPL, and decoding is still ours), and SDL3's audio (a device and a stream, nothing
  above that).
- **Renderer — D2, D4: SDL3's GPU API.** It is Metal on macOS and Vulkan on Linux underneath, so
  it is one backend with a native API under it on each platform, and its window already comes
  from the SDL3 chosen above. Its model — pipeline objects built from state, resolved at draw time —
  is the shape `dx11backend.cpp` already has, which is why D2 generalises that file. Rejected:
  native Metal plus a later Vulkan backend (two backends to keep in step), and Vulkan with MoltenVK
  on macOS (one backend, but a translation layer on the primary platform and much more boilerplate).
  **Fallback, decided now so nobody re-argues it:** if D2 finds something the game needs that SDL3's
  GPU API cannot express, the second choice is one Vulkan backend with MoltenVK on macOS. D2 records
  the gap that forced it. D2's interface stays abstract either way, so a native backend is never
  shut out. The shader question (what D3's IR emits: MSL and SPIR-V, or SPIR-V alone with
  SDL_shadercross converting) belongs to D3 and gets written down there.
- **Verification.** Linux is checkable on this machine, unlike Windows: a Linux container (OrbStack)
  builds the tree with GCC and Clang and runs `ctest`. **Revised 2026-09-26, at the user's
  direction: macOS is the target to get working, and Linux is a constraint on how the code is
  written, not a gate on each merge.** Branches merge on macOS evidence. The layering rules above
  still bind every change: POSIX first, Darwin only as a refinement, `#error` for anything unported.
  `linux-check.sh` runs at milestone boundaries, or when a change is specifically about
  portability, and not per branch. Per-branch runs had cost hours of wall-clock time and once
  filled the disk. Its first run found three defects the macOS build had hidden. `uintptr_t` was used
  without `<stdint.h>`, because Apple's headers include it transitively and glibc's do not.
  `always.h` declares `operator delete` without the `noexcept` the standard gives it, which clang
  forgives and GCC rejects. And `cpudetect.cpp` has no Linux answer, which failed at its `#error`
  exactly as intended.
  **The check is `GeneralsMD/Code/Tools/linux-check.sh`**: {gcc, clang} × {linux/arm64, linux/amd64},
  configure, build and `ctest`, one table, and a nonzero exit on any failure, a missing tool or an
  empty tree. It builds from a case-sensitive copy of git's own file names, because the OrbStack
  mount folds case. That copy is what found the largest defect class of the three: 298 includes
  spelled in a case other than their file's. `include_case_check` in `ctest` now catches that class
  on a Mac.

**4. Shaders stay HLSL; SPIR-V and MSL are derived at run time (taken 2026-09-25, from the D-spike).**
D3 was going to make the shader generators emit an IR with an MSL emitter beside the HLSL one. The
D-spike measured the alternative on the game's own shader text — all 49 programs the generators
produce, captured by running the unmodified `ffshader.cpp`, `ffvertex.cpp` and `engineshader.cpp`
— and it is better: **HLSL → glslang's HLSL front end → SPIR-V (Vulkan) → SDL_shadercross built
without DXC (SPIRV-Cross) → MSL (Metal)**. 49 of 49 compile, pass `spirv-val`, and are accepted by
Metal. The route through DXC was rejected on footprint: it costs 21 MB of runtime and 150 MB of
source against ~5 MB of runtime and ~10 MB vendored for the chosen three. *Corrected 2026-09-26:* the
record first also said DXC failed on the evidence, because Metal refused 3 of 49 (the bumped-terrain
programs), blamed on DXC stripping unused textures. D3 found that glslang strips them too, and the
real cause is SDL_shadercross's texture/sampler pairing, which gives a texture read through another
slot's sampler no MSL index of its own. It is the same for both compilers, and it is fixed on our side
by the SDL3 target (one sampler per texture slot, slot counts from the SPIR-V; see
`tasks/D3-shader-generators-ir.md`). So DXC would probably pass those 3 today. That is unmeasured,
and it leaves footprint as the whole reason.
So the generators keep one language; D3 shrinks to a target flag (SDL's register spaces and a BGRA
vertex-colour swizzle) and a POSIX twin of `test_ffshadercompile`. The byte-identical-HLSL gate for
the D3D11 path stands, so Windows is untouched. Driver compiles cost 50–225 ms per program on first
use, so programs are compiled ahead of need. Proven to *draw*, not only compile, by
`Tests/w3d_view` (screenshot: `d-spike-crusader-metal.png`); the 49 programs themselves are proven
to compile and validate, not yet to draw. Evidence and tables: `tasks/D3-shader-generators-ir.md`.

*Known risk, recorded 2026-09-26:* glslang has deprecated its HLSL front end (glslang issue #4210,
opened 2026-04-06). It goes at the next major version, with at least 18 months' notice, so not before
about October 2027. We vendor a pinned commit, and what it compiles is our own generated text, not
untrusted input, so the stated security reason barely applies. What we lose is upstream fixes.
**Exit, if we need one:** DXC through the same shadercross call. With the SDL3 target's pairing fix
the 3-program failure is not expected to recur, so the exit is a footprint cost rather than a
correctness one (unmeasured). Slang is the other named route, also unmeasured. Only one compile call is
specific to glslang, and D3's ctest twin runs every generated program through whichever front end is
in use, so a switch is caught by the tests rather than in the game. Revisit by 2027-04, or at the
first glslang release that removes the front end, whichever comes first.

**5. The multiplayer compatibility CRC stops hashing the executable (taken 2026-09-26).**
`GlobalData.cpp` builds `m_exeCRC` from the running executable's own bytes, the version number and
the two multiplayer script files. LAN and GameSpy matchmaking compare it to decide who can play
together, and the replay header stores it to warn on playback. A Mac or Linux binary can never have
`generals.exe`'s bytes, so as written, cross-platform play is impossible by construction.
Decided: the executable-bytes term is replaced, **on every platform**, by a *build fingerprint*.
That is a hash over the content of every tracked source file under `GeneralsMD/Code` (the whole
tree, not a per-platform subset), generated at build time into a header. The version and script
terms stay. Two builds of the same source then agree whatever compiled them, and any source change
still separates builds, which is what the executable CRC was for ("the game will go out of sync if
they change"). What it gives up: detecting a binary modified after the build. The source is GPL,
so that detection protected nothing. What it changes on Windows: the `m_exeCRC` value, which
already changes with every rebuild, so no compatibility is lost that a rebuild would not already
lose. Until it lands, the POSIX build hashes version and scripts only, which can match no Windows
build, and says so where it is computed. Task: `tasks/N1-build-fingerprint.md`.

**6. Text is rasterised with FreeType off Windows (taken 2026-09-26; task D6).**
The plan had no task for text. `WW3D2/render2dsentence.cpp` draws every glyph through GDI
(`CreateFont`, `ExtTextOut` into a DIB section, then copies it into a texture), and
`GlobalLanguage.cpp` installs the language's own font files with `AddFontResource`. Found by B5,
which hit `AddFontResource` in the sweep. The game names "Arial" in 319 places and "Times New
Roman" in 51, plus its own "Generals" font. Decided: **FreeType**, vendored like the other
libraries (FTL licence, GPL-compatible), behind a glyph-rasteriser seam whose Windows body is
today's GDI code, unchanged. Rejected: stb_truetype, whose unhinted output at the game's small UI
sizes would look worse than the Windows build. Fonts: the game's own files load from its data, as
`AddFontResource` does. The system fonts go through a substitution table, not a bundled copy of
Microsoft's fonts: Arial and Times New Roman are present on macOS, and on Linux they map to the
metric-compatible Liberation Sans and Serif (OFL). Metric compatibility matters because text width
decides where the UI wraps lines. Text is display only and never reaches the simulation, so
mismatched glyph metrics are a cosmetic risk, not a determinism one.

**7. The renderer reaches POSIX through a D3D9-shaped device, and Windows keeps its own (taken
2026-09-26, from `RENDERER-ROUTE-RECON.md`).** D1's funnel stalled because its call-site moves
change Windows rendering and need a Windows A/B that nobody here can run. The recon measured why
the old sequence (D1 → D2 → D4) cannot work without one. The engine does not just CALL Direct3D, it
SPEAKS it: 477 D3D names and ~5,000 uses outside the wrapper, 3,778 of them fixed-function constants.
`dx11backend` also MIRRORS a real D3D9 device rather than replacing one, so off Windows there is
nothing for it to mirror. Decided: **route A.** On POSIX only, WW3D2 and W3DDevice compile against a
D3D9-shaped device (~95 methods) whose resources are CPU-backed, and whose draws resolve at draw time
into cached SDL3 GPU pipelines. That is `dx11backend`'s design on a new base, with D3's generators
producing the shaders. Calls that escape the wrapper need no funnel, because they reach this device
directly. Windows keeps D3D9 and the D3D11 mirror, untouched. Phases: **A0** (the only
Windows-visible one) fixes three header lines and removes Win32 scalar types from WW3D2 and
W3DDevice, B5's way; **A1** a POSIX-only header of the D3D9 names and interfaces, with loud-failing
bodies, so both libraries compile and link; **A2** CPU-backed resources: every texture of the
install loads and reads back; **A3** the draw, in frames. About 7-8k new POSIX-only lines.
Rejected: finishing D1 on POSIX (it still needs A0-A2, and its site moves are the Windows A/B that
stalled it), and a new renderer interface under WW3D2 (a rewrite of all 5,000 uses, visible on
Windows throughout). D1 PRs 2-8 are deferred until a Windows machine exists. D2's "interface" is
the D3D9-shaped device itself, and D4 is A3.
- **Names.** This is a deliberate exception to "engine-own names" (FF_*, MSGBOX_*): on POSIX we
  implement the D3D9 device the renderer already speaks, so its names ARE the interface. They go in
  ONE POSIX-only header that `#error`s on Windows, written from the published values (never copied
  from Wine's LGPL headers), static_asserted against mingw-w64's headers in a checker TU, as B5 did
  for the DIK codes, and reviewed under the second-reader rule. It contains no Win32 scalar types:
  A0 removes DWORD, HRESULT and HWND from the renderer first.
- **Reference images.** There is no Windows frame to compare against. Correctness is checked per
  draw on the CPU against the fixed-function formulas, as D3's generator tests do. A visual reference
  can come later from the retail game under CrossOver, run on a COPY of the install (rule 9 applies
  there too: the INIZH.big delete is EA's code).

**8. The POSIX engine uses the W3D factories, as Windows' own headless mode does (taken
2026-09-26).** C2's first `generals -headless` run on macOS got through the file systems, the archive
mount and the first INIs, then stopped at the first factory that only W3DDevice provides
(FunctionLexicon, then ModuleFactory, ThingFactory, GameClient, ParticleSystemManager, Radar,
GameLogic). On Windows, `-headless` does NOT swap those out: it keeps the W3D classes and skips only
the frame. Decision 7 already builds WW3D2 and W3DDevice on POSIX (phase A1's checkpoint), so the POSIX
engine's factories return the same W3D classes Windows uses, and a headless run has no video device
under them. Rejected: a separate set of headless counterparts (null draw modules, a headless
ThingFactory and so on). They would be a second copy of the INI-facing surface, and every difference
from W3D's is a place for a replay to diverge. This supersedes C1's proposed "headless module factory"
piece: W3DModuleFactory registers its own 19 draw modules. T1 is unaffected, because its extraction
and height golden make the simulation's terrain independent of the render object on every platform,
which is worth having whichever class answers. M2's first slice therefore waits on A1 (W3DDevice
links) as well as on T1.

*Refined 2026-09-26: what `-headless` means off Windows.* On Windows, `-headless` is NOT device-less.
`W3DDisplay` still creates a real D3D9 device on a hidden 100x100 window, and only drawing is skipped.
The device-less path is a separate flag, `-nodevice`, which its own comment says "does not survive map
load yet" (`CommandLine.cpp:1859`). Decided: **POSIX `-headless` creates decision 7's device with no
window and no GPU**. Its resources are CPU-backed (phase A2), so it needs neither a display nor D4's
draw. The W3D classes then see a device, exactly as under Windows' `-headless`, which is what
`replay-check.ps1` runs, so E1 compares like with like. Rejected: making POSIX `-headless` mean
`-nodevice`, which would put that flag's unfinished map-load path on M2's critical path, and match a
Windows mode nobody records replays in; and a hidden SDL window with a GPU device, which contradicts
"headless needs no display" and waits on D4. Consequence: M2's gate is A1 (W3DDevice links) plus A2
(CPU-backed resources), with no A3.

**9. The fork's own data is an overlay the game mounts, never something written into the player's
install (taken 2026-09-26).** On Windows the build copies `Code/Data`'s masters (the fork's INIs,
`Patch.str`, scripts, textures, windows, the splash) into `Run/`, the game folder, and the game reads
them from there. The first macOS headless run stopped at the fork's own `FXListReforged.ini` for want
of that step. Off Windows the player's install is a folder the game must never write to (rule 9's
reasoning, applied to shipping). Decided: the fork's data ships beside the executable (inside the app
bundle on macOS, in the package on Linux), and the local file system searches it BEFORE the install
root. This reproduces Windows' result (the fork's files win over the retail ones) without touching
the install. Until packaging (M5) implements that search order, E1's harness assembles a symlink farm
with the overlay copied over it, as C2's first run did. Owner of the implementation: packaging (E2/M5)
with C1's file system.

### Rule: a project-wide definition in front of an uncompilable header needs a second reader

Added 2026-09-22 after two Windows-only breaks in one afternoon, both with the same shape and
**both invisible to every check available on this machine**:

- `UINT32=unsigned int` as a compile definition, in front of `windef.h`, where `UINT32` is an SDK
  *typedef* — so `basetsd.h` would have preprocessed to `typedef unsigned int unsigned int`.
- A shim header that rewrote `_stricmp` into a call to itself.

Neither was caught by a compiler or a test. The first was caught by another agent reading the diff;
the second by its author re-reading their own script. Both were a **project-wide definition placed
in front of a header nobody here can compile**.

So: any change that defines, redefines or shims a name the Windows SDK also owns — `UINT32`,
`DWORD`, `_stricmp`, `__int64`, anything in that family — gets a second pair of eyes on the diff
before merge, and the reviewer's job is specifically to ask *what does this do to the SDK header
that includes it*. Until E2 exists, that review is the only Windows check this project has.

### Rule: when you write a check, say what it cannot see

Added 2026-09-22 after **four** instances in one day, each found only because something else caught
it. The shape: *a check that shares the property it is checking, so it agrees with the code under
test while both differ from the truth.*

| Check | Shared the property | Found by |
|:--|:--|:--|
| `DetRound`'s 400,000-value sweep | referenced the C library, which has the same 64-bit `long` and the same saturation as the arm64 code under test | E3, running the real x86 instruction |
| E3's own probe | varies architecture while holding OS and **endianness** fixed, so a byte-order probe would agree with itself | its author, refusing to add one |
| `crcengine_byte_at_a_time_matches_block` | both sides move together, so it passes at either accumulator width | rewriting `crc.h` |
| `persist_object_round_trips_through_its_factory` | checks the object's data, not the identity token the round trip exists to preserve — passed through the entire x64 port while the token was lost | measuring the chunk payload |
| A root-level `grep -r` | (tooling) `ugrep` honours `.gitignore` by default and skips **466 tracked files**, including 54 `.cpp`/`.h` in two **built** libraries | an explicit-directory sweep disagreeing with it |

So: every test and every sweep gets a sentence saying what it does **not** establish. E3's header
does this and is the model. An absolute expected value beats a self-consistency check wherever one
can be derived — `crc.h`'s new test asserts `0x93b0f838` for "Westwood Studios" rather than that
two code paths agree.

**Tooling corollary**, measured on this machine: `grep -rln 'DEBUG' .` finds **0** files under
`Libraries/Source/debug/`; `grep -rln --no-ignore-files 'DEBUG' .` finds **13**; and
`git check-ignore` says git does not ignore them. Naming the directory explicitly is also safe.
Recurse from the root and you silently miss two built libraries. Use `--no-ignore-files`, or drive
the file list from `git ls-files`.

**Two more, from B17, both about protections that look stronger than they are:**

- **`#pragma clang fp contract(off)` does not hold against `-ffp-contract=fast`** — clang documents
  that `fast` overrides it. Under clang's default and under `on` it holds (a control expression
  fused 12 times, the guarded function 0). So the pragma is a second line of defence, not a
  substitute: **the build flag remains the real protection.**
- **A differential probe over an input the compiler can see measures the compiler, not the
  hardware.** E3 hid a NaN-sign difference twice through constant folding — first a `const` input,
  then a `static` nothing writes. Both let the compiler fold the expression at build time, so both
  architectures ran identical constants and "agreed". Probe inputs must be opaque to the optimiser.

### Rule: never define `_UNIX`

It will look free, and it is the most expensive thing in this tree.

`_UNIX` appears at roughly sixty sites across WWVegas — twenty in `rawfile.cpp` alone, plus
`cpudetect.cpp`, `data.cpp`, `ini.cpp`, `hash.cpp`, `matrix3d.h`, `vector3.h`, `udp.h`,
`widestring.h`, `wwstring.h`. It is Westwood's own never-finished UNIX port, and defining it would
make a large part of `wwlib` compile at once.

Two of those arms have now been read, and the tree records what they were:

- `mutex.cpp` — the `#ifdef _UNIX` arms "were not a port; they were a hole": locks that took
  nothing and answered success, constructors that built nothing.
- `thread.cpp:201` — its `_UNIX` branch "used to return 0 from here for every thread", which makes
  every main-thread assertion in `TextureLoader` and `DX8_THREAD_ASSERT` pass from anywhere.

Those two have been replaced. **The other fifty-eight have not been read.** Defining `_UNIX` trades
compile errors for silent misbehaviour, which is exactly what B5's task file forbids for a
`WinTypes.h`. Port the file in front of you; do not switch on someone's abandoned 1990s branch.

**The same goes for a compiler's MSVC mode.** `-fms-compatibility` would accept GameEngine's
forward-declared enums, the one construct behind every one of its 602 files failing, and it was
rejected by measurement, not taste (B5, 2026-09-25): on `GameLOD.cpp` it **doubled** the errors,
every new one inside Apple's libc++ (`<__locale>`, `char_traits.h`, `<string>`, `<string_view>`).
MSVC-compatibility mode breaks the platform's own standard library. It is also a compiler-wide
switch that changes name lookup and template parsing to hide one construct, and GCC has no
equivalent, so a Linux build could not follow. Fix the construct: the enums got an explicit `: Int`.

### Rule: the only locale category the game may set is `LC_TIME`

Added 2026-09-25 with the POSIX date and time formatting (B5). This tree assumes the "C" locale
everywhere except in how a date is shown, and on Windows it gets that for free. On POSIX, a
process's locale is whatever the entry point asks for, so this is written down before C2 writes one.

- **`setlocale(LC_TIME, "")` is allowed, and nothing broader.** Never `LC_ALL`, `LC_NUMERIC` or
  `LC_CTYPE`.
- **`LC_NUMERIC` decides how `strtod`, `atof`, `sscanf` and `printf` read and write a decimal.** On a
  comma-decimal locale the INI parser (`INI.cpp:1628`, `:1637`: `sscanf(token, "%f", ...)`) would read
  `1.5` as `1`, report success and carry on, which is a determinism
  bug keyed on the user's region settings. It would not be a crash.
- **`LC_CTYPE` is what `_strlwr` and `_strupr` in `MSVCCompat.h` assume is "C"**. They
  build hash keys and sort orders that have to match across machines. (`_wcsicmp` was here too;
  since B1 `UnicodeString::compareNoCase` is `WideCharICmp`, which folds ASCII whatever the locale.)
- **`WideCharIsSpace` and its neighbours in `WideCharFns.cpp` are the C library's `isw*`, and
  already differ between Windows and POSIX in the "C" locale.** Measured on macOS: `iswspace` is
  true for tab, newline and space and false for U+00A0, U+2000-U+200A, U+2028 and U+3000. MSVC's
  classifies characters above U+00FF from Windows' own Unicode tables rather than the locale, so it
  is expected to call most of those spaces - expected, not measured: there is no Windows here. `GameText.cpp` trims `.str` text with it, so a
  string file with a non-breaking or ideographic space at an edge loads differently. Not
  measured on glibc. Pre-existing, not B1's; recorded so nobody widens `LC_CTYPE` to "fix" it.
- **Text that comes back from an `LC_TIME` call is decoded as UTF-8 explicitly**
  (`WideCharFromUtf8`), never with `mbstowcs`, which decodes by `LC_CTYPE`.

### Rule: grep the vendored sources for platform predicates

Added 2026-09-22 after three instances in one afternoon, all the same shape — **a platform
predicate that was correct when it was written and is silently wrong now** — and all three in
**vendored third-party code, which is the code nobody reviews**:

| Where | The predicate | Why it is wrong now |
|:--|:--|:--|
| zlib `zconf.h` | `#if !defined(MACOS) && !defined(TARGET_OS_MAC)` around the `Byte` typedef | `TARGET_OS_MAC` arrives transitively; under `Z_PREFIX` nothing stands in |
| `gimex.h` | `#if defined(__APPLE__)` → native load of a big-endian field | 2003 shorthand for PowerPC. Apple Silicon is little-endian, so every RefPack header field was byte-swapped |
| LZH-Light `_lzhl.h` | `#define UINT32 unsigned long` | 64 bits on LP64; the hash index runs off its table. SIGBUS on the first buffer |

**None was found by reading this plan. Each was found by a compiler or a round-trip failing.** Two
of the three corrupt data rather than fail to build, and both reach the bytes that save games and
network packets are made of.

So: before assuming a vendored library ports cleanly, grep it for `__APPLE__`, `MACOS`,
`TARGET_OS_*`, `_WIN32`, `__BIG_ENDIAN__`, **`BIG_ENDIAN`/`LITTLE_ENDIAN`/`BYTE_ORDER`**,
**`__i386__`/`_M_IX86`**, `unsigned long` and `#ifdef` around type definitions. Treat every one as a
claim about 2003 hardware until checked.

**`BIG_ENDIAN`, `LITTLE_ENDIAN` and `BYTE_ORDER` were added on 2026-09-25**, by a sixth instance, in
**our own code rather than vendored code**: `WWLib/fixed.h:207` and `WWLib/base64.cpp:90`/`:102` chose
their byte layout with `#ifdef BIG_ENDIAN`. On POSIX systems that is not a flag but the *name of a
byte order*, defined unconditionally beside `LITTLE_ENDIAN` and `BYTE_ORDER`. Darwin's
`<machine/endian.h>` always defines it, and glibc's `<endian.h>` does under `_DEFAULT_SOURCE`. So
every little-endian Mac and Linux machine took the big-endian branch. Measured in a wwlib
translation unit: `BIG_ENDIAN` 4321, `BYTE_ORDER` 1234. `test_wwlib`'s published RFC 4648 vectors
and the `fixed` tests caught it. The list's `__BIG_ENDIAN__` could not have: a grep for it does not
match the bare name. The fix is `WWLib/wwendian.h`'s `WW_BIG_ENDIAN`, from the compiler's own
`__BYTE_ORDER__`, tested with `#if`. **Test a byte-order macro's value, never whether it is defined.**

**`__i386__` was added to that list on 2026-09-22**, by a fifth instance that the list as it stood
could not have found: `gimex.h:99` selected the `ARGB` channel order with
`#if defined(_MSC_VER) || defined(__i386__)`, meaning "little-endian desktop", and Apple Silicon
fell through it to the GameCube/Mac big-endian order. A 2003 "which machine am I?" test is written
as a positive CPU test at least as often as a negative platform one.

**A sixth, 2026-09-25: the undefined macro that reads as zero.** `GameMemory.h` guarded placement
array new/delete with `#if _MSC_VER < 1300`, meaning "a compiler older than VC7". Off MSVC,
`_MSC_VER` is undefined, the preprocessor reads it as `0`, and `0 < 1300` is true. So every
non-MSVC compiler took the branch meant for VC6, and redefined two operators every standard
`<new>` already has. `always.h` had carried the same guard and been corrected earlier; its twin had
not. **A version comparison against a vendor's macro is also a claim that the vendor's compiler is
the one running.** Write `defined(X) && X < N`.

**Running the rule immediately found a fourth**, in the same library and the same file family as
the first: `zlib-1.1.4/zutil.h:113` repeats `#if defined(MACOS) || defined(TARGET_OS_MAC)` and,
inside it, `#define fdopen(fd,mode) NULL`. Live on macOS today. It was missed because **the
compiler stopped at the first error in that file, so the second never got a chance to fail** —
"fix the error the compiler reports" is not the same as "fix the file". Inert for now (nothing in
the tree calls `gzopen`/`gzdopen`, and `CompressionManager` uses zlib format rather than gzip), so
it is a landmine rather than a fire — which is the argument for fixing it while the mechanism is
open, not against.

**Which half of this can be automated**, which matters if anyone turns it into a CI check: the
predicate patterns (`__APPLE__`, `TARGET_OS_`, `MACOS`, `__BIG_ENDIAN`, `POWERPC`,
`__i386__`/`_M_IX86` added after a fifth instance was found written as a positive CPU test, and
`#ifdef BIG_ENDIAN`/`LITTLE_ENDIAN`/`BYTE_ORDER` after a sixth) are
quiet enough to run on every build. `unsigned long` is not: GameSpy alone would bury it. Automate
the first half; the second stays a human read.

⚠ **The "five hits across zlib, LZH, EAC and all 564 GameSpy files" figure this section used to
carry was wrong, and wrongly reassuring.** GameSpy contributed **zero** of those five because not
one of its 564 files was ever opened. Real figures once they were: **7** on the automatable list,
**44** on the wider one. The automation split survives; the number did not.

**There are three distinct ways this check passes on nothing**, all now measured:

1. `grep` here is `ugrep`, which honours `.gitignore` by default. From the repository root it skips
   **466 tracked files**, including 54 `.cpp`/`.h` in two **built** libraries. Git does not ignore
   them.
2. **Every vendored directory carries a committed `.gitignore` containing `*`** — so `ugrep` reads
   none of it even when fully populated. Measured: `grep -rn socket Libraries/Source/GameSpy`
   returns **0**; `/usr/bin/grep -rn socket --include='*.c'` on the same directory returns **854**.
3. Those directories are **empty in a fresh worktree** until the vendor step runs, so a sweep finds
   nothing and says so cheerfully.

Any CI form of this rule must use `--no-ignore-files` or drive the list from `git ls-files`, must
run **after** vendoring, and must **fail rather than pass** on an empty directory.

EAC and the FFmpeg dist were swept on 2026-09-22 —
[`VENDORED-PREDICATE-SWEEP.md`](VENDORED-PREDICATE-SWEEP.md). FFmpeg is clean on both halves and
was always going to be (it is 2020s code written against `stdint.h`); EAC is clean on the
predicate half and produced the `__i386__` instance above on the human half, now fixed. GameSpy has
had the predicate half only: seven hits, six of them modern `__APPLE__ && __MACH__` Darwin support
and the seventh inside a commented-out block. **`zutil.h:113` is still live** — it is in a fetched,
`.gitignore`d directory, so it needs a vendor-step patch rather than an edit.

**Two ways this rule can report clean without having looked.** Both measured, both reproducible,
and they matter to every sweep in this plan rather than only to this one:

1. **`grep -r` from the repository root skips 466 tracked files on this machine.** `grep` here is
   ugrep, which honours `.gitignore` by default — and git does *not* ignore these files, so this is
   the tool's opinion rather than git's. 356 of the 466 are in `GeneralsMD`, including all 147
   committed FFmpeg files and 54 sources in `Libraries/Source/debug` and `Libraries/Source/profile`,
   **both of which are built**. Naming a directory explicitly avoids it; recursing from the root
   does not. Any sweep meant to be exhaustive wants `--no-ignore-files`, or a file list from
   `git ls-files`.
2. **zlib, GameSpy and most of LZHCompress are not in the repository at all** — their directories
   hold a `.gitignore` that ignores everything and `build.bat` fetches the sources. In a fresh
   worktree they are empty, and a sweep pointed at them finds nothing and says so. Any check must
   run after the vendor step and fail, rather than pass, on an empty directory.

**A corollary about fixing them**, learned the same afternoon: the fix can be as
platform-dependent as the bug. B3's `UINT32=unsigned int` compile definition is correct on Darwin
and would probably break the Windows build — `UINT32` is an SDK *typedef* and the definition makes
it a *macro*, so a TU reaching `windef.h` preprocesses to `typedef unsigned int unsigned int`. It
also buys nothing there, because the header's guard is `#ifndef UINT32` and a typedef does not
satisfy it. Guarded to non-Windows at merge.

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

**3. Pointer remapping in `WWSaveLoad` does not happen at all on x64 — and never has.**
*This entry was rewritten 2026-09-22 after it was measured. The first version, taken from a reading
of the code, described a probabilistic collision. It is not probabilistic and it is not a
collision.*

`persistfactory.h:133` saves an object identity token with `csave.Write(&objptr, sizeof(uint32))`
— 4 bytes. `persistfactory.h:118` has **always** read it back with `cload.Read(&old_obj,
sizeof(T *))` — 8 bytes on any 64-bit build. And `ChunkLoadClass::Read` (`chunkio.cpp:697`)
refuses a read that would overrun the chunk: it returns 0 and **does not touch the buffer**.

So `old_obj` keeps the `NULL` it was initialised with one line earlier, and every object is
registered with `SaveLoadSystemClass::Register_Pointer` under the key `NULL`. Measured against the
real `ChunkSaveClass`/`ChunkLoadClass` and the real template: 4 bytes written, `Read` returns 0,
key is `0x0`. Pointer remapping through this template is not degraded on x64 — it is **absent**.

**And the game never executes it**, which is the other half and cuts the other way. Zero references
to `SaveLoadSystemClass::` anywhere in `GameEngine` or `GameEngineDevice`; zero files naming
`PersistFactoryClass`; the only caller of `SaveLoadSystemClass::Save`/`Load` in the tree is
`Tests/test_wwsaveload.cpp`. The game's own save system is separate — `Common/System/SaveGame/`,
built on `Xfer`. The 22 `SimplePersistFactoryClass<>` instantiations are Westwood engine classes,
registered but never driven.

Certain and total, in code that does not run. **Neither half is the story on its own**, which is
why the first version of this entry was wrong in both directions at once.

The fix was not the trade-off it looked like either. Keeping 32 bits on the save side was
impossible — the load side already reads 8 — and widening does not "break the save format", because
no build that writes this format can read it back. The rest of the engine already writes these
tokens at pointer width (`AudibleSoundClass`'s `WRITE_MICRO_CHUNK` is `sizeof(var)`, read back as
`sizeof(old_ptr)`); this template was the only place that narrowed it. One line, bringing an
outlier into line with the codebase's own convention.

The existing test passed throughout the entire x64 port while the token was being lost, because it
only checks the object's data. That gap is the finding behind the finding.

**4. An animation picks the wrong frame from 33 upward.**
`hrawanim.cpp`'s `Float_To_Long(frame - 0.499999f)` floor idiom is exact only below 33. Float
spacing doubles at 32, so from there up the subtraction lands on an exact tie, which rounds to the
even neighbour below — an odd frame returns `frame-1`. Every architecture agrees, so it does not
block the port. Found because a test written for the port went red against real code, and the test
was right. D-track.

**5. A Japanese player's auto-saved replay may fail to write.**
`StatsCollector.cpp:345` and `Recorder.cpp:1582` build a **file name** from a player's name through
`%ls`, which renders an unmappable character as `?` — illegal in a Windows filename. Reasoned from
the code, not observed. UTF-8 introduces no path-illegal byte, so B1's sweep incidentally fixes it.

**6. A format string one translator away from being attacker-controlled.**
About a dozen callers pass a `TheGameText->fetch(...)` result as a printf format
(`InGameUI.cpp:280`, `:339`, `:7855` and others). Safe only because the shipped `.csf` strings
contain no `%`. None is user- or network-controlled today. Flagged, not actioned.

**7. An Intel and an AMD Windows machine compute a shell's flight path differently.**
Found by B17, 2026-09-25. **Half of this is measured and half is inferred. They are kept apart
below on purpose, because blurring them is how defect #3's first version went wrong in both
directions at once.**

*Measured.* `d3dx9_43.dll` (x64, 9.29.952.3111, the June 2010 redistributable that every Steam
install ships in `_CommonRedist`) does not have one `D3DXVec4Transform`. The export is a thunk
through a slot, and a one-time dispatch at RVA `0x5e198` fills that slot:

| Condition | Body | Sums lane *j* as |
|:--|:--|:--|
| `DisablePSGP`/`DisableD3DXPSGP`=1 under `HKLM\Software\Microsoft\Direct3D`, or no SSE | scalar, `0x3efa0` | `((x·m0j + y·m1j) + z·m2j) + w·m3j` |
| CPUID vendor `GenuineIntel` | SSE, `0x211e60` | `(x·m0j + y·m1j) + (z·m2j + w·m3j)` |
| any other vendor | SSE, `0x2112c0` | `((x·m0j + y·m1j) + z·m2j) + w·m3j` |

The dispatch was **read** from the disassembly. The three bodies were **run**: they are leaf
functions, so `Tests/d3dx_oracle` copies Microsoft's bytes into an executable page and calls them
under Rosetta. Over BezierSegment's basis matrix, the Intel body differs from the other two **only
in lane x**, the cubic coefficient `-P0+3P1-3P2+P3`. It is the one lane with four nonzero terms.
There it differs on **35.7%** of the oracle's basis inputs, and on **46.8%** of control-point
vectors shaped like a real shot. Replaying `BezFwdIterator`'s forward differencing under each order,
**51%** of synthetic flight paths have at least one point that differs. Wine's builtin `d3dx9`, which
CrossOver runs, sums left to right (its disassembly was read, not run).

*Inferred, not observed.* `BezFwdIterator::start` passes control-point coordinates through
`D3DXVec4Transform`. `DumbProjectileBehavior.cpp:638` moves the projectile to each path point.
`Object::crc` xfers the whole transform matrix (`Object.cpp:4112`). So a mixed Intel/AMD network
game on the current Windows build should report a mismatch after a shell-firing unit shoots, and
a replay recorded on one vendor should diverge on the other. **Nobody has seen this happen.**
`README.md` at the repository root says LAN and online play between separate machines "have not
been tested", and `lan-play.ps1` runs its copies on one machine, which means one CPU vendor and one
DLL body. That setup cannot show this, however many games it plays. There is a precedent of the same
shape: `215f84a5` found that the x64 CRT chooses an FMA3 or an SSE2 `log()` by CPU, one bit apart,
and fixed it with `_set_FMA3_enable(0)` in `WinMain`. Its CHANGELOG entry ("two players'
processors do the maths slightly differently … Every machine uses the same one now") does not say
how the bug was found. So it shows the project has already chosen one path for every CPU once. It
does not show that anyone has played across machines.

*What was done.* A Mac cannot agree with an Intel and an AMD Windows machine at once while
Windows takes this function from the DLL, and D3DX has no runtime switch equivalent to
`_set_FMA3_enable`: it exports no CPU-optimisation control. So, per decision 1 under "Decisions
taken" above, **every platform now routes `D3DXVec4Transform` and `D3DXVec4Dot` through
`d3dxportable.h`**. That file sums left to right: the SDK's reference order, the order of every
non-Intel machine, and the order CrossOver players compute today. The only callers are
`BezierSegment.cpp:112` and `BezFwdIterator.cpp:69-71`, so the change is in `d3dx9math.h` and
`d3dx9runtime.cpp`, not in game code. The DLL's own transform stays bound as
`D3DXVec4TransformFromDLL` for `dx9_smoke` alone, which is still the one check that can observe the
dispatch. Replays recorded on an Intel machine before this change may not play back. It is a
`WINDOWS-DEBT.md` row at high severity, because MSVC has never compiled it.

Related and **not** a defect, because someone got it right: `ConnectionManager.cpp:706`/`:718` pass
a constant `L"%ls"` with network chat as the *argument*. It looks like a redundant format and it is
the thing stopping a remote player's text being interpreted as one. B15 says so in capitals.

**8. A malformed font string in a map script reads past the end of the stack buffer.**
`ScriptActions.cpp`, the cinematic-text font parser: `for( c = buf; c != '\0'; *c++ )` compares the
*pointer* with `'\0'`. MSVC took `'\0'` as a null pointer constant, so the condition is always true,
and the loop ends only on a `' '` or `'-'`. It also advances `c` twice a pass, and `concat()`s the
whole rest of the string each time, and the `while (*c != ':')` after it has no bound either. A user
map's script can reach it. Found by B5 (clang refuses the comparison). **Kept as it is on every
platform** (spelled `c != NULL`, which is what MSVC compiled): a fix is a rewrite that changes what
Windows displays today and wants its own test.

**9. Replay error boxes overstated their buffer to `FormatMessageW` - fixed.**
`PopupReplay.cpp`, both error paths, passed `sizeof(buffer)` (2048 bytes) as a count of wide characters
for a 1024-`wchar_t` buffer, so a system message over 1024 characters would overrun the stack. Fixed
by B5 (`sizeof(buffer)/sizeof(buffer[0])`); any message that fits is unchanged.

**10. The score screen's "add buddy" always asks for profile 0.**
`ScoreScreen.wnd:ButtonAdd%d` never has data set: the two `GadgetButtonSetData(..., m_profileID)`
stores are commented out, so `GadgetButtonGetData` returns NULL and `playerID` is 0. Dead GameSpy
service; recorded, not fixed.

**11. A find handle is opened to ask a yes/no question and never closed - fixed.**
`Image.cpp:272` (`ImageCollection::load`) tests `FindFirstFile(...) != INVALID_HANDLE_VALUE` to
see whether the user has any `INI\MappedImages\*.ini`, and drops the handle without `FindClose`.
It runs once, from `GameClient.cpp:339`, so it leaks one handle per run, and only for a player who
has user mapped images. Harmless in practice. Found listing C1's file operations (B5's task file).
Fixed by C1 (c): the question now goes through `LocalFileSystem::getFilesInDirectory`, which
closes what it opens.

**12. Writing a file whose last name component has no `'.'` spins until AsciiString throws.**
`Win32LocalFileSystem::openFile`, for any `WRITE`, first creates the directories along the path. It
takes tokens as directories until it finds one that contains a `'.'` with no `'.'` after it, which
it takes to be the file. Two shapes of name defeat that rule.

- **The last component has no `'.'`** (`Save\README`). Once the name runs out, `nextToken` yields an
  empty token, which has no `'.'` either, so the loop carries on. Each pass appends a `'\'` to the
  directory name and calls `CreateDirectory` with it, until the name passes `_MAX_DIR`. The loop ends
  when AsciiString's 32767-character ceiling throws `ERROR_OUT_OF_MEMORY` to the caller. Before
  that, the first pass has already created a directory named after the file.
- **A dotted directory before a dotless file** (`Save\v1.0\README`). The walk stops at `v1.0` as
  if it were the file, never creates it, and the open that follows fails quietly.

**Not reachable today.** Every write the game makes names a file with an extension:
- `buildDDS.txt`;
- the map-preview copy (`MapUtil.cpp:1294`: a `.tga` name, with `'\'` and `':'` replaced by `'_'`);
- map transfers (`ConnectionManager.cpp:790`). `IsValidTransferFileContent` requires the name's
  last `'.'` to begin one of a fixed list of extensions, matched exactly, so it sits in the final
  component;
- `ScriptEngine`'s numbered backups and INI rewrites.

A future caller writing an extensionless name would hit it. `PosixLocalFileSystem` keeps the rule
but stops when the name runs out. Found by C1; recorded, not fixed on Windows.

**13. A staging-room stats message sent the address of a string instead of the string - fixed.**
`WOLGameSetupMenu.cpp:125` passed `formatPlayerKVPairs(...)`'s `std::string` straight into
`AsciiString::format("%d %s", ...)`'s varargs, where every other caller adds `.c_str()`. On MSVC
x64 a non-trivial class in varargs is passed as the address of a temporary copy (measured: clang
targeting `x86_64-pc-windows-msvc` passes `ptr`), so `%s` read the string object's own bytes.
MSVC's `std::string` keeps up to 15 characters inline at its start and a heap pointer there
otherwise (documented layout, not measured here), and a stats list is far longer than 15, so the
"STATS/" UTM carried the profile id and then the pointer's bytes. Other players in the staging room
never got this player's stats. It is dead-service code (GameSpy), and there was nothing portable to
preserve, so it is fixed with `.c_str()` (PM decision).

**14. A 3D turn toward a goal exactly behind does not turn.**
`Locomotor.cpp:170` (`tryToRotateVector3D`) and `NeutronMissileUpdate.cpp:320` turn a heading
toward a goal by at most the turn rate, rotating about `Normalized_Cross_Product(current, goal)`.
When the goal is exactly opposite the heading, that cross product is exactly (0,0,0), `Normalize`
leaves a zero vector alone, and `Matrix3D`'s axis-angle form - which asserts a unit axis
(`matrix3d.h:594`) - builds `cos(angle)` times the identity. Measured in a Release build: heading
(1,0,0), goal (-1,0,0), turn rate 0.1 gives (0.995,0,0) - the same heading, shortened, no turn; one
step off opposite, (-1,0.001,0), turns normally. The same on every platform (exact zeros, no
rounding in play), so not a desync; a Debug build of any platform stops on the assert instead.
Whether a match ever presents an exactly opposite goal to a 3D locomotor or a neutron missile is
not measured. `W3DWater.cpp:2441` has the same shape for a camera looking straight down, visual
only. Found 2026-09-26 while tracing a Debug-build abort in `test_wwmath` and `wwmath_selfcheck`,
which was the tests' own non-unit axes, fixed. Every call of the asserting overloads in the POSIX and
mingw-as-MSVC sweeps was listed by marking them deprecated: these three and `W3DView.cpp:364`/`:367`
(literal unit axes) are all of them outside the tests. Recorded, not fixed: a fix changes what the
simulation computes, which is rule 3's business.

**15. A truncated replay header reads as a name of 1023 U+FFFF characters.**
`RecorderClass::readUnicodeString` stops a string at `c == EOF`, but `fgetwc` never returns `EOF`
(-1): under MSVC it returns `WEOF`, which is 0xFFFF. So at the end of a truncated file the loop
stores 0xFFFF until it reaches its 1023-unit bound, and every string read after that is the same.
Only a damaged `.rep` reaches it. **Kept as it is
on every platform**: `WideCharFileGet`, which replaced `fgetwc` in PR (g), returns 0xFFFF at the end
exactly as `fgetwc` did (checked under Wine's msvcrt), so the check still never fires. Found by C1.

**16. With the save folder missing, the save list reads the game folder, and the scratch-map
cleanup deletes the game folder's `.map` files - fixed.**
`GameState::iterateSaveFiles` and `GameStateMap::clearScratchPadMaps` changed into the save folder
with `SetCurrentDirectory`, ignored its result, and searched `"*"`. If the folder did not exist, the
search ran in the current directory, which is the game folder. The first then offered any `.sav`
there as a save. The second deleted every `.map` file there, and the Zero Hour folder is where a
player's loose maps may sit. Both also returned without changing back if the search found nothing.
`GameState::init` creates the save folder first, so this needs the creation to have failed (an
unwritable or redirected Documents folder). Shown under Wine by `fs_oracle dir-before` with a
missing folder: it listed the current directory's six files, `Map Scratch.map` among them. Fixed by
C1 (c): both list the save folder by its path, and a missing folder lists nothing.

**17. Fork-introduced: the logic catch-up lets the water grid, which the simulation reads, fall
behind by the number of catch-up frames.** Fixed by T1c (below). A code-path argument, not yet shown
to desync in a match. `TerrainLogic::isUnderwater` (`TerrainLogic.cpp:2218, 2276`) reads
`TheTerrainVisual->getWaterGridHeight` whenever the map has enabled the water grid, and its
callers are simulation: pathfinding (`AIPathfind.cpp:5918`), `Locomotor`, `PartitionManager`,
`FloatUpdate`, `ParachuteContain`, `ObjectCreationList`, `GenerateMinefieldBehavior`. The grid's
mesh moves in `WaterRenderObjClass::update` (`W3DWater.cpp:1343`), on the CLIENT pass, gated by a
static `lastLogicFrame != currLogicFrame`: at most one step per client pass. EA's loop ran one
client pass per logic frame, so that was exactly one step per frame, deterministic. This fork's
catch-up (`GameEngine.cpp:2468-2496`) runs several logic frames with no client pass in front of
them, in network games and whenever `m_maxFPS > 0`. A machine that catches up k frames advances the
grid once, so grid heights at frame N depend on that machine's frame-rate history. Two machines in
one match can then disagree about `isUnderwater`, and a replay played at a different speed from its
recording can too. The loop's authors guarded the same class for the camera freeze
(`GameEngine.cpp:2546`), but not for the water grid. **Which maps:** the grid is not a script
action; `TerrainLogic::newMap` (`TerrainLogic.cpp:1143`) turns it on when the map has a waypoint
named `WaveGuide1`. Every shipped map was searched for that name (read-only from the archives, each
decompressed from RefPack or zlib and checked to be a whole `CkMp` file): it is in three, all
original-Generals campaign maps in `maps.big` - `CHI03` (the dam), `GLA01` and `USA06`. **None of Zero
Hour's 116 maps has it, so no shipped multiplayer or skirmish map runs the grid**; where it bites a
player is a custom map with a `WaveGuide1`, and replays of those three campaign missions played back
at a different speed. Fix direction: step the grid once per LOGIC frame, which restores EA's count on
Windows. That is a rule 3 change needing a replay check. Found by T1's recon (-47), read-only.
**Fixed by T1c:** the step (W3DWater.cpp's text, frame gate and all) is `WaterGridMotion` in gameengine,
and `GameLogic::update` calls it at its top through `TerrainVisual::updateWaterGrid`, where EA's client
pass stood, once per logic frame. `test_water_grid` holds it, under EA's loop and three catch-up
schedules, to the original code's own output under EA's loop (an oracle, identical under Wine, arm64
and x86_64); the original under the same catch-up schedules is the armed control and differs. **On
Windows this changes grid heights on water-grid maps whenever the catch-up runs, back to EA's count.**
Headless runs (one logic frame a pass) step as before. `WINDOWS-DEBT.md` has the row.

**18. A screenshot reads past the end of its image when the width is not a multiple of 8 - fixed.**
`W3DDisplay.cpp`'s `CreateBMPFile`, which writes every F12 screenshot and every `-video` frame, set
`biSizeImage` to `(width + 7) / 8 * height * 24` and wrote that many bytes out of a buffer both callers
allocate as `3 * width * height`. At a width that is a multiple of 8 the two agree; at any other, a
1366x768 screen or a window of any size, it reads `3 * height * (8 * ceil(width / 8) - width)` bytes
past the end (4,608 at 1366x768), and since it never pads a row to four bytes, a width that is not a
multiple of 4 gives a skewed picture as well. Found writing A1's POSIX branch. **Fixed:** the portable
writer A1 wrote, which reads exactly the image and pads each row, is the one writer on every platform
(checked on a 5x3 image under AddressSanitizer and by macOS's own decoder). `WINDOWS-DEBT.md` has the
row.

**19. Fork-introduced: a random map smaller than the generator's own sizes can put two starts on top
of each other.** Recorded, not fixed (command line only). `-randommap <seed> <players> <cells>` takes
any cell count from 64 up. At 128 cells for two players (below the generator's small size, 184, and its
normal size, 248), seed 1 puts `Player_1_Start` at (980,1040) and `Player_2_Start` at (950,920), 124
units apart. Both AIs then judge every build site unsafe, because the enemy command centre is inside
`isLocationSafe`'s ~350 radius, and the match stays idle. The generator's own
`start_positions_land_inside_the_map_with_room_between_them` requires more than 32 cells (320 units)
between starts, but only tests the default size per player count.
- **Who can reach it:** only the command line. The skirmish menu always generates for eight players
  at `cellsFor(size, 8)`, 304 cells or more (SkirmishMapSelectMenu.cpp:130). A map rebuilt from its
  name carries whatever size made it.
- **Visible now:** `replay-check.ps1` passed 128, and both harnesses now leave the size to the
  generator. `replay_check` keeps seed 1 at 128 cells as the IDLE guard's armed control. The integer
  generator should make the same map on Windows; expected, not verified.
- Found by E1's harness (-18).

**20. Fork-introduced: saving the game nudged every object's heading, so a replay watched with
checkpoints parted from its recording - fixed.** `Object::xfer` read the transform, transferred it and
set it back in both directions. `Thing::setTransformMatrix` recomputes the cached angle from the matrix
(`Get_Z_Rotation`, a round trip that does not return the angle the logic set) and clears the cached
flags. So every save replaced each object's exact heading with a reconstructed one, and the logic's
next `setOrientation` built a matrix an ULP or two away.
- **Why it mattered now:** EA's save had the same side effect, but a save used to end a session. The
  fork's replay viewer saves a checkpoint every 900 frames of playback (InGameUI.cpp,
  `takeReplayCheckpoint`).
- **Measured on macOS**, seed 1 at 12,000 frames:
  - the recording and a second run agree (0xB5B11D73);
  - the playback, with 14 checkpoints, reached 0x352D97A4;
  - bisection: identical through frame 8,259. At 8,260 exactly one object differs: a GLA Stinger
    Soldier whose position is bit-identical and whose rotation terms are 1-2 ULP off;
  - with checkpoints skipped, or with the setter guarded, the playback matches.
- **Fixed:** `Object::xfer` and `Drawable::xfer` set the transform only when loading. A save now leaves
  the world exactly as it found it. Normal play, recording and network CRCs (`Object::crc` is a
  separate function) are unchanged.
- **On Windows:** checkpointed playback now matches its recording, which it did not before; expected,
  not verified.
- `replay_check` plays seed 1 at full length as the end-to-end check. `WINDOWS-DEBT.md` has the row.
  Found by E1's harness (-18).

### Latent undefined behaviour that MSVC happens to tolerate

Not defects a Windows player can hit today: MSVC does the intended thing. But a second compiler and
C library are free not to, and when they don't, the result looks like a platform bug. **If you are
hunting a crash or corruption that only one platform shows, look here first.**

- **Overlapping `strcpy`.** `WWLib/trim.cpp`'s `strtrim` and `wcstrim` shifted a string left over
  itself with `strcpy`/`wcscpy`. Copying between overlapping regions is undefined. MSVC's copy
  evidently runs forwards and gets away with it, and every INI line in the game goes through
  `strtrim`. glibc's aarch64 `strcpy` under GCC writes the tail before it has read the middle, and
  `test_wwlib`'s `strtrim_in_place` came back corrupted on Linux arm64 only. Fixed with `memmove`.
  **The mechanism generalises:** any `strcpy`/`memcpy`/`sprintf` whose source and destination can
  overlap works until a library copies in a different order.
- **A `va_list` passed by `const` reference.** `StringClass::Format_Args` takes `const va_list &`.
  Where `va_list` is a pointer (MSVC, both arm64 ABIs) the `const` binds to the reference. Under
  x86-64 System V it is an array, the `const` binds to the elements, and it cannot be handed to
  `vsnprintf`: a compile error on Linux amd64 alone. Fixed in `wwstring.cpp`. **`widestring.cpp`
  has the same signature** and will hit the same error when B1 ports it.

- **Not undefined, but the same trap: an optimiser may drop a `new`/`delete` pair.**
  `initMemoryManager` checks the engine's operator new is the one linked by counting the calls that
  `new char; delete ...` makes, and it `exit(-1)`s if the count is wrong. C++14 lets a compiler omit
  a new-expression's allocation when nothing else uses the pointer. clang `-O2` did, the count stayed
  0, and a Release build exited silently at startup. Found by C1's linked file-system test, the first
  thing to run `GameMemory.cpp` off Windows, and independently by B1's `.csf` test at `-O3`. Fixed by
  calling `::operator new` and `::operator delete` by name, which no compiler may drop (B1's fix;
  C1's first fix, a `volatile` pointer, was redundant once both merged, and was removed).
  Windows builds evidently keep the calls, since the game starts there. The fix is a
  `WINDOWS-DEBT.md` row.
- **A mismatched `delete` in the saved-login obfuscation - fixed.** `WOLLoginMenu.cpp`'s
  `obfuscate()` allocated its buffer with `NEW char[...]` and freed it with `delete buf`. Freeing an
  array with the non-array form is undefined; MSVC's CRT and the macOS one both release a `char`
  array either way, which is why it never showed. `delete[]` now (PM decision): the same behaviour on
  both, without the undefined part.
- **An XOR whose write lands one character late, on every compiler - kept.** In the same function,
  `*c = *c++ ^ *c2++;`. Since C++17 the right-hand side of `=` is sequenced before the left, so `c` has
  already moved on when `*c` is written: each byte is stored one position further than the loop reads
  as meaning. MSVC in C++17 mode and clang apply the same order, so both platforms produce the same
  bytes. Changing it would make every saved GameSpy login unreadable, so it stays (PM decision).

- **Strings built before main, and the memory manager's start - fixed.** File-scope `LogClass`
  objects (`WOLLobbyMenu.cpp`, `WOLQuickMatchMenu.cpp`, `PeerThread.cpp` twice) build an
  `AsciiString` in their constructors. The strings allocate from `TheDynamicMemoryAllocator` directly,
  and it is NULL until something starts the memory manager. The global operator new starts it on
  first use (`preMainInitMemoryManager`), so Windows survives only because some other static
  constructor there happens to call operator new first: nothing orders C++ static initialisation
  across translation units. On macOS a `LogClass` ran first and the process died with
  `EXC_BAD_ACCESS` before main (found by B6). Fixed (B5): `AsciiString` and `UnicodeString` call
  `preMainInitMemoryManager` themselves when the allocator is NULL, as operator new does; that costs
  one not-taken branch on their allocation path. Off Windows the start then reached a second case of
  the same class: `userMemoryManagerInitPools` looks for its pool-size file through `zh_fopen`, and
  the path resolver's caches were file-scope `std::map`s, used before their own constructors ran (and
  emptied when those did). They are now built on first use. `test_premain_strings` and
  `test_premain_unicode` build each string type first in a static constructor. **The general rule:**
  anything reachable from a static constructor must not depend on another file's statics; on Windows
  the link order happens to work.
- **A pointer truncated to 32 bits and dereferenced - fixed, and unreachable in the game.**
  `WW3D2/surfaceclass.cpp`'s `SurfaceClass::FindBB` and `Is_Transparent_Column` computed a row's
  address as `(unsigned char *)((unsigned int)lock_rect.pBits + offset)`, which on x64 drops the top 32
  bits of the locked pointer and faults whenever the driver maps the surface above 4 GB. Their only
  caller is `font3d.cpp`, and nothing in the game creates a `Font3DDataClass` (`Get_Font3DInstance`
  has no caller outside WW3D2's asset manager), so no player reaches it. Clang refuses the cast; A1
  made it plain pointer arithmetic.
- **Two pointers truncated before they are subtracted - fixed, and correct in practice.**
  `WW3D2/assetmgr.cpp` and `W3DAssetManager.cpp` sized a name as `((int)mesh_name) - ((int)name) + 1`.
  The difference of two truncated addresses is the true difference modulo 2^32, so a name shorter than
  2 GB comes out right, and it did; clang refuses the casts. Now `(int)(mesh_name - name) + 1`.
- **A D3DX entry point declared with the wrong return type.** `d3dx9math.h`'s Windows
  `D3DXMatrixInverseFunction` returns `HRESULT` where `D3DXMatrixInverse` returns a `D3DXMATRIX *`:
  on x64 the pointer comes back truncated to its low 32 bits. No call site reads the result, so
  nothing is wrong today; the first one that tests it against null would be.
- **A vertex format and its structure disagree.** `dx8fvf.h`'s `DX8_FVF_XYZNUV2DMAP` declares three
  texture sets (one, four and two floats, 52 bytes a vertex) and `VertexFormatXYZNUV2DMAP` holds only
  the last two (48). Nothing sizes a buffer by either; `dx8fvf.cpp` only prints the format's name.

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
   - **Show what Windows sees, with `GeneralsMD/Code/Tools/windows_view_diff.py`.** It resolves
     MSVC x64's conditionals in every C/C++ file a change touches, before and after, and sorts each
     file into identical, include-case-only, or different, printing the differences. Put its
     summary in the pull request and give every "different" file a `WINDOWS-DEBT.md` row. Do not
     reach for mingw instead: it defines `_WIN32` but not `_MSC_VER`, so it takes the POSIX side of
     most of this tree's platform code. **If you compile with MinGW as extra evidence, define what the
     Windows build defines: `-DWIN32 -D_WINDOWS`** (`CMakeLists.txt` adds both to every Windows
     target). Without `_WINDOWS` it silently skips WW3D2's window and movie code and `udp.h`'s winsock,
     which is how A1's first MinGW runs checked less than they said; the tool itself left `_WINDOWS`
     unresolved until A1's findings added it. A MinGW pass is still not MSVC's. **What the tool cannot see:** what a macro token expands to
     (check its definition), anything through an `#include` (each file is resolved on its own), a
     condition on any macro other than the compiler's and platform's own, and whether MSVC accepts
     the result. It is a text diff, not a compiler.

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

8. **Non-ASCII in a literal is written as a `\u` escape, never as raw bytes.** MSVC reads source in
   the ANSI code page (this tree does not pass `/utf-8`) and clang reads UTF-8, so the same bytes are
   different characters on the two compilers - and inside a `UnicodeString`, different CRC bytes.
   Write `u'\u20AC'`, not the euro sign. `widechar_check` fails on any byte above 0x7F inside any
   string or character literal in `GameEngine`, `GameEngineDevice`, `WWVegas`, `Libraries/Include`,
   `Main` and `Tests`, in every branch; an exception goes in `Tools/widechar_check_allow.txt` with its
   reason. Comments are not checked. Found by B1: `Keyboard.cpp`'s UK euro key had held three raw
   bytes for years, and MSVC read them as one wrong character.

9. **Never start the engine with the real game install as its root.** `GameEngine::init` deletes
   `Data\INI\INIZH.big` (a leftover of patch 1.01) from its install root, and the Steam install on
   `/Volumes/External` has that file, as a Windows install does. Only the engine does this, and only
   at start-up, so reading the install's archives from a test (as `gametext_csf` and
   `test_miles_miniaudio` do) is safe. What is forbidden is any process that runs
   `GameEngine::init` - a headless game, E1's replay runs, an engine-level test - with that folder as
   its working directory or install root. Use a copy of the files it needs, or a read-only mount.
   Found by C1 (c) while moving that delete behind `TheLocalFileSystem`.

## Open questions that need an answer before the milestone that depends on them

- **BC textures.** The art is DXT/BC (44 files in WW3D2 reference it). SDL3's GPU API exposes BC
  formats where the device supports them. Apple Silicon reports `supportsBCTextureCompression`, and
  desktop Vulkan drivers almost always do, but which Macs, which macOS versions and which Linux
  drivers is worth confirming rather than assuming. If the answer is patchy, D5 decompresses on load
  and eats the memory. **Owner: D5, answer before D4 finishes.**
- ~~**Metal vs MoltenVK.**~~ Answered by decision 3: SDL3's GPU API, with Vulkan plus MoltenVK as the
  named fallback.
- **Where the game data comes from.** There is no Mac Zero Hour. Users will have to bring `.big`
  files from a Windows or Steam install, and the launcher's install flow assumes a local one.
  **Owner: unassigned. Needs a product answer before M5, not an engineering one.**
- **Code signing and notarisation.** Unsigned is fine for M1–M4 and not fine for a release.
  **Owner: E2.**
