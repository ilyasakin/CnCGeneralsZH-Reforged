# PERF1 — A performance baseline of the Mac renderer

- **Milestone:** M4
- **Depends on:** A3e
- **Status:** in review: measured and profiled; the three largest costs and their fixes are proposed to the PM, and nothing is changed yet
- **Owner:** -a9

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
- **Long frames:** three of the twelve skirmish runs had a single frame of 0.5 to 0.65 s. It fell at a
  different logic frame each time (246, 290, 747), which isn't something the seeded game does. The load
  reached 20 to 30 in those minutes, so it is most likely the shared machine. Not explained, and not
  pursued.

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
