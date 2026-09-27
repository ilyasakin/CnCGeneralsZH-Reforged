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

- The game drawing a frame on Vulkan (`-offscreen` in the game itself): not run at the time. Done since
  (feature/mac-port-l2); see "The denser set" below.
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
- **Settled.** -a9 described the layout in words: word 0 the version token; word 1 a comment token of N words;
  word 2 the tag "ZHSR"; word 3 the text's byte count L; the text, zero-padded; then END, with no
  instructions.
  - ffref_judge now recognises exactly that shape. It checks N >= 2, that L fits the comment, and that the
    comment fits the stream; anything else is taken as real tokens.
  - It reads the text out itself and assembles it with FFReference's assembler.
  - draw_00019 then agrees exactly: 665 pixels written on both sides, all 480000 exact. Three mutations catch
    it (65, 41 and 665 outside).
  - **The set's verdict is now 38 agree, 11 empty, 0 refused, 0 disagree, 0 unreadable.** That matches -a9's
    49 compared, 11 drawing nothing.

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

## The denser set, and the synthetic one (2026-09-27)

By -a9. The judged set above exercises no specular, fog, LERP, point or spot lights, so -47 asked for more.

**What the game draws at all.** Three scenes were captured on ANV, `-offscreen`, `-noaudio`, from
feature/mac-port 06c60545. They're frozen read-only at thinkerer:`~/zhr-worker/l2-a9/set2`:
- mobstress on Alpine Assault, 4000 frames: 35 captures;
- the shell map, about 25 minutes: 215 captures;
- the seed-1234 skirmish, 6000 frames: 93 captures.

A census of the header fields, run over those 343 and over run4's 49, found:
- Point lights: 136 captures in the shell map and 39 in the skirmish. They come from the explosions' light
  pulses. There are none in run4 or mobstress.
- None of the following, anywhere: fog, specular, spot lights, a LERP stage, SCISSORTESTENABLE, or
  DepthBound = 0.

The census has an armed control: one real capture, copied with each feature planted in turn, is counted
every time. Specular is planted both lit and unlit. The census's first version counted specular only on lit
draws; the control caught nothing because it planted specular lit only. Both have been fixed.

The source says which of these absences are for good:
- **Fog.** D3D fog needs the scene's `FogEnabled`, which starts false. Only `Set_Fog_Enable` changes it, and
  nothing calls that (scene.cpp:132, scene.h:125).
