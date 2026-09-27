# L2 recon: SDL3's GPU API on Vulkan, windowless, on thinkerer

By -47, 2026-09-27, for -a9, who owns the Vulkan device bring-up (the PM's L2 ruling). This is written
from SDL3's own source (the vendored 3.4.16), thinkerer's drivers, and the output of test binaries. No
A3 code (PosixDevice9*, the capture and replay harness, engineshader, ffshader, ffvertex, dx11backend)
was read for it: the independence rule stands, and -47 judges Linux captures against FFReference later.

## The host

| | |
|:--|:--|
| GPU | Intel UHD Graphics 620 (Kaby Lake GT2, 8086:5917) |
| Driver | Mesa 26.2.1 ANV (`intel_icd.json`), Vulkan 1.4.354, conformance 1.4.0.0 |
| Other ICDs installed | `intel_hasvk` (Gen7/8, not this GPU), `radeon` (no AMD GPU) |
| Software Vulkan | none: lavapipe (`vulkan-swrast`) is not installed |
| Render node | `/dev/dri/renderD128`, mode 0666: usable by `zhr` with no group |
| Display | none over ssh (`DISPLAY` and `WAYLAND_DISPLAY` unset) |
| Instance extensions of note | `VK_EXT_headless_surface`, `VK_KHR_surface`, `VK_KHR_display`, `VK_KHR_xlib_surface`, `VK_KHR_wayland_surface` |

## What SDL3 needs to make a Vulkan GPU device

From `src/gpu/vulkan/SDL_gpu_vulkan.c` and `src/video`:

1. `VULKAN_PrepareDriver` refuses unless the video driver has a `Vulkan_CreateSurface`, and loads the
   Vulkan library through the video driver.
2. Device selection asks `SDL_Vulkan_GetPresentationSupport` for each queue family, and takes only a
   graphics family that can present. A video driver with no `Vulkan_GetPresentationSupport` hook counts
   as "always supported".
3. SDL's **offscreen** video driver (`SDL_VIDEO_DRIVER_OFFSCREEN`, built in: `SDL_build_config.h` has it,
   `SDL_VIDEO_VULKAN` and `SDL_GPU_VULKAN`) provides `Vulkan_CreateSurface` through
   `VK_EXT_headless_surface`, and asks for `VK_KHR_surface` plus that extension. It has no presentation
   hook, so every graphics family qualifies.
4. SDL's **dummy** driver has no Vulkan surface, so the Vulkan backend refuses it. That is the Mac's
   route (dummy plus the Metal patch), and it does not carry over.

So on Linux, the windowless route is `SDL_VIDEO_DRIVER=offscreen`, and **it needs no SDL patch**.

## Measured

A 60-line SDL-only probe: `SDL_Init(VIDEO)`, `SDL_CreateGPUDevice(SPIRV)`, clear a 64x64 RGBA8 target to
(0.25, 0.5, 0.75, 1), download it, and read one pixel.

| `SDL_VIDEO_DRIVER` | result |
|:--|:--|
| `offscreen` | GPU driver `vulkan`, Intel UHD 620 / Mesa 26.2.1; pixel 64 128 191 255: exact |
| `dummy` | `SDL_CreateGPUDevice`: "No supported SDL_GPU backend found!" |

The six SDL GPU selfchecks, run as test binaries with `SDL_VIDEO_DRIVER=offscreen` (L1b puts that in their
Linux ctest environment, 42513f12). All six pass, and `-V` shows each ran its checks:

- sdl_gpu_frame_selfcheck: vulkan. Clear, depth-only clear, gamma (its control differs in 3072 of 3072
  pixels) and the identity blit hold.
- sdl_program_cache_selfcheck: vulkan, 27 programs built in 46 ms, 1 refused by the generator.
- sdl_pipeline_state_selfcheck: vulkan, 2 pipelines; layout, keys and samplers hold. One sampler refused
  (border or mirror-once addressing).
- posix_gpu_draw_selfcheck: vulkan, 672 draws. Refused: sampling the render target being drawn into, and
  a cube or volume texture. The indexed shared edge differs from per-primitive at 0 pixels.
- ffref_gpu_selfcheck: vulkan, 62 scenarios, 0 failed, and 2 known findings: F8 flat shading (3135 outside,
  worst 248/255) and F7 absent specular / N7 (2970 outside, worst 248/255). One draw refused (table fog).
  lod probes: mean gpu - reference +0.000, +0.010 and +0.027.
- ffref_capture_selfcheck: 4 captures, 3 compared, 0 failed. The armed control was found (209 pixels).
  River water ps.1.1 was not replayed: the reference refuses it (r0 not fully written).

Before L1b these six were "Skipped" on Linux with `SDL_Init: No available video device`.

## Questions for -a9

- Are F7 and F8, and their pixel counts, the same on Metal? If not, the difference is the first thing to
  look at on Vulkan.
- The refusals above (table fog, cube/volume textures, sampling the target, border and mirror-once
  samplers): are they the device's known A3c limits on every backend, or anything Vulkan-specific?
- Should a software implementation (lavapipe, `sudo pacman -S vulkan-swrast`) be a second Vulkan to
  compare against? Nothing is installed for it yet. It would be logged in workers.md first.

## What this cannot see

- The game drawing a frame on Vulkan (`-offscreen` in the game itself): not run. It is the device's
  territory.
- Any Vulkan other than Mesa ANV on Kaby Lake: no AMD, no NVIDIA, no lavapipe.
- Presentation to a real window (X11 or Wayland) on Linux.
- Whether FFReference judgements from Linux captures agree with Metal's. That is -47's later task (3),
  from the capture format.
