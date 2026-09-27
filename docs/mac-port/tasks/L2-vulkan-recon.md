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

## Answered since (2026-09-27)

- **F7 and F8 are the same on three backends.** -a9 ran ffref_gpu_selfcheck on Metal (finer): F8
  3135 outside, worst 248/255 at (59, 4); F7 2970 outside, worst 248/255 at (5, 5), the same as ANV's.
  Lavapipe gives the same numbers too (below). Three implementations agree pixel for pixel, so both
  findings sit on the device's or the reference's side, not in any driver. F8 is filed as flat shading
  and F7 as N7 (specular add with no vertex specular). For F8, what is measured is the worst pixel, GPU
  5 250 0 against reference 255 0 0; which vertex supplies the flat colour on each side has not been
  checked (-a9's correction: an earlier "first vertex" reading was an inference).
- **The refusals are the device's own limits, on every backend** (-a9, from the code; no Metal count for
  these runs). Table fog and fog from the specular alpha are the fixed-function path. Cube and volume
  textures are refused because the resource mirror is 2D-only. Sampling the target being drawn into is
  refused up front because it is an error in SDL3 GPU. Border and mirror-once addressing are refused
  because SDL3 GPU has only repeat, mirrored repeat and clamp to edge. The refused program comes from the
  backend-independent generator.
