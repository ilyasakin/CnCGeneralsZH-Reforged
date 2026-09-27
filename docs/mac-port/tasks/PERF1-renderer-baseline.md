# PERF1 — A performance baseline of the Mac renderer

- **Milestone:** M4
- **Depends on:** A3e
- **Status:** in review. Fix 1 made (the device's recording halved). Fix 3 kept by the PM's decision (proven identical; no measurable gain). Fix 2 measured and deferred. The long frames were this shared Mac's memory compressor under load, and are absent on a quiet second machine (finer). An engine profile and a ranked list of same-result speed-ups are next.
- **Owner:** -a9

> **Void: every timing taken on finer before 02:32 local, 2026-09-27.** finer, a MacBook Pro with its
> lid closed, was cycling between DarkWake (about 45 s) and Maintenance Sleep (5 to 7 s): 103 sleeps that
> day before `pmset -a disablesleep 1` (the user's approval, on the workers' revert list). One run showed
> a 21,414 ms engine frame that the device's clock saw as 333 ms. Pass or fail results and signatures
> from that time stand; its frame and work times do not. So does the first per-run-locked matrix
> (02:51 to 03:10): each run took the machine's lock alone, in the same order every time, and the
> no-fix-3 runs drifted from 1.35 to 2.07 ms of device draw as finer warmed. It was stopped for the
> ABBA A/B below.

## Why

The game now opens at the monitor's native size, and the device draws through SDL3 GPU from a CPU-side
D3D9. Nobody has measured whether that is playable. This measures first and changes nothing: a table,
a profile, and the three largest costs, each with a proposed fix, its likely gain, and its risk to the
oracle checks (FFReference, -47's interpreter, the capture replay).

## Method

**Build:** Release (`build-mac`, `CMAKE_BUILD_TYPE=Release`, `RELEASE_DEBUG_LOGGING` on, which keeps the
engine's own frame-time report in `DebugLogFile.txt`).

**Machine:** Apple M3 Pro, 36 GB, macOS 27.0, a 2560x1440 display at 144 Hz. The machine is shared, so the
load average is recorded before and after every run, and every configuration is run three times.

**Runs:** all with a hidden window (`-hiddenwindow`) and the HUD overlay on.
- The seed-1234 skirmish with `-observer`, which shows both AI sides with full visibility, for 1,800
  logic frames.
- The real shell map, `-nologo` without `-quickstart`, which would switch the shell map off.
- Each at 800x600, 1920x1080 and 2560x1440 (native).

**What is measured:**
- **The engine's own report** (`HEADLESS FRAMETIME`): the match's frame-time distribution from one second
  after the match starts to the end. Skirmish only.
- **The device's report** (`ZH_GPU_TIMING="delay,count"`, PosixDevice9Timing.cpp): 1,000 frames from a
  fixed time after the first present (45 s for the skirmish, 60 s for the shell map), after loading at
  any resolution. For each frame:
  - the frame, from present to present;
  - the draws;
  - the device's CPU time recording them;
  - mid-frame flushes;
  - the present, which records and submits the last batch and the gamma pass.
  - What is left of the frame is the engine's own CPU time.
- **GPU time:** SDL3 GPU has no timestamp queries. With `ZH_GPU_TIMING_SYNC=1`, every submit waits for its
  fence and the waits add up. That serialises CPU and GPU, so those runs measure the GPU, not the frame
  rate, and they are reported apart.
- **Profile:** `sample(1)` on the worst configuration, giving the top self-time functions split into the
  engine, the device (PosixDevice9 and resource mirroring), SDL3 GPU, and the driver.

## Results (2026-09-26, Release, hidden window, three runs a configuration)

**Every configuration is playable, and every one is held at the display's 120 Hz.**
- **The frame:** from present to present, p50 is 8.2 to 8.5 ms and p99 is below 9.6 ms everywhere.
- **Why that is not the cost:** SDL's swapchain acquire waits for vsync, even with the window hidden. The
  first matrix measured only that pacing: about 8.33 ms whatever the resolution and the draws. It was thrown
  away, and the timing aid now reports the wait apart.
- **What a frame costs,** its *work* without the wait: 4.2 to 5.3 ms at p50 and 5.5 to 9.5 ms at p99.
- **About 2 ms headroom** at p99 in the worst configuration, against 120 Hz; a 60 Hz display leaves far more.

| Scene, resolution | Frame p50 / p99 / worst (ms) | Work p50 / p95 / p99 (ms) | Draws | Device draw p50 | Record+submit mean | Engine CPU mean | GPU p50 / p95 (sync) | Engine match p99 / worst |
|---|---|---|---|---|---|---|---|---|
| skirmish, 800x600 | 8.39–8.48 / 9.24–9.39 / 9.56–10.19 | 4.78–4.92 / 5.45–5.52 / 5.94–6.48 | 1788 | 2.89–2.92 | 0.50–0.53 | 1.46–1.54 | 1.49 / 2.24 | 9.75 / 11.43–546.45 |
| skirmish, 1920x1080 | 8.22–8.36 / 9.48–9.55 / 9.65–23.58 | 4.38–4.63 / 5.00–5.45 / 5.57–8.13 | 1571 | 2.56–2.58 | 0.46–0.52 | 1.42–1.69 | 1.81 / 2.83 | 9.75 / 11.20–573.49 |
| skirmish, 2560x1440 | 8.20–8.31 / 9.48–9.54 / 9.54–51.91 | 4.37–4.56 / 4.96–5.27 / 5.46–6.81 | 1584 | 2.59–2.60 | 0.46–0.49 | 1.38–1.61 | 2.24 / 3.61 | 9.75 / 12.50–644.95 |
| shell, 800x600 | 8.37–8.41 / 9.52–9.57 / 9.94–16.12 | 5.09–5.30 / 7.93–8.23 / 8.78–9.47 | 1350 | 2.57–2.62 | 0.97–1.02 | 1.68–1.81 | 2.09 / 3.15 | — |
| shell, 1920x1080 | 8.30–8.33 / 9.42–9.48 / 11.12–17.10 | 4.16–4.48 / 5.25–5.77 / 6.52–8.26 | 1195 | 2.27–2.33 | 0.57–0.62 | 1.40–1.71 | 2.21 / 3.46 | — |
| shell, 2560x1440 | 8.34–8.40 / 9.41–9.46 / 9.76–11.34 | 4.49–4.88 / 5.63–5.98 / 6.38–6.52 | 1302 | 2.44–2.50 | 0.61–0.68 | 1.54–1.76 | 2.55 / 4.41 | — |

**Reading the table:**
- **Work:** the frame without the swapchain wait.
- **Device draw:** the CPU time inside `Gpu_Draw` recording the frame's draws.
- **Record+submit:** Present's own recording and submission, the wait excluded.
- **Engine CPU:** work minus the device's two parts.
- **GPU:** from the serialised run, with every submit waiting for its fence.
- **Engine match:** the engine's own report over the whole match, frames from one second in; skirmish only.
- Ranges run across the three runs. The load average was 5 to 22 during the matrix: `runs.txt` in the
  scratch copy has it per run.

**Findings from the table:**
- **The cost is on the CPU and does not depend on resolution.** Work is the same at 800x600 and at
  2560x1440. The GPU needs 1.5 to 2.6 ms at p50 and at most 4.4 ms at p95, growing mildly with the pixels.
  Even serialised, it is not the limit.
- **The device's recording is over half of the work:** 2.3 to 2.9 ms for 1,200 to 1,800 draws, which is
  1.6 to 1.9 microseconds a draw. Record+submit adds 0.5 to 1.0 ms. The engine's own CPU is 1.4 to 1.8 ms.
- **Long frames:** three of the twelve skirmish runs had a single frame of 0.5 to 0.65 s, at a different
  logic frame each time (246, 290, 747), which isn't something the seeded game does. Traced later: see
  "The long frames" below.

## Profile

`sample(1)` of the main thread: the shell map at 800x600, hidden, 20 s from 130 s in, 5,218 samples. The
load was 10.5 at the start and 30 at the end. System frames (printf, memmove, mutexes) are charged to their
nearest caller in the game's own binary:

| Share of the main thread | Where | Function (and the system work under it) |
|---|---|---|
| 10.6% | device, via the generator | `CombinerShader_Key`: printf formatting |
| 8.3% | device, via the generator | `VertexShader_Key`: printf formatting |
| 5.6% | engine | `JobSystem::parallel_for`: waiting for the workers (particles) |
| 3.7% | engine | `MemoryPool::allocateBlock`: contended mutex |
| 3.4% | engine | `DynamicMemoryAllocator::freeBytes`: contended mutex |
| 3.4% | device | `SdlGpuFrame::Record_Batch`: memmove |
| 3.1% | engine | `DynamicMemoryAllocator::allocateBytes`: contended mutex |
| 2.9% | device | `PosixDevice9::Gpu_Draw`: memmove, staging vertices |
| 2.2% | SDL3 GPU and driver | `METAL_DrawIndexedPrimitives`, AGX state encoding |
| 1.4% | engine | `renderShoreLinesSorted` |
| 1.3% | engine | `W3DParticleSystemManager::doParticles` |
| 1.2% | device | `PosixDevice9::Build_Constants` |
| 1.2% | SDL3 GPU and driver | `METAL_BeginRenderPass` |
| 1.1% | engine | `operator delete`: contended mutex |
| 1.1% | device | `SdlGpuFrame::Stage`: memset and bzero |

**By owner:**
- **The device and what it calls per draw:** about 34% of the main thread.
- **SDL3 GPU and the Metal driver:** about 6%.
- **The engine:** the rest, with its allocator's locking the largest part of it.
- The main thread is busy for about 55% of the sampled time and waiting for vsync for the rest. So 10% of
  its samples is about 0.8 ms of an 8.3 ms frame.

## The three largest costs, and proposed fixes (not made: for the PM's decision)

**1. Every draw formats both program keys with printf: about 19% of the main thread, about 1.5 ms a frame.**
- **The cause:** `SdlProgramCache::Vertex_Program` and `Pixel_Program` build `VertexShader_Key` and
  `CombinerShader_Key`, strings made with snprintf, to look up a program that is nearly always cached.
- **The fix:** key the cache by the description's own bytes. The descriptions are built with memset, so
  they compare with memcmp. Keep a last-used fast path, as the pipeline cache has. Build the key string only
  on a miss, and keep it on the program, which is where the capture signature reads it.
- **Gain:** about 1.3 to 1.5 ms a frame, a third of the work.
- **Risk to the oracle checks:** none expected. The same description gives the same program. The round
  trip, the capture replay and FFReference's harness would all show a wrong program. A padding byte left
  unset would only cost a cache miss, never a wrong program.
- **Owner:** the device (mine). The generators are untouched.

**2. The engine's allocator takes a contended mutex: about 11% of the main thread, about 0.9 ms.**
- **The cause:** `MemoryPool` and `DynamicMemoryAllocator`, and `operator delete` through them, run the
  macOS pthread mutex's slow path, which it takes when another thread holds or waits for the lock. The job
  system's worker threads (`fillBillboards`) allocate from the same pools while the main thread does.
- **The fix, one of:**
  - per-thread pool caches;
  - an `os_unfair_lock` in place of the pthread mutex on macOS;
  - keeping the particle workers' allocations out of the shared pools.
- **Gain:** up to about 0.9 ms.
- **Risk:** none to the oracles or to determinism, since allocation does not feed the simulation. It is a
  change to the engine's memory manager, so the crash and leak risk is real, and it needs its owner and a
  second reader.
- **Owner:** the engine's memory and job system (B8/B11), not the device.

**3. The device copies and clears more than it needs per draw: about 9%, about 0.6 ms.**
- **The costs:**
  - `Record_Batch`'s memmove, the batch's bytes into the transfer buffer (3.4%);
  - `Gpu_Draw`'s staging of dynamic vertices (2.9%);
  - `Stage` zero-filling what is then overwritten (1.1%);
  - `SdlResourceMirrors::Buffer` re-copying buffers whose version moved (1.0%);
  - `Build_Constants` rebuilding both constant blocks for every draw (1.2%).
- **The fix:**
  - stage without the zero-fill;
  - write staged bytes straight into the mapped transfer buffer instead of copying them in at record time;
  - rebuild the constants only when their state has changed, with a dirty flag set by the Set* calls.
- **Gain:** about 0.4 to 0.6 ms.
- **Risk to the oracle checks:** low. Staging and constants are exactly what `posix_gpu_draw_selfcheck`, the
  FFReference harness and the capture replay exercise. A stale constant block would show there as a wrong
  colour.
- **Owner:** the device (mine).

**Not proposed:**
- **The GPU side:** 1.5 to 2.6 ms, not the limit.
- **SDL3 and the driver:** about 6% together, and not ours to change.
- **Resolution:** the cost does not grow with the pixels.

**Kept:** the sample output and the matrix logs, a few MB in scratch. No xctrace trace was recorded.

## After the PM's decisions (2026-09-26)

The PM's calls: fix 1 go; fix 3 go after it, as its own commit; fix 2 held; and the long frames chased
before the fixes are called done.

### Fix 1: programs found by their description's bytes (46f95c30)

`SdlProgramCache` finds a program by the description's own bytes: a last-used fast path by `memcmp`, then
an FNV-hashed table capped at 16,384 entries. The key string is built only on a miss, and kept on the
program, where the capture signature reads it.

Before (6c334876) and after (46f95c30), the same six configurations, three runs each, the load 9 to 55.
The medians over the three runs:

| Configuration | Work p50 | Work p99 | Device draw p50 | Device draw mean | Record+submit mean |
|---|---|---|---|---|---|
| skirmish, 800x600 | 5.15 → 3.42 | 7.39 → 5.20 | 2.93 → 1.38 | 2.94 → 1.38 | 0.59 → 0.55 |
| skirmish, 1920x1080 | 4.51 → 3.07 | 5.52 → 4.72 | 2.57 → 1.21 | 2.58 → 1.22 | 0.49 → 0.47 |
| skirmish, 2560x1440 | 4.56 → 3.39 | 7.27 → 6.61 | 2.60 → 1.25 | 2.60 → 1.26 | 0.49 → 0.54 |
| shell, 800x600 | 5.25 → 4.20 | 8.54 → 8.28 | 2.60 → 1.43 | 2.83 → 1.59 | 1.00 → 1.05 |
| shell, 1920x1080 | 4.35 → 3.19 | 6.36 → 4.65 | 2.28 → 1.15 | 2.26 → 1.15 | 0.60 → 0.61 |
| shell, 2560x1440 | 4.67 → 3.38 | 6.91 → 5.44 | 2.46 → 1.21 | 2.44 → 1.21 | 0.64 → 0.65 |

- **The device's recording halved:** 1.2 to 1.4 ms at p50, from 2.3 to 2.9. The work fell by 1.1 to
  1.7 ms, as proposed.
- **Signatures:** a capture run before and after: the same 50 signatures. 29 files are byte-identical;
  the rest differ in their matrices only (plus one vertex set and one texture name), which is the camera
  at the moment of capture, not the draw.
- **The replay:** 50 of 50 against FFReference, with the known C1.
- **The suites:** ctest 22 of 22 in the subsets run.

### Fix 2: the engine allocator's mutex, measured and deferred

About 11% of the main thread in the profile: `MemoryPool::allocateBlock` 3.7%,
`DynamicMemoryAllocator::freeBytes` 3.4%, `allocateBytes` 3.1%, `operator delete` 1.1%. That is up to
about 0.9 ms a frame. Held by the PM. One constraint for whoever takes it: the engine's lock is a
CRITICAL_SECTION on Windows, which is recursive; `os_unfair_lock` is not, so it is no drop-in
replacement. Taken since, as candidate 1 below, with an owner and a depth around the unfair lock.

### Fix 3: the per-draw copies and clears

- **Done (04495837), kept by the PM's decision: no measurable gain at this resolution, <0.1 ms, below what six runs resolve.** It is proven identical in result and does strictly less work, so under the user's rule (a safe improvement is taken when its result is the same, guaranteed) it stays. The batch's three arenas (stream, upload, constants) grow without
  zero-filling: `std::vector<ArenaByte>`, a byte with an empty constructor, in place of
  `std::vector<uint8_t>`. What is staged is written in full at once; alignment gaps keep what they held,
  and are uploaded but never read. The profile put the fill at about 1% of the main thread.
- **Signatures:** fix 3 off and on, both on 8d3bef8e: the same 50 signatures. 29 byte-identical, the
  other 21 in matrices only (one each also in vertices, a texture name, render states), as with fix 1.
- **The A/B, on finer** (2026-09-27, 03:13 to 04:21; the machine awake, its lock held for the whole
  batch). ABBA, BAAB, ABBA; skirmish and shell at 1920x1080 with the overlay; `-offscreen`,
  `ZH_OFFSCREEN_HZ=120`, `-noaudio`; six runs of each binary a configuration, 60 s idle before each run.
  All 72 thermal readings were Nominal, the P-cluster between 0.7 GHz (idle) and 3.8 GHz; the load 1.5
  to 2.5; no sleeps. Medians over the six runs of each run's p50, without fix 3 / with it:

  | Configuration | Work p50 (ms) | Device draw p50 (ms) | Record+submit mean (ms) | Work p99 (ms) |
  |---|---|---|---|---|
  | shell, 1920x1080 | 4.359 / 4.361 | 1.808 / 1.702 | 1.050 / 1.138 | 7.816 / 7.469 |
  | skirmish, 1920x1080 | 4.361 / 4.465 | 2.050 / 2.013 | 0.590 / 0.653 | 7.531 / 7.492 |

  The differences change sign between mean and median and between the scenes, and stay within about
  0.1 ms: below what six runs resolve on this machine. The expected gain was about 1% of the main thread,
  some 0.05 ms. The code does strictly less: the disassembly of `SdlGpuFrame::Stage` on finer has no
  `bzero` with fix 3 (66 instructions against 72). By position in the block, device draw was flat in the
  shell (1.766, 1.769, 1.741, 1.756) and lower first in the skirmish (1.829, 2.088, 2.072, 2.019), which
  the ABBA order balances.
- **The same result, proven:** signatures on finer with the overlay, 50 of 50 in common (29
  byte-identical, 18 matrices only, 2 matrices and vertices, 1 render states and matrices); the
  FFReference replay of the fix-3 capture, 50 compared, 0 failed, the three known C1/C5 findings; the
  suites, 83 of 83.
- **Not done, and why:**
  - *Staged bytes written straight into the mapped transfer buffer:* the memmove is 3.4%, but mapping
    the transfer buffer across a batch changes when it is cycled and when it is safe to write, which is
    exactly the hazard class the PM listed as suspect 1. Not worth it before the A/B shows what is left.
  - *Constants rebuilt only when dirty:* `Build_Constants` is 1.2%, about 0.1 ms. A dirty flag has to be
    set by every Set* path that feeds the blocks, and one missed path is a stale colour. The gain does not
    carry that risk yet.

### The long frames: the shared machine's memory pressure

**Instruments** (all off unless asked, committed on this branch):
- `ZH_GPU_TIMING`'s long-frame line: every frame over 50 ms, split into the device's draw, its flushes,
  its present and swapchain wait, and the engine's own rest.
- `ZH_GPU_CREATION_LOG`: every program, pipeline, sampler, texture, retexture, buffer and rebuffer, with
  its time and duration; a draw over 20 ms with its phases, the GPU copies split into textures, samplers
  and buffers; a mirror-lock wait over 5 ms; a texture call over 5 ms. Kept in memory and written once a
  present (8d3bef8e), so the log no longer stalls the draws it times; a write over 5 ms says so.
- `ZH_GPU_CREATION_TRACE=WxH`: the caller of every 500th texture of that size.
- `ZH_LOAD_TIMING`: every file open and read over 20 ms, and every W3D model and texture load over 20 ms,
  split into reading and building, with the thread.
- `vm_stat 1` beside each run, and the load and `kern.num_files` before it.

**What was found**, hidden skirmishes at 2560x1440, 19 runs in 4 batches:
- **No loading:** no file open or read, and no model or texture load, over 20 ms in the match.
- **No device cause:** in every slow draw, the flushes, the staging, the constants, the samplers and the
  buffers were 0.00 ms, and no mirror-lock wait reached 5 ms. That rules out the PM's suspects 1 to 3: a
  buffer reused while the GPU reads it, a ring growing or waiting for a fence, and the mirror's lock held
  by another thread.
- **Every device-side long frame coincided with a blocked write under measured memory pressure.** Each
  slow draw's time was one `SdlResourceMirrors::Texture` or `Buffer` call, and inside it the creation
  log's own unbuffered writes to stderr: 8 to 12 ms (runs A1, A2), 264 and 27 ms (G2), 551.46 ms of a
  551.70 ms draw (G3, the size of the 577 ms first seen), each to within 0.1 ms. One G2 call of 348 ms
  held a timed write of 269 ms; the other 79 ms came after it, where the only work left was the log's
  second, untimed line (its own report of the slow write).
- **None was seen in the unlogged runs** (B1, B2, C1, C2: device draw 1 to 5 ms in every long frame).
- **The engine-side stalls in C2 coincided with free pages at 15 MB:** 15 long frames in the match,
  1,096 and 950 ms among them, device draw 1 to 5 ms, logic at most 106 ms, so the engine's client and
  render work. `vm_stat` at the same second: 975 free pages, 150,000 decompressions and 247,000
  compressions. B1 had four frames of 120 to 225 ms at 3 to 4 s into the match, the worst a 222 ms
  logic tick, with free pages around 60 MB.
- **The machine:** 36 GB, shared by several agent sessions each running copies of the game (about 0.9 GB
  each), OrbStack (1.8 GB) and the user's own applications; about 2.1 million pages (34 GB) in the
  compressor; the load 15 to 64 during these runs. The game itself was about 0.4 GB.
- **Fix 3 neither causes nor hides it:** the G runs were on fix 1 and the instruments only.
- **Excluded:** runs A3, B3 and F1 to F3 (22:25 to 22:31), when two other harnesses had filled the
  machine's file table. They were rerun as G1 to G3 and C1 to C2.
- **What this could not see, until finer:** there was no run on an unloaded machine. On finer (below),
  24 runs had no long frame in steady state. Free pages there fell as low as here, about 60 MB, but the
  compressor was idle: at most 110 decompressions and no compressions a second, against this Mac's
  151,000 and 247,000 at C2's stall. What stalls this Mac is the compressor's churn under the shared
  load, not the free-page count alone.

**The load-time frames**, measured, with no action:
- **The shell build:** one frame of 655 to 839 ms at 0.3 s after the first present, all the engine's own.
- **First use:** one frame of 88 to 123 ms at about 2 s (299 ms once, at load 62): the first-use
  program compiles (29 to 66 ms of device draw) and the engine's own rest.

### Observations

- **Text textures, made anew every frame.** About 3,500 64x64 A4R4G4B4 textures a match, two most frames:
  `PosixDevice9::CreateTexture <- D3DX9Posix_Create_Texture <- DX8Wrapper::_Create_DX8_Texture <-
  TextureClass::TextureClass <- Render2DSentenceClass::Build_Textures <- Render2DSentenceClass::Render <-
  W3DDisplayString::draw`, called from `Drawable::drawConstructPercent`, `Drawable::drawHealthBar` and
  the HUD overlay's `litehtml::el_text::draw`. Text that changes gets new textures. On Windows the driver
  absorbs it; here each creation costs a mirror texture and an upload. A candidate for a glyph or
  text-texture cache only if a profile shows the cost.
- **A crash after the Mac slept:** a run's present blocked through a 17-minute clamshell sleep, and the
  process crashed one second after waking, in `WindowLayout::hide` (null) from
  `GameLogic::clearGameData` on the -maxframes exit. Handed to -18 as a player question: does closing a
  MacBook's lid mid-game crash it?

### A second machine: finer (2026-09-27)

**The machine.** A MacBook Pro 14-inch (Mac15,6): M3 Pro, 5 performance and 6 efficiency cores (as this
Mac), 36 GB, macOS 26.5.2. The lid is closed, it's on AC, and `pmset -a disablesleep 1` is set (the
user's approval; it's on the workers' revert list). There's no display and no window server for zhr, so
every run is `-offscreen` (decision 10). "Native resolution" is the panel's specification, 3024x1964,
because no display was attached for zhr. Low power mode is off.

**The runs.** The fix-3 build (`perf1-finer-run`: this branch with `-offscreen` merged in, plus the
FFmpeg fix), from `ssh zhr@finer.local`:
- `-offscreen`, `ZH_OFFSCREEN_HZ=120` (frames paced at 120 a second in place of vsync), `-noaudio`,
  `-overlay <build>/overlay`, the HUD on;
- the same scenes, frames and timing windows as this Mac's matrix, three runs a configuration;
- `zheavy` held for each whole batch;
- 60 s idle before every run;
- the load, `vm_stat 1`, and the thermal pressure and cluster frequencies (`powermetrics`) before, 40 s
  in and after each run.

Every thermal reading was Nominal, the load was 1.2 to 2.1, and there were no sleeps.

| Scene, resolution | Frame p50 / p99 / worst (ms) | Work p50 / p95 / p99 (ms) | Draws | Device draw p50 | Record+submit mean | Engine CPU mean | Engine match p99 / worst | Long frames in steady state |
|---|---|---|---|---|---|---|---|---|
| skirmish, 800x600 | 8.33 / 8.46–8.60 / 16.31–16.67 | 4.61–4.68 / 5.71–5.75 / 7.00–7.34 | 1786 | 2.20–2.23 | 0.71–0.72 | 1.79–1.83 | 9.25 / 16.53–16.71 | 0, 0, 0 |
| skirmish, 1920x1080 | 8.33 / 8.35–8.74 / 16.66 | 4.31–4.67 / 5.25–5.58 / 6.86–7.53 | 1570 | 1.97–2.22 | 0.64–0.68 | 1.74–1.85 | 9.25–9.50 / 17.21–18.58 | 0, 0, 0 |
| skirmish, 3024x1964 | 8.33 / 8.34–8.83 / 8.84–16.67 | 3.40–3.90 / 4.64–4.92 / 6.23–7.38 | 919 | 1.40–1.57 | 0.47–0.52 | 1.67–1.82 | 9.25–9.50 / 23.77–24.55 | 0, 0, 0 |
| shell, 800x600 | 8.33 / 8.50–8.79 / 16.67–25.00 | 4.49–4.65 / 5.88–6.02 / 7.25–8.32 | 1336 | 1.73–1.79 | 1.42–1.48 | 1.46–1.51 | — | 0, 0, 0 |
| shell, 1920x1080 | 8.33 / 8.33–8.50 / 16.67 | 4.29–4.51 / 5.58–5.84 / 6.06–6.99 | 1187 | 1.69–1.77 | 1.07–1.16 | 1.48–1.68 | — | 0, 0, 0 |
| shell, 3024x1964 | 8.33 / 8.34 / 16.67 | 3.22–3.39 / 4.22–4.45 / 4.76–5.09 | 1034 | 1.48–1.56 | 0.62–0.65 | 1.20–1.27 | — | 0, 0, 0 |

- **Record+submit** here is the present less the offscreen wait: the gamma pass into a texture of its
  own, with no swapchain.
- **Long frames in steady state** means frames over 50 ms after 10 s into a skirmish (the match
  under way), or after 120 s in the shell. The shell has a burst of about 9 frames a second between 71
  and 76 s, before its measured window; it isn't counted.
- **The 3024x1964 rows are reruns,** on the same build with one fix to the timing aid (60506d38). The
  engine makes a device and replaces it before the first frame at that size, and the replaced device's
  teardown used up the one report, so the first six 3024 runs reported "0 frames". The fix touches
  only the report. The skirmish draws only 919 a frame at this size, against 1,570 at 1920x1080. That
  is noted, not explained.

**The hitch is absent on the quiet machine.** No steady-state long frame appeared in any of the 24
runs, and the engine's own worst match frame was 16 to 25 ms.
- Free pages fell to about 3,600 (roughly 60 MB) in some of these runs too, with no stall. So low
  free memory alone is not what stalled this Mac. The compressor churning was.
- Per second at most, measured by `vm_stat 1`:

  | Machine and run | Decompressions | Compressions | Page-ins |
  |---|---|---|---|
  | finer, all runs | 110 | 0 | 1,902 |
  | this Mac, B2 (a clean run) | 24,658 | 43,911 | 17,159 |
  | this Mac, C2 (the 842 ms stall) | 150,674 | 246,572 | 39,493 |

**The content, bridged.** This Mac's baseline had the overlay's loose files copied into its farm, but not
its three art archives (ReforgedNormals, ReforgedTerrain and ReforgedTextures.big). The archive lists in
the debug logs show it. So this Mac's baseline equals "the overlay minus the three art archives". A
run with no overlay at all can't start ("could not open 'Data\INI\FXListReforged.ini'"); three such
bridge runs failed at once and are discarded. The bridge that was run instead uses the overlay's 40
loose files without the archives: skirmish 1920x1080, three runs.

| skirmish, 1920x1080 | Work p50 / p95 / p99 | Device draw p50 | Record+submit | Engine CPU | Engine match worst |
|---|---|---|---|---|---|
| the full overlay | 4.31–4.67 / 5.25–5.58 / 6.86–7.53 | 1.97–2.22 | 0.64–0.68 | 1.74–1.85 | 17.21–18.58 |
| without the three archives | 4.37–4.67 / 5.33–5.75 / 7.26–8.45 | 2.00–2.21 | 0.64–0.67 | 1.76–1.86 | 17.31–17.70 |

The archives cost nothing measurable.

**finer against this Mac: the machine, not the present path.** finer's work is higher than this Mac's
after fix 1: 4.3 to 4.7 ms at p50 against 3.1 to 3.4, with device draw at 2.0 to 2.2 ms against 1.2 to
1.4. It is the same chip and core layout, low power mode is off, the thermals were Nominal, and the
bridge rules out the content. To separate the present path from the machine, finer's own binary
(`generals-fix3b`) was run on this Mac twice. The PM approved this as the one exception to "no work on
this Mac": the seed-1234 skirmish at 1920x1080, silent, off-peak (load 5.1 to 5.6), the farm's content
(the overlay minus the archives, which the bridge showed to be equivalent).

| This Mac, the same binary | Work p50 / p95 / p99 (ms) | Device draw p50 (ms) | Draws p50 | Engine match worst (ms) |
|---|---|---|---|---|
| `-hiddenwindow` (vsync) | 3.26 / 3.91 / 4.52 | 1.28 | 1580 | 33.7 |
| `-offscreen`, `ZH_OFFSCREEN_HZ=120` | 3.06 / 3.77 / 5.01 | 1.23 | 1547 | 25.6 |
| finer, `-offscreen` (the matrix, three runs) | 4.31–4.67 / 5.25–5.58 / 6.86–7.53 | 1.97–2.22 | 1570 | 17.2–18.6 |

The present path makes no measurable difference, so finer's extra 1.2 ms of work, 0.8 ms of it in the
device's recording, belongs to the machine or its OS: macOS 26.5.2 against 27.0, or the clocks of a
lid-closed laptop in a closet, whose P-cluster read 2.4 to 3.8 GHz 40 s into runs. Those two are not
separated. finer's numbers are right for comparing builds on finer, not for comparing with this Mac.

**Not the local LLM either.** For all the finer runs above, the user's local LLM stack was resident there
(mlx-dspark serving a 27B model, and LM Studio: about 28 GB resident, 21 GB of it compressed). It was
stopped at the user's word, and the skirmish at 1920x1080 was run again: the same binary and settings,
three runs, the lock held, all Nominal, no sleeps. Before the runs there were 1,110,029 free pages
(17 GB), and the compressor held 47,199 pages.

| skirmish, 1920x1080 on finer | Work p50 / p95 / p99 (ms) | Device draw p50 | Record+submit | Engine CPU | Engine match worst | P-cluster 40 s in |
|---|---|---|---|---|---|---|
| the LLM resident (the matrix) | 4.31–4.67 / 5.25–5.58 / 6.86–7.53 | 1.97–2.22 | 0.64–0.68 | 1.74–1.85 | 17.2–18.6 | 2022, 2097, 2149 MHz |
| the LLM stopped | 4.44–4.71 / 5.47–5.73 / 7.70–8.32 | 2.01–2.21 | 0.65–0.68 | 1.77–1.88 | 17.5–18.0 | 2123, 2059, 2116 MHz |

Nothing moved, so the gap to this Mac is the OS or the clocks. The one clue: 40 s into every one of these
runs, finer's P-cluster read about 2.0 to 2.2 GHz, where an M3 Pro's performance cores reach about
4 GHz. That suggests the cores run slowly under this load there: a closed lid's thermal or power
policy, or how macOS treats work started from an ssh session with no GUI login. This Mac's clock was
not read (`powermetrics` needs sudo here), so the comparison is not closed.

**Installed or created on finer:** listed, with how to undo each, in `docs/mac-port/tasks/workers.md`
(probe-a9, since deleted; perf-a9 with its farm, bundles, scripts and logs; wt-a9 and its two run
branches; build-a9).

**Not the throttle tiers either** (the PM's cheap test). The skirmish at 1920x1080 was run once under
`taskpolicy -t 0 -l 0` (no I/O throttle or timer-latency tier), as zhr, without sudo. The P-cluster read
2,085 MHz 40 s in, as in the other runs (2,022 to 2,149), and work p50 was 4.33 ms. A plausible reading,
not measured: at 120 frames a second the main thread is busy about half the time, so the performance
cores never ramp. The gap to this Mac stays open.

## Same-result speed-ups (the user's rule, 2026-09-27)

The user's rule: "any safe performance improvement chance should be taken if and only if it will produce
the same result guaranteed". The PM ranked the candidates from finer's engine profile and set the order:
5, 1, 3, then L2, then 2, 4, 6. Each is one commit, with:
- an ABBA A/B on finer (skirmish and mobstress at 1920x1080, offscreen at 120 Hz, silent, with the
  overlay; six runs per binary per configuration; the machine's lock held exclusively for the batch; 60 s
  idle before each run; thermal pressure and cluster frequencies logged);
- the suite;
- the proof that the result is the same.

**How rendering sameness is proven.** A screenshot of the running game cannot show it. Two runs of the
SAME binary give different pixels (the HUD's live fps and time; particles and animation follow wall-clock
time), so that method fails its own control. Instead, ONE capture set is replayed through the base and
candidate builds, and every capture's GPU read-back is compared byte for byte (`FFREF_GPU_DUMP`). The
same draws with the same state give the same bytes, whatever the game's timing did. The comparison is
armed: with one byte flipped in each texture on the device side (`FFREF_FLIP_TEXEL`), it must report
differences. Capture signatures are compared too, over several runs of each binary.

### Candidate 5: samplers found by their state's bytes (379448bb)

The sampler cache keyed each lookup by a 56-byte `std::string`, past the string's inline buffer, so
every lookup allocated: up to 8 a draw. The key is now the 14 states, hashed and compared by their bytes,
with a last-used fast path. The same states give the same sampler.

| finer, medians of six runs, base / candidate | Work p50 (ms) | Device draw p50 (ms) | Work p99 (ms) |
|---|---|---|---|
| skirmish, 1920x1080 | 4.522 / 4.431 | 2.064 / 1.903 | 7.467 / 7.405 |
| mobstress, 1920x1080 | 10.613 / 9.661 | 4.256 / 3.703 | 13.860 / 13.386 |

The means agree with the medians in direction and size. The gain grows with the draw count: 0.16 ms of
device draw in the skirmish, and 0.55 ms (13%) under mobstress. All thermal readings were Nominal, and
there were no sleeps.

**The same result:**
- the suite, 84 of 84;
- the pixel proof: 48 of 48 captures byte-identical, 0 differ, and the armed control reports 3;
- the FFReference replay: 48 compared, 0 failed, the known C1/C5 findings, on base and candidate alike.
- Signatures: the candidate's first capture had two signatures more than base's (the same programs under
  two pipeline keys, from engine-set render states: no culling, a stencil test, alpha test at 96). Seven
  captures settle it. The base binary gave 48, 48, 50 and 50; the candidate 50, 50 and 50. The base
  binary itself shows both counts, so those two are what a 600-frame run happens to reach, not the
  candidate.

### Candidate 1: the allocator's lock is an unfair lock with an owner on Apple (9fec1f19, 665df643)

Fix 2 above, taken now. On Apple the `CriticalSection` behind the allocator's four locks is an
`os_unfair_lock` with an owner and a depth (`RecursiveUnfairLock`, CriticalSection.h). Before, it was
libc++'s `std::recursive_mutex`, a recursive pthread mutex. Recursion is kept exactly: the owner
re-entering counts up, and the lock is released at zero. The allocator's code doesn't change, so on one
thread the same calls get the same blocks in the same order. Which thread wins a contended lock was never
ordered (neither CRITICAL_SECTION nor a pthread mutex is FIFO).

An exit by a thread that doesn't hold the lock traps, in every build (-18's second read). Windows and
Linux keep `std::recursive_mutex`.

Both builds are at the older base, a9-base-sampler (before 9f17c203), per the PM's same-side rule.

| finer, medians of six runs, base / candidate | Work p50 (ms) | Device draw p50 (ms) | Work p99 (ms) |
|---|---|---|---|
| skirmish, 1920x1080 | 4.412 / 4.330 | 2.010 / 1.847 | 7.527 / 6.878 |
| mobstress, 1920x1080 | 10.601 / 8.871 | 4.281 / 3.397 | 13.815 / 12.669 |

- Under mobstress the candidate is faster by 16% of work and 21% of device draw. The device allocates
  under these locks too.
- The six candidate runs span 3.386 to 3.400 ms of device draw; the six base runs span 4.201 to 4.302.
- Every thermal reading was Nominal, and there were no sleeps.

**The same result:**
- the suite, 84 of 84;
- the FFReference replay: 48 compared, 0 failed;
- the E1 CRCs, base and candidate compared at the older base: identical, all 10 lines of replay_check and
  net_check (seed 0 at 1200 frames 0x0177BEF6, seed 1 at 12000 0x7C7DBA69, the net check's two copies);
- merged with feature/mac-port (9f17c203's CRCs): the suite, 86 of 86, with seed 0 at 1200 frames 0x0177BEF6
  and seed 1 at 12000 0x830467DB;
- signatures: the candidate's first capture had 48, the base's 50 and 50. Seven captures settle it: the
  base binary gave 50, 50, 48 and 50, and the candidate 48, 50 and 50. The same two signatures come and go
  in both binaries, so it's timing, as it was for candidate 5.

**The locks under the sanitizers.** The standard here is B8's and B11's: a harness around the real
header, and a control proving the sanitizer can go red. The harness compiles candidate 1's actual
`CriticalSection.h` (sha256 4cfcca66…) with a shim for `PerfTimer.h`. It checks three things:
- **Recursion:** the owner re-enters three deep, and another thread gets the lock only after the last exit.
- **Contention:** 8 threads each make 20,000 increments of a plain int, every other one inside a nested
  enter.
- **The trap:** a thread that doesn't hold the lock exits it, and the child process must die of SIGTRAP.

Results on finer, AppleClang 21:
- Plain, TSan, and ASan+UBSan: all pass. The counter is 160,000 of 160,000, with 0 races, 0 ASan and 0
  UBSan reports.
- The trap check ran in the plain and ASan builds, and the child died of signal 5. TSan doesn't support
  fork with threads, so it doesn't run there.
- The controls:
  - The contention with no lock, under TSan, reports a data race, and the counter lands on 60,000.
  - A planted heap overflow, under ASan, is reported.

The whole game couldn't run under either sanitizer at first, with any lock. TSan aborted before main.
GameMemory replaced the unsized global operators but not the sized deletes or the nothrow forms, which
the sanitizer runtime then supplied. That was fixed separately (feature/mac-port-sized-delete, merged). A
sanitizer build of the whole game, GPU included, is ZH_SANITIZE's (feature/mac-port-sanitize).

**The lock-wait tails** answer the PM's fairness rule: no thread's worst wait may grow past what it
tolerates (audio, one buffer period; workers, a frame).
- The setup: a measurement build (`ZH_LOCK_WAIT_MEASURE`: wall time around each `enter`, per thread,
  printed at exit), and mobstress offscreen at 1920x1080, 6000 frames, base, candidate, candidate, base.
- The audio thread runs: the null backend with `ZH_ALLOW_AUDIO=1`, silent. CoreAudio's log showed no
  line from the game in any of the four runs, and the silent afplay control after each run showed up in
  that same log.
- The results, base / candidate:
  - The busiest thread after main, about 14,300 acquires: worst wait 0.255 and 0.277 ms / 0.014 and
    0.007 ms.
  - Every other thread: at most 0.075 / 0.030 ms.
  - Main: at most 0.074 / 0.030 ms. Its total time inside `enter` fell from 26.2 s to 22.9 s over about
    2.4 billion acquires.
  - No wait over 1 ms, in any thread or run.
- The candidate's worst waits are lower than the base's on every thread. Both are orders of magnitude
  below a buffer period or a frame.
- Only the main thread is named in the output. The verdict doesn't need the others' names, because every
  thread is far inside the smaller of the two tolerances.

x86_64 macOS (Rosetta, installed by the user 2026-09-27): feature/mac-port 1f4229c3, which has the lock,
built with `-DCMAKE_OSX_ARCHITECTURES=x86_64` and nothing else. replay_check under Rosetta gives the pins:
0@1200 0x0177BEF6, 0@12000 0x5273770F, 1@12000 0x830467DB, the same as arm64. So the lock is proven on both
Mac architectures.

### Candidate 3: the constant blocks reused, measured and not taken (feature/mac-port-perf-constants)

Each draw rebuilt its constant blocks: the matrices, the material, the lights and the states they're built
from. The candidate snapshotted those inputs and reused the previous draw's blocks when the snapshot's bytes
matched.

It gave the same result:
- the suite passed, 84 of 84;
- the guard is armed: with the fog colour left out of the snapshot, the self-check fails ("step 15, a render
  state it reads"), and restored, it passes;
- the signatures were 50, 50 and 50;
- the FFReference replay compared 50 captures and 0 failed;
- the pixel proof found 50 of 50 dumps byte-identical, and the armed control reports 3.

It wasn't faster:

| finer, medians of six runs, base / candidate | Work p50 (ms) | Device draw p50 (ms) | Work p99 (ms) |
|---|---|---|---|
| skirmish, 1920x1080 | 4.402 / 4.377 | 2.013 / 2.052 | 7.319 / 7.744 |
| mobstress, 1920x1080 | 10.517 / 10.558 | 4.288 / 4.226 | 13.974 / 13.849 |

- Under mobstress the device draw was 1.4% lower, but the per-run ranges overlap (base 4.219 to 4.314,
  candidate 4.211 to 4.245), and work went the other way.
- In the skirmish the difference is noise.
- All thermal readings were Nominal. Each ABBA block held the machine alone.

**Not taken** (the PM, 2026-09-27). A cache that can go stale is a risk, and with no measurable gain it isn't
an improvement. The branch stays unmerged, for reference.
