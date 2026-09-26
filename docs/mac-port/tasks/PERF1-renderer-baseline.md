# PERF1 — A performance baseline of the Mac renderer

- **Milestone:** M4
- **Depends on:** A3e
- **Status:** in progress: the timing aid is built and the measurement matrix is running
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

## Results

To come.