- **LERP.** No stage op the engine emits is LERP; `D3DTOP_LERP` appears only in names and in the device.
- **Scissor.** Nothing outside the device sets a scissor rectangle or SCISSORTESTENABLE.
- **Specular is reachable.** shader.cpp:1018 enables it from a model's secondary gradient; none of these
  scenes draws such a model. (-a9 first called specular unreachable too, and corrected that the same day.
  W3DWater's TRUE is in dead code.)
- **Spot lights and DepthBound = 0** are absent by count only.

**Driver or device.** -a9's replay of set2 against FFReference:
- On ANV, 343 of 343 pass.
- On lavapipe, 342 of 343 pass. The process loaded `libvulkan_lvp.so`.
- The exception is the shell map's draw_00002: 2 pixels outside the envelope, of 478,601 written, about
  10 levels per channel. At the worst pixel, (363, 326), ANV's output equals the reference exactly and
  lavapipe's doesn't.
- **Recorded as unclassified: the driver or a tolerance edge, ANV exact. It isn't a device finding.**
- 128 of the 343 dumps are byte-identical between the two drivers. The rest differ within the envelope.

**The synthetic set.** -47 asked for draws only of what the game can draw: specular, spot lights and
DepthBound = 0.
- They come from the device's own GPU test. `ffref_gpu_selfcheck` was run with `ZH_GPU_CAPTURE`, and the
  captures showing those features were kept.
- That test gained one scenario for this (feature/mac-port-l2-synth). It draws with no depth-stencil
  surface bound while the device's ZENABLE is on with GREATER. GREATER passes nothing against the cleared
  1.0, so any depth test would draw nothing.
- The scenario passes on ANV: 4,096 exact.
- Its mutation fails on 2,192 pixels. The mutation tells the reference that depth stays on.
- set3-synth, at thinkerer:`~/zhr-worker/l2-a9/set3-synth`, holds 7 captures, labelled synthetic:
  - one DepthBound = 0;
  - five with specular: one unlit, and four lit, one of them with a spot light;
  - one with a spot light alone.
- One capture is the scenario of known finding F7 (N7, specular add with no vertex specular). Replaying it
  outside the live test shows F7 as a failure; the live test counts it as known.

### The verdicts on set2 and set3 (-47, ffref_judge, blind)

GPU pictures came from -a9's harness (build-a9 at 06c60545 for set2, cc51c0c0 for set3), run by -47 on
frozen copies, on both ANV (Intel UHD 620) and lavapipe. Every file matched the description.

| set | captures | ANV | lavapipe |
|:--|:--|:--|:--|
| set2 mobstress | 35 | 29 agree, 6 empty, 0 disagree | 29 agree, 6 empty, 0 disagree |
| set2 shellmap | 215 | 182 agree, 33 empty, 0 disagree | 181 agree, 33 empty, **1 disagree (LVP1)** |
| set2 skirmish | 93 | 63 agree, 30 empty, 0 disagree | 63 agree, 30 empty, 0 disagree |
| set3 (synthetic) | 7 | 6 agree, **1 disagree (F7)** | 6 agree, **1 disagree (F7)** |

- **Substitutions and refusals:** ANISOTROPIC was replayed as LINEAR on 32, 207 and 89 draws. Three water
  stubs were judged from their carried text. No refusals.
- **set3** agrees exactly on both drivers, pixel for pixel:
  - DepthBound = 0 (draw_00002), so the starting-state rule for a draw with no depth-stencil matches the
    device;
  - specular unlit (draw_00031), lit with a directional light (00036, 00038 with local viewer) and lit with
    a spot light (00037);
  - a spot light alone (00035).
- **F7, known:** draw_00032, specular with no specular in the FVF, disagrees on all 3136 pixels, with the
  same count on both drivers. The device adds 0 for the absent specular. FFReference's N7 follows the
  D3DTA page's 0xFFFFFFFF. That's the known finding F7 (Metal, ANV and lavapipe agree on it), a question
  between the device and the pages, not a driver effect.
- **LVP1, a lavapipe finding, new:**
  - The capture: shellmap draw_00002, a full-screen quad textured 1:1 from a 1024x1024 X8R8G8B8 texture
    (MODULATE, LINEAR, no mips).
  - On lavapipe, 2 of 480000 pixels are outside, at (363, 326) and (359, 329), 5 and 10/255 past the
    envelope. On ANV all 480000 are exact.
  - The reference samples both exactly at texel centres on a sharp bright-to-dark texture edge.
    Bilinear-sampling the texture by hand, lavapipe's colours are reproduced (to 0.3-0.6/255) by a sample
    shifted by about (-0.05 to -0.06, -0.078) texels: 1/16 to 1/13 of a texel. D3D9's documented bilinear
    precision (FFReference's freedom) is 1/128 of a texel.
  - Every other high-contrast texel of that draw is exact, so this is a local texture-coordinate precision
    quirk of lavapipe (llvmpipe), not the device's or the reference's. The envelope is not widened for it.
  - **The driver-versus-device answer for these sets:** the one driver difference is lavapipe's. ANV,
    running the same device code, agrees everywhere.

**Controls on set2 (ANV):**
| scene | shifted GPU pictures | pixel centres | screen-linear | texel corners | blend swap | no attenuation |
|:--|:--|:--|:--|:--|:--|:--|
| mobstress | 32 disagree | 23 | 3 | 21 | 12 | 0 |
| shellmap | 173 | 181 | 15 | 150 | 45 | **67** |
| skirmish | 59 | 66 | 2 | 39 | 16 | **4** |
- The point lights from explosions make the attenuation mutation count for the first time.
- No specular add, reversed fog, swapped LERP arguments and the top-left rule catch 0 on every scene. That
  matches -a9's census: fog, LERP and scissor are unreachable by source; specular, spot lights and
  DepthBound = 0 are only absent from these scenes, and set3 covers those.

**What this cannot see:** fog, LERP and scissor, which this game never draws (per -a9's reading of the
source), so they are not judged; hardware other than Intel UHD 620 (ANV) and lavapipe; and a draw's
dependence on the previous draw's depth, since every capture starts cleared.

## F7 settled: Windows' own Direct3D 9 decides (2026-09-27)

F7 asked what an absent vertex specular is: the device used 0, while FFReference's N7 followed the D3DTA
page's 0xFFFFFFFF. It was settled by measuring Microsoft's own runtime, not by reading.
`Tests/ffreference/f7probe_windows.cpp` draws 8x8 pretransformed quads into an A8R8G8B8 target and reads
the pixel back. It was run in the project's Windows 11 VM (desktop session), where the adapter is the
Microsoft Basic Render Driver (WARP, d3d10warp.dll 10.0.26100.5074), as a HAL device and as REF. Both give
the same numbers:

| case | vertex format | read | result |
|:--|:--|:--|:--|
| A | XYZRHW, DIFFUSE red | COLORARG1 = D3DTA_SPECULAR | 0xFF000000: specular RGB **0** |
| D | XYZRHW, DIFFUSE alpha 0x40 | ALPHAARG1 = D3DTA_SPECULAR | alpha **0** |
| B | XYZRHW, DIFFUSE red | SPECULARENABLE, lighting off | 0xFFFF0000: **nothing added** |
| C | XYZRHW only | COLORARG1 = D3DTA_DIFFUSE | 0xFFFFFFFF: diffuse **white**, as the page says |

The controls with the colour present in the vertex (specular black, green, alpha 0x80) read back as given.

- **The ruling:** an absent specular is 0x00000000, colour and alpha; an absent diffuse is 0xFFFFFFFF. The
  device was right. FFReference's N7 changes for specular. Its unit test checks all four cases, and fails
  three checks with the old reading.
- **After the change:** set3's draw_00032 agrees exactly on ANV and lavapipe (7 of 7). Every other verdict
  (run4, set2) is unchanged. F7 comes off the known list; the device's selfcheck marks it known, which
  -a9 will turn back into an expected pass.
- **What this cannot see:** a GPU driver's own D3D9 (the VM has none, only WARP). But WARP is Microsoft's
  conformance rasteriser, and its HAL and REF agree. REF is a separate implementation: the known-list probe
  below, on the same VM, shows the REF device loading `d3dref9.dll`, Microsoft's reference rasteriser. The
  HAL device loads only `d3d10warp.dll`.

## The known list measured: Windows' own Direct3D 9 on every open item (2026-09-27)

By -47, at the PM's request: each item on the device's known list (A-posix-d3d9-device.md, "Findings
waiting on a decision") was measured the way F7 was, not read. Each FFReference reading behind those items
was measured too. `Tests/ffreference/knownprobe_windows.cpp` draws into a 64x64 A8R8G8B8 target and reads
pixels back. It ran in the Windows 11 VM's desktop session on two implementations:
- **HAL:** the Microsoft Basic Render Driver (WARP, `d3d10warp.dll` 10.0.26100.5074);
- **REF:** `d3dref9.dll`, Microsoft's reference rasteriser. The probe prints the loaded modules, so this is
  known, not assumed.

It ran twice; the second run added cases and repeated every value of the first. The raw output is
`docs/mac-port/tasks/knownprobe-2026-09-27.txt`. `test_ffreference`'s known-list tests rebuild each scene and
pin FFReference to the measured bytes. Armed controls fail them: the old N27/N3 code, a fan's provoking vertex
moved, DOTPRODUCT3's alpha replicate removed, and (u, v, 0, 1) padding.

| item | the question | Direct3D 9, measured (WARP and REF) | FFReference | the device (-a9's list) |
|:--|:--|:--|:--|:--|
| F1 | per-light ambient | **included**: Atten × Spot × La, attenuated (Attenuation0 2 halves it), none beyond Range | already so (N4) | lacks it: **a real deviation** |
| F2 | LOCALVIEWER | **honoured**: centre 189, corners 159 and 218 with it; 242 everywhere without | honours it; its **infinite viewer was wrong**, now fixed (N27) | ignores it: **a real deviation** on every highlight, since the engine leaves it TRUE |
| F3 | DOTPRODUCT3 | **signed** (2x − 1) and **replicated into alpha**: C0 80 80 · C0 80 80 = 65 in all four channels | already so (N17) | no alpha replicate: **a real deviation** |
| F4 | TTFF_COUNT2 padding | **(u, v, 1, 0)**: `_31/_32` move the set, `_41/_42` do not | already so (N13) | pads (u, v, 0, 1): **a real deviation** (scrolling textures) |
| F6 | unlit vertex specular | **added** under SPECULARENABLE: red + green = 0xFFFFFF00 | already so | adds nothing: **a real deviation** (no engine FVF carries a specular) |
| F7 | absent specular | settled above: 0 | changed (N7) | right; off the list |
| F8 | flat shading | **the first vertex**: list 3i, strip i, fan i + 1, indexed by the first index; alpha flat too; fog still interpolated. Specular: **flat on REF, interpolated on WARP** | already so, following REF (N11) | Gouraud always: **a real deviation**, drawn only by the stencil-only volumetric shadows |
| F11 | the LOD's norm | **L2 of the longer axis**: WARP and REF read λ within −0.05..+0.03 of the exact value on isotropic, anisotropic and rotated footprints; L∞ or L1 would be 0.5 off | the nominal is exactly this (N15) | Apple +0.10 mean, +0.58 worst: inside the ±0.6 ruling, **outside Microsoft's own two** |

**FFReference readings the measurement overturned.** The pages said otherwise on each; the measurement
decides.
- **N27, the halfway vector without LOCALVIEWER:** the viewer is at (0,0,−1), the direction a camera
  looking down +z sees from, not the page's (0,0,1). Ldir (0, −.6, −.8) and N (0,0,−1) give 242 (N·H .949),
  where (0,0,1) gives 0.
- **N3, the specular gate:** specular is gated by N·L. With N·L −.436 and N·H .531, both give 0, where the
  page's formula gives 135. The controls with N·L > 0 (.436, .141) give 216 and 193, as N·H predicts.
- **N14, the reflection vector without LOCALVIEWER:** E = (0,0,−1), as for N27. The measurements: R =
  (0, 0, −.99) for N (0,0,−1), and (0, .96, −.28) for N (0, .6, −.8). With LOCALVIEWER, E = norm(−V) is as
  written and as measured.

**What the device does with LOCALVIEWER off** (-a9's written description of the generator, not read by
-47):
- The halfway vector is normalize(L + (0, 0, +1)): the page's viewer, where Direct3D 9 measures (0,0,−1).
- The reflection vector has no LOCALVIEWER branch: it always uses the local E, where Direct3D 9 measures a
  fixed (0,0,−1) without it.
- The N·L gate matches N3, at a threshold of 0.0001.

Both deviations are real, but hidden: the engine never turns LOCALVIEWER off, and every game capture has it
on. -a9 lists them beside F1–F4, F6, F8 and F11.

**Readings the measurement confirmed.**
- N4, N11, N13 and N17, as in the table.
- N28: a missing coordinate set reads (0, 0). Texel (0, 0) where the vertices' (.6, .6) is texel (2, 2).
- N29 is truly undefined: WARP passes CURRENT's alpha through, REF writes 1. Both are inside FFReference's
  envelope (CURRENT, 0, 1).

**Where Microsoft's two differ.** WARP and REF agree on every case except these:
- **F8's flat specular:** REF flat, WARP Gouraud. FFReference follows REF and the page. A flat draw with a
  specular would need this as a freedom; the engine has none.
- **N29:** see above.
- **F11:** one level step (1/28 of a mip level) on two footprints.

**What this cannot see:**
- A GPU driver's own D3D9: the VM has only WARP and REF, so the ±0.6 LOD ruling remains a ruling for
  hardware, not a measurement.
- Cases the probe does not draw: spot lights' ambient, LOCALVIEWER with a non-identity view, and the
  specular gate exactly at N·L = 0.

## Metal beside Vulkan: one table (2026-09-27)

By -47, at the PM's request: the same frozen sets, replayed on Metal and judged the same way, so the Mac and
Linux verdicts sit side by side.
- **The replay:** finer (M3 Pro, macOS 26.5) ran -a9's `ffref_capture_selfcheck` as a black box, with SDL's
  dummy video driver and `ZH_SDL_GPU_METAL_WINDOWLESS=1`, `FFREF_GPU_DUMP` on and its own output discarded.
  The build was feature/mac-port-f7 8cf87ea1.
- **The judge:** `ffref_judge` with FFReference as of feature/mac-port-known, which carries the N27/N3/N14
  corrections above.
- **Re-judging:** the Linux dumps were re-judged with that FFReference too. Every game-set verdict is
  unchanged by the corrections.

Each cell reads agree / disagree / empty.

| set | captures | ANV (Intel UHD 620) | lavapipe (llvmpipe) | Metal (Apple M3 Pro) |
|:--|--:|:--|:--|:--|
| run4 | 49 | 38 / 0 / 11 | not replayed | 35 / **3** / 11 |
| set2 mobstress | 35 | 29 / 0 / 6 | 29 / 0 / 6 | 25 / **4** / 6 |
| set2 shellmap | 215 | 182 / 0 / 33 | 181 / 1 / 33 (LVP1) | 164 / **18** / 33 |
| set2 skirmish | 93 | 63 / 0 / 30 | 63 / 0 / 30 | 61 / **2** / 30 |
| set3 (synthetic) | 7 | 5 / 2 / 0 | 5 / 2 / 0 | 5 / 2 / 0 |

**F7 and F8 on Metal:**
- F7: set3's draw_00032, the absent specular, agrees on all three.
- F8: flat shading is the same on all three in -a9's ffref_gpu_selfcheck (3135 outside, worst 248/255). It is
  a device deviation, now measured (above).

**set3's two disagreements are the same on all three drivers.** They are draw_00036 (2088 outside) and
draw_00037 (978 outside): the specular draws with LOCALVIEWER off. They agreed while FFReference used the
page's (0,0,1) viewer, as the device did. Under the measured (0,0,−1) they disagree, with the numbers
-a9's armed control of the old generator gives. -a9's feature/mac-port-ffviewer 52b842ba changes the
device to the measured viewer, and those scenarios are then exact (4096/4096) on ANV and lavapipe.

**Metal's 27 disagreements on game captures are Metal's alone.** ANV draws all 27 inside the envelope.
- **Size:** 243 pixels outside in all, in frames of 262,144 or 480,000 pixels. Per draw it is 1 to 106
  pixels, and the worst is 51/255 (mobstress draw_00031).
- **Where:** every outside pixel is on a minified stage (λ 2 to 8.7), most of them on thin or tiny triangles.
- **The draws:** run4 14, 28, 29; mobstress 9, 17, 31, 32; shellmap 21, 25, 26, 42, 54, 58, 59, 72, 73, 81,
  82, 113, 129, 145, 167, 178, 195, 198; skirmish 14, 68.

`ffref_judge --lod-delta` (a diagnostic, never a verdict) widens only the LOD freedom:
- **±1.0:** 6 of the 27 agree.
- **±1.5:** 14 agree.
- **Left at ±1.5:** 13 disagree: run4 29; mobstress 17, 31, 32; shellmap 25, 26, 59, 72, 73, 167, 178, 195,
  198. That is 1 to 13 pixels each, worst 17/255. At those pixels the GPU sampled a finer level than
  FFReference at some and a coarser one at others.

So Apple's λ departs from the D3D9 nominal by more than the ±0.6 ruling on real game geometry. WARP and REF
hold to within 0.05 (F11 above). The rest is not LOD alone.

**The open question** (asked of -a9, answer pending): what the SDL GPU sampler gets on Metal when the replay
turns ANISOTROPIC into LINEAR. If anisotropy stayed on for Metal only, it would explain "a finer level on
slivers".

**What this cannot see:**
- **Other hardware:** any Apple GPU but the M3 Pro, and any Vulkan hardware but Intel's.
- **run4 on lavapipe:** it was never replayed there.
- **Draw order:** whether the 27 draws matter in a frame. Each capture starts cleared, so a draw's pixels
  are judged alone, never under the draws that would cover them.

