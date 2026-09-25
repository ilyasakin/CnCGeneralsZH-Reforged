# D2 — Abstract the backend interface

- **Milestone:** M3
- **Depends on:** D1
- **Blocks:** D3
- **Status:** not started
- **Size:** 22 WW3D2 headers expose D3D9 types; `dx11backend.h/.cpp` is 2,970 lines and is the
  model

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** Step 1 is decided: the non-Windows backend is **SDL3's GPU API** (Metal underneath on macOS, Vulkan on Linux). The fallback, if you find something the game needs that it cannot express, is one Vulkan backend with MoltenVK on macOS. Record the gap that forced it here. The interface stays abstract either way.

## What SDL3's GPU API can do, probed: 2026-09-25

`Tests/sdl_gpu_probe.cpp` (built on POSIX, not in ctest; it needs a GPU and a display). SDL 3.4.16,
static, video and GPU only. It asks about what the game asks of Direct3D today, as the D3D11
backend answers it. Measured on an **Apple M3 Pro, macOS 27.0 (26A428), `metal` backend**:

- **Nothing probed forces the fallback.** Every sampled and colour-target format the game maps is
  supported, BC1-3 included; see D5.
- **D24S8 is not supported; D32S8 is.** The game creates D24S8 (`dx11device.cpp`), and the shadow
  volumes need the stencil, so the backend maps D24S8 to **D32_FLOAT_S8_UINT**. This is Apple
  GPU hardware, not SDL, so Vulkan over MoltenVK would meet the same limit, and it is not a reason
  to take the fallback. D16 and D32F are supported; bare D24 is not.
- **MSAA:** 2x and 4x, not 8x. **Present modes:** vsync and immediate, not mailbox. **Swapchain:**
  B8G8R8A8_UNORM, SDR and SDR-linear.
- **Shader formats:** MSL and metallib on Metal; SPIR-V on Vulkan. That is D3's question.
- **Clip distance, depth clamping, anisotropy:** SDL_gpu.h documents all three as *required* by a
  default device, with properties that only relax them, so a device that creates has them. The game
  does use anisotropic filtering (`dx11state.cpp:310`). It does not use user clip planes: every
  `D3DRS_CLIPPLANEENABLE` reference is a name table or a commented-out reset.
- **Coordinates:** SDL_gpu.h documents a left-handed system "following the convention of D3D12 and
  Metal".

**Not queryable, so not measured:**
- Point size must be written by a POINTLIST vertex shader, and the game draws no points (every
  `D3DRS_POINT*` reference is commented out).
- The game uses 4 texture stages (`DX11_BACKEND_TEXTURE_STAGES`). SDL_gpu.h documents no per-stage
  sampler limit.
- Fog, alpha test and the stage combiners are shader code, as `dx11backend` generates them today.

**A caveat on the probe itself:** `SDL_GPUTextureSupportsSampleCount` said yes to 2x and 4x for
D24S8, a format the same device had just refused. Its answer does not imply the format is
supported; check the format first.

**Linux:** built with GCC 13.3 and Clang 18.1 (Ubuntu 24.04, arm64, zero warnings), with X11,
Wayland and Vulkan video paths in. It has **not** been run on a Linux GPU. The container has no
display or GPU, where the probe reports "No available video device". Run on Mesa's llvmpipe with
SDL's offscreen driver, it completes on the Vulkan backend. That shows the code path works. It does
not show what any real Linux GPU supports (llvmpipe claims D24S8 and 4x-only MSAA, for instance).


Once D1 has everything going through `DX8Wrapper`, the wrapper still talks to a concrete
`IDirect3DDevice9`. This task turns that into an interface with two implementations, so a third can
be added without touching the engine.

The design already exists and is worth reading before anything else. `dx11backend.h`'s header
comment describes it exactly:

> The engine sets one thing at a time and then draws: a render state, a texture stage state, a
> texture, a transform, a stream, and eventually a DrawIndexedPrimitive. There are 236 places that
> do it and 5600 calls between them, and rewriting those into something D3D11 shaped is not a
> phase, it is a different program. So this takes the calls as they are and resolves them at the
> moment of the draw.

