# D2 — Abstract the backend interface

- **Milestone:** M3
- **Depends on:** D1
- **Blocks:** D3
- **Status:** not started
- **Size:** 22 WW3D2 headers expose D3D9 types; `dx11backend.h/.cpp` is 2,970 lines and is the
  model

## Why

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
