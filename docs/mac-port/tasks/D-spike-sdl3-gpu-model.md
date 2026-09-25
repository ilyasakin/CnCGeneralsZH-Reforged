# D-spike — One real model through SDL3's GPU API

- **Milestone:** M4
- **Depends on:** nothing. It deliberately does not wait for D1.
- **Informs:** D2 (what SDL3 GPU can and cannot do against the game's needs), D3 (the shader
  toolchain decision), D4 and D5
- **Status:** in progress, -a9
- **Size:** one standalone tool, `Tests/w3d_view`. It is POSIX-only and not in ctest.

## Why

D1 is stuck waiting for a Windows machine, and D2 through D5 are designed on paper against an API
nobody here has drawn with. This spike draws one real game model, with its real textures, through
SDL3's GPU API on this Mac. The first contact between the game's data and the chosen API then
happens before anyone designs an interface around it.

## Goal

`w3d_view` opens an SDL3 window and draws one real model, read only from the Steam data (for
example the Crusader tank from `W3DZH.big`), with its real DDS textures and an orbit camera.

- **W3D:** parsed with WWLib's `chunkio` and the structs in `w3d_file.h`. WW3D2 itself is not built.
- **Textures:** BC1-3 uploaded straight from the DDS, with its mip levels.
- **Shaders:** a vertex and a fragment shader, each written once, reaching both MSL (Metal) and
  SPIR-V (Vulkan) by one approach. SDL_shadercross is measured against hand-written MSL plus
  glslang's SPIR-V, including what each costs as a dependency. The decision goes in D3's task file.
- **Depth:** D24S8 is requested, and one mapping function falls back to D32S8, as D2 will.
- **Linux:** built in a container with gcc and clang. There is no window there, and this file
  says so.

## Done when

- A screenshot of the textured model, rendered on this Mac, is saved in `docs/mac-port/`.
- D3's shader decision is recorded with measurements.
- D2's task file has a section on what the spike learned about SDL3 GPU against the game's needs.
- Nothing in the engine changes.