Resolve-at-draw is the right shape for Metal too. D2 generalises `DX11BackendClass`; it does not
invent anything.

## What the D-spike learned about SDL3 GPU against the game's needs: 2026-09-25

The D-spike (`Tests/w3d_view`, [its task file](D-spike-sdl3-gpu-model.md)) drew a real model with
real textures through SDL 3.4.16's GPU API: on Metal (Apple M3 Pro, macOS 27.0) and on Vulkan
(lavapipe, Linux arm64). The findings below are what an interface designed for SDL3 GPU has to
account for. Each says whether it was **run** or only **read**.

**It fits as the game expects:**

- **Clip space and winding are the same on both backends.** SDL presents Direct3D's clip space
  (Y up, depth 0 to 1) on Metal and on Vulkan, flipping Vulkan's viewport itself. A right-handed
  projection with front faces counter-clockwise and back faces culled reproduced the game's
  `D3DCULL_CW` on both. The two frames overlay; nothing in the backend is per-API. *Run.*
- **BC1, BC2 and BC3 go straight from the DDS, with every mip level.** Full chains down to 1×1 were
  uploaded unchanged on both backends, with no block-alignment fix-up for the small levels. *Run.*
- **Depth needs exactly one mapping function.**
  - Apple has no D24S8. `mapDepthStencilFormat` in `w3d_view.cpp` asks for D24S8 and falls back to
    D32S8, keeping the stencil the shadow volumes need.
  - lavapipe took D24S8 and the Mac took D32S8, so both branches were exercised.
  - This function can move into the backend as it is. *Run.*
- **The alpha test and fog are shader code, as in D3D11.** SDL has no alpha-test state, and the
  spike's fragment shader discards against the game's reference (0x60/255). `dx11backend` already
  works this way, so the D3D11 design carries over. *Run for the alpha test; fog only read.*
- **Readback works.** `SDL_DownloadFromGPUTexture` from a colour target made every screenshot here,
  on both backends. That is the game's screenshot and any readback path. *Run.*
- **Dynamic state is where the game needs it.** Viewport, scissor, blend constants and the stencil
  reference are command-buffer state in SDL, not pipeline state. The shadow volumes' stencil
  reference is therefore not a pipeline key. *Read (SDL_gpu.h).*

**It needs design:**

- **Everything else is baked into a pipeline.** Blend, depth test and write, cull, fill, the vertex
  layout, the target formats and the shaders are one immutable `SDL_GPUGraphicsPipeline`. The
  game's render states therefore reach SDL through a pipeline cache keyed by the state D1 funnels,
  the way `dx11backend` caches D3D11 state objects. The spike's 13 draws needed 2 pipelines. *Run.*
- **Samplers are immutable objects.** D3D's per-stage sampler states (address, filter, mip filter,
  LOD bias, anisotropy) become a sampler cache keyed by those values, bound with the texture as a
  pair. SDL binds textures only in texture-sampler pairs, one per stage, and the game uses 4. *Run
  with one stage.*
- **There is no BGRA vertex format.** `dx11layout.cpp:26` declares vertex colours
  `DXGI_FORMAT_B8G8R8A8_UNORM`, which is D3DCOLOR's memory order. SDL has only
  `UBYTE4_NORM` (RGBA). The backend therefore either swizzles `.bgra` in the generated vertex
  shader, a generator change (D3), or converts every vertex buffer on upload. The first is cheaper
  and keeps the buffers byte-identical. *Read; the Crusader has no vertex colours.*
- **Uniforms are pushed, not bound.** `SDL_PushGPUVertexUniformData`/`...FragmentUniformData` gives
  up to 4 slots per stage, copied per draw. The generated shaders use one constant buffer per stage
  (`CombinerConstants`, `VertexPipeline`), so they fit. They do have to sit in SDL's register spaces:
  vertex uniforms in space1, fragment uniforms in space3. *Run.*
- **Shaders compile at runtime and cost time the first time.** Handing MSL to the Metal driver took
  50 to 225 ms per program on average on a first run. The operating system then caches the result: 0.2 ms on
  the second run. The backend should create its pipelines ahead of need, for example on the loading
  screen, rather than on the frame that first draws something. Numbers in D3's task file. *Run.*
