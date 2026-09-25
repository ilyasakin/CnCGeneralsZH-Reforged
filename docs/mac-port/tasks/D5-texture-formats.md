# D5 — Texture formats

- **Milestone:** M4
- **Depends on:** D4
- **Blocks:** nothing
- **Status:** not started
- **Size:** 44 files in WW3D2 reference DXT or DDS

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** The question is now whether SDL3's GPU API takes BC formats on the devices we care about: Apple Silicon under Metal, and Linux Vulkan drivers. It is no longer about Metal alone.

## The BC question, answered on one machine: 2026-09-25

`Tests/sdl_gpu_probe.cpp`, SDL 3.4.16 (`fa2c02bb`) static, through `SDL_GPUTextureSupportsFormat` with
`SAMPLER` usage.

| Machine | Backend | BC1 (DXT1) | BC2 (DXT2/3) | BC3 (DXT4/5) |
|:--|:--|:--|:--|:--|
| Apple M3 Pro, macOS 27.0 (26A428) | metal | yes | yes | yes |
| Linux, llvmpipe (Mesa 25.2.8), CPU only | vulkan | yes | yes | yes |

**On Apple Silicon under Metal, the art's DDS formats sample directly.** Step 3's decompress-on-load
is not needed on this machine. Every other format `dx11resource.cpp` maps also samples:
B8G8R8A8, B5G6R5, B5G5R5A1, B4G4R4A4, A8 and R8.

What this does **not** establish:
- **Other Apple GPUs.** M1 and M2 are untested.
- **Any Linux GPU.** The llvmpipe row is Mesa's CPU rasterizer. It shows the probe's Vulkan path
  runs, and says nothing about radv, anv or NVIDIA. It needs rerunning on real Linux hardware.
- **Rendering correctly.** "Supported" is the device's claim, not a picture. Step 4 and the Done-when
  still stand.


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
