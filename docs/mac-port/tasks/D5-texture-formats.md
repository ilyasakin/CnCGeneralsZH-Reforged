# D5 — Texture formats

- **Milestone:** M4
- **Depends on:** D4
- **Blocks:** nothing
- **Status:** not started
- **Size:** 44 files in WW3D2 reference DXT or DDS

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** The question is now whether SDL3's GPU API takes BC formats on the devices we care about: Apple Silicon under Metal, and Linux Vulkan drivers. It is no longer about Metal alone.

## Why

The art is block-compressed. `README.md` advertises "481 originals at four times the resolution",
and all of it is DXT in DDS containers. Whether Metal takes it directly is the open question this
task has to answer, and the answer changes how much memory the game needs on a Mac.

## Scope

- The 44 WW3D2 files referencing DXT/DDS — the loader, the texture manager, `dx8texman.cpp`
- The art fetched by `vendor.ps1` from the `art-latest` release
- D4's Metal texture implementation

## Do

1. **Answer the open question first, before writing anything.** `MTLDevice` has a
   `supportsBCTextureCompression` property. Query it on the target hardware and macOS versions
   rather than trusting a summary — including this plan's. Write the answer in this file with the
   machines and OS versions tested.
2. If BC is supported: DDS maps onto `MTLPixelFormatBC1_RGBA` and friends, the loader changes
   little, and this task is small.
3. If it is not, or is patchy: decompress on load, and measure what that costs in memory and in
   load time. Upscaled art at four times the resolution decompressed is a lot of memory, and that
   measurement decides whether a transcode to ASTC at build time is needed instead.
4. DDS is a container with its own header and mip chain layout; it is not tied to D3D and the
   parsing is portable. Check the loader for D3D enum values baked into the format handling — those
   are the sites that need mapping.

## Done when

Textures render correctly on macOS: colour, alpha, mip chains, the cursor and UI atlases. Memory
use is measured and recorded, and compared against the Windows build.

The BC support question is answered in writing in this file, with the hardware tested.

## Do not

- Do not re-export the art. It is fetched from a release and checksummed against `art.json`; a
  parallel Mac art set is a maintenance burden and a desync risk for anything that hashes assets.