- **Lavapipe is installed** on thinkerer (vulkan-swrast 26.2.2, llvmpipe on LLVM 22.1.8), with the PM's
  approval, and logged in workers.md. `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json` selects it
  (the loader's debug output confirms only that ICD loads). Without it SDL picks ANV, because SDL ranks CPU
  devices last. The six selfchecks pass on it, and ffref_gpu's numbers equal ANV's.
- **-a9's -offscreen** will fall back per platform: SDL's dummy driver plus the Metal hint on Apple, the
  offscreen driver on Linux.

## What this cannot see

- The game drawing a frame on Vulkan (`-offscreen` in the game itself): not run. It is the device's
  territory.
- Any hardware Vulkan other than Mesa ANV on Kaby Lake: no AMD, no NVIDIA. Lavapipe is the one other implementation measured.
- Presentation to a real window (X11 or Wayland) on Linux.
- Whether FFReference judgements from Linux captures agree with Metal's. That is -47's later task (3),
  from the capture format. Done for the first Vulkan set: see the next section.

## Judging the first Vulkan capture set (FFReference's own replay)

By -47, 2026-09-27. The set: -a9's run4 on thinkerer. It's the seed-1234 skirmish at 800x600, 600 frames,
`-offscreen` on Mesa ANV, drawn by feature/mac-port-l2 4f5fc67c: 49 captures, 6 of them programmable, and 28
textures. It was frozen by a reflinked copy before judging.

### The tool

`Tests/ffreference/ffref_judge` with `ffcapture.h/.cpp`, both -47's. It reads the captures itself, from -a9's
written descriptions below, never from the device's writer or the harness's reader. It decodes the textures
from the D3DFORMAT and block-compression pages, the programs with `ffprogram.h`, and the vertices from the
D3DFVF page. It draws each capture with FFReference. The GPU's pictures come from -a9's harness,
`ffref_capture_selfcheck` with `FFREF_GPU_DUMP`, run by -47 on the frozen copy as a black box, with its own
output discarded.

**The format, as described by -a9 (the contract):**
- `draw_NNNNN.cap`: "ZHDC", version 1 (fixed function) or 2 (programmable, with a .prog beside it). Then:
  - A 5132-byte header: primitive type and count, FVF, stride, vertex and index counts, the target's size
    and format, DepthBound, the viewport, RenderStates[256], StageStates[8][33], SamplerStates[8][14],
    World, View, Projection, the 8 texture matrices, D3DMATERIAL9, 8 D3DLIGHT9 of 104 bytes, the 8
    lights' enabled flags, the 8 stages' .tex names (48 bytes each), and a 512-byte signature.
  - Then VertexCount x Stride bytes from the first vertex the draw reads.
  - Then IndexCount u32 indices, rebased to that vertex.
- `NAME.tex`: "ZHTX", D3DFORMAT, width, height and levels; then per level a u32 byte count and the bytes.
- `draw_NNNNN.prog`: "ZHPG", version 1 or 2. It holds the vertex and pixel sections (Present, a 64-byte
  name, the token count, the tokens), and after the vertex section the D3D9 elements. The elements are
  present only while the declaration is the current format. Then VS c0-c95 and PS c0-c7. Version 2 adds
  the engine's D3D8 declaration through D3DVSD_END, and the streams (stream, stride, first vertex).

**The replay's starting point, as described by -a9:**
- Before each draw the whole target is cleared to 0xFF3F2F1F, depth to 1.0 and stencil to 0 (D24S8). Then
  the captured viewport is set.
- With no depth-stencil bound, depth and stencil are off.
- SCISSORTESTENABLE without a rectangle keeps the device's default rectangle, the whole target.
- ANISOTROPIC min and mag filters are replayed as LINEAR on both sides, and counted.
- The GPU dump is `<capture>.cap.bgra`: the back buffer after the draw, B G R A bytes, top row first.

### The verdicts

| | captures |
|:--|:--|
| agree: every pixel exact or inside a documented freedom, with something written | 37 |
| empty: nothing written on either side (the target left at the clear colour) | 11 |
| refused by FFReference | 1: draw_00019, "trapezoid water ps.1.1": r0 is not fully written by the end, so its unwritten channels have no documented value (known; the harness refuses it too) |
| disagree | 0 |
| unreadable | 0: every file matched the description byte for byte |

- 46 draws had ANISOTROPIC replayed as LINEAR, on both sides.
- Six captures draw into 512x512 targets, the rest into 800x600.
- Five of the six programmable captures are judged: terrainnoise2.pso (draw_00005) and Trees.vso with
  its fixed-function pixel half (draw_00014, 15, 29, 30). The sixth, the trapezoid water, is the refusal.
- Where the reference wrote pixels and the GPU changed none from the clear colour, the reference's colour
  there is the clear colour too: passes that leave colour unchanged, and the comparison agreed pixel for pixel.

**-a9's own numbers**, given only after this judgement: 49 compared, 11 drew nothing on the reference, 0
failed, 46 ANISOTROPIC replayed as LINEAR. They agree on every capture but one:
- **draw_00019, the trapezoid water.** The game assembles its ps.1.1 at run time through the port's D3DX
  stub, which doesn't assemble. It emits placeholder tokens that carry the program's text.
- -a9's harness reads the text out and assembles it with FFReference's own assembler. The device draws the
  program by its registered name.
- This judge takes the captured tokens literally and refuses them.
- So it's a difference in what was replayed, not in pixels. -a9 will describe the stub's layout in words, so
  that this judge can read the text out itself.

**Armed controls on the same set, so an "agree" is a comparison that can fail:**
- **Each capture against the next capture's GPU picture:** 37 disagree, and 8 can't be compared because
  the neighbour's target has another size.
- **FFReference's own deliberate departures:**
  - D3D10 pixel centres: 30 disagree.
  - Texel centres at the corner: 27 disagree.
  - Swapped blend factors: 12 disagree.
  - No perspective correction: 1 disagrees.
  - No specular add, reversed fog, swapped LERP arguments, unattenuated lights and the top-left rule
    turned off: 0 each. So this set doesn't exercise specular, fog, LERP, point or spot lights, or exact
    edge ties, and says nothing about them.

**What this cannot see:**
- Anything the 49 draws don't exercise (the list above).
- The water reflection, which samples a render target and isn't captured.
- A draw's interaction with the previous draw's depth: every capture starts from a cleared target. A pass
  that tests depth against what an earlier pass wrote draws nothing that way, on both sides. That may be
  why some of the 11 are empty; it wasn't checked per draw.
- The GPU dump comes from -a9's harness. Its replay of a capture is trusted as described, not checked
  against a second GPU path.