- **House colour needs CPU access to texture pixels.** `W3DAssetManager::Recolor_Texture` recolours
  team-colour textures on the CPU, through `SurfaceClass`, with a 16-step palette scale. The spike
  tinted in a shader instead, which is not what the game computes. The backend, or D5, has to give
  that code pixels to work on, whether decoded BC or the original data. *Read.*

**Not tested by the spike:**

- MSAA, render-to-texture chains and post-processing (`dx11post.cpp`).
- 16-bit textures (B5G6R5 and friends; the probe says they sample) and multi-pass materials.
- Any real Vulkan GPU, and MoltenVK.

## Scope

- `Libraries/Source/WWVegas/WW3D2/dx8wrapper.h/.cpp`
- `dx11backend.h/.cpp`, `dx11device`, `dx11state`, `dx11layout`, `dx11resource`, `dx11texture`,
  `dx11twin`, `dx11post`
- The 22 WW3D2 headers that leak D3D9 types

## Recon findings, 2026-09-22

From `docs/mac-port/D1-call-site-survey.md`:

**Your header target is 14, not 22.** The 22 are not 22 of the same thing. 14 are real leaks; the
other 8 are the D3DX shim (3) and the D3D11 backend's own headers (5), which D2 changes rather than
hides. Two headers dominate: `dx8wrapper.h` (29 includes from outside WW3D2) and `texture.h` (21)
are 50 of the 82 include sites between them, and expose every D3D9 type the engine can name.

**Numbers revised again by D1 PR1, which found the wrapper richer than the survey credited.** Of
the 75 sites said to need new API, 7 do not: `SetFVF` (6 sites) is `Set_Vertex_Format`, which
already mirrors, and `EvictManagedResources` is `Flush_DX8_Resource_Manager`. A render-target
save/restore pair was written and then deleted, because `Set_Render_Target(surface, depth)` already
keeps what it displaced and `Set_Render_Target(NULL, NULL)` puts it back — a second stack of one
would have fought it. Final split: **59 sites covered by PR1's new API, 7 reclassified as swaps, 9
out of scope** (7 cursor calls to C3, 2 possibly-dead `ProcessVertices`).

**A surface this file did not account for:** `Peek_D3D_Texture()` and friends hand raw
`IDirect3D*9` resources straight out of the texture classes — **134 uses, 64 of them outside
WW3D2**. Not D1's problem (they are not device calls), but they are D2's, and they are most of why
`texture.h` leaks.

## Do

1. **Decide Metal versus MoltenVK, and write the reason down here.** This is the decision point the
   plan flags as open. Native Metal is less indirection and better tooling; MoltenVK means one
   Vulkan backend serves macOS and a future Linux port, at the cost of a vendored dependency and a
   second translation layer under a first one. Whatever you choose, the paragraph you write here is
   what stops the question being reopened every month.
2. Extract an abstract backend from `DX11BackendClass`'s shape — the setters, the resolve, the
   draw. `DX11BackendClass` becomes one implementation, the D3D9 path becomes another.
3. Get the D3D9 types out of the 22 public headers. This is the mechanical bulk of the task:
   `D3DFORMAT`, `D3DPOOL`, `D3DMATRIX` and friends become engine-side enums and types that each
   backend maps. `DX11_BACKEND_TEXTURE_STAGES` and `DX11_BACKEND_STAGE_STATES` in `dx11backend.h`
   show the style.
4. Keep the caching. The reason the D3D11 backend is viable is that it caches state objects,
   samplers, shaders and layouts on the description they came from — "-ffprobe counted 42 distinct
   stage programs across four maps". Any backend needs that and the interface should make it
   natural rather than optional.

## Done when

- `d3d9.h` is included by the D3D9 backend and nothing else.
- Both existing backends run through the interface. `-d3d9` and the default D3D11 path both work,
  and the pixel comparison against the pre-D2 build is clean on both.
- The `dx11*` test suites still pass, adapted to the interface rather than deleted.
- The decision on Metal versus MoltenVK is recorded in this file.

## Do not

- Do not write any Metal in this task. D2 proves the interface by supporting the two backends that
  already exist. A third one written against an unproven interface is how you get an interface
  shaped like one backend.
