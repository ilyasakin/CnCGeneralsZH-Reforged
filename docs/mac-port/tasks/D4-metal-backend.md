# D4 — Metal backend

- **Milestone:** M4
- **Depends on:** D3
- **Blocks:** D5 C3
- **Status:** not started
- **Size:** the largest single piece of new code in the plan; `dx11backend.cpp` is 2,339 lines and
  is the closest model

## Why

The task everything else has been clearing the way for. With D1 funnelling, D2 abstracting and D3
emitting MSL, this implements the backend interface against Metal.

Read `dx11backend.h`'s header comment first. Its five-part resolve — render states to state
objects, sampler states to sampler objects, texture stage states to a generated pixel shader,
lighting and transform state to a generated vertex shader, FVF to an input layout — maps onto Metal
almost term for term.

## Scope

New, alongside `dx11*`: the Metal device, backend, state, layout, resource, texture and post
files. Mirror the existing naming and the existing file split; it is a good split and the tests are
organised around it.

## Do

1. Mirror the `dx11*` decomposition rather than writing one large file. `test_dx11device`,
   `test_dx11state`, `test_dx11layout`, `test_dx11resource`, `test_dx11twin`, `test_dx11texture`,
   `test_dx11pipeline`, `test_dx11post` and `test_dx11backend` exist because of that split, and the
   Metal versions should exist for the same reason.
2. Objective-C++ (`.mm`) at the Metal boundary only. Everything above it stays C++.
3. `MTLRenderPipelineState` is the expensive object and caching it is the whole performance story,
   exactly as it is for D3D11. The description-keyed caches D2 preserved are where this lands.
4. `dx11post.cpp` (989 lines) is the glow and edge smoothing over the battlefield, and `-dx11post
   off` already exists to turn it off. Get the game drawing before you port the post chain, and use
   that switch as the milestone inside this task.
5. The window and swap chain: `CAMetalLayer` in an `NSWindow`. This is where C2's headless entry
   point grows a real one, and where C3 can start.
6. Wide screens get a three-piece command bar and ultrawide opens the view sideways. Test at those
   aspect ratios, not just 16:9.

## Done when

The game draws a playable match on macOS. The pixel comparison against the Windows D3D11 build is
the measure — the same frame of the same match, pixels counted — and where it differs, the
difference is explained rather than accepted.

Frame cost recorded on the same scenes `README.md` benchmarks, so there is a number to argue with
later.

## Do not

- Do not improve the renderer. Same picture, different API. Anything else is a separate change with
  its own pixel argument.

## Notes

`README.md` records that tree shadows cast from stencil volumes were built, measured and reverted.
Read `CHANGELOG.md` on the renderer before assuming an effect works the way it looks like it does —
several of them are the second or third attempt.
