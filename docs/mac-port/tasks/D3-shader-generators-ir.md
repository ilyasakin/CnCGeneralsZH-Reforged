# D3 — Shader generators target SDL3 GPU (was: emit an IR)

- **Milestone:** M3
- **Depends on:** nothing since decision 4 (it depended on D2 when the plan was an IR)
- **Blocks:** D4
- **Status:** in progress, -a9
- **Size:** `ffshader.cpp` 543 lines, `ffvertex.cpp` 689, `engineshader.h` 104,
  `d3d8shadertranslate.cpp`

## Why

The game has no shader files — `find . -name '*.hlsl'` returns nothing. `ffshader` and `ffvertex`
*generate* HLSL source at runtime from the engine's fixed-function state, and `dx11backend.cpp`
compiles it by binding `d3dcompiler_47.dll` at runtime:

```cpp
// d3dcompiler_47.dll ships with Windows.  Binding it by hand rather than linking it keeps a machine
static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
```

On macOS there is no `d3dcompiler_47.dll` and no HLSL. The generators have to be able to produce
something Metal can consume.

## The shader toolchain decision, measured by the D-spike: 2026-09-25

> **Recommendation:** keep HLSL as the one shader language. The generators keep writing it, with a
> target that adds SDL's register spaces. At run time glslang's HLSL front end compiles it to SPIR-V
> (Vulkan), and SDL_shadercross, built **without** DXC, translates that SPIR-V to MSL (Metal).
> Cache by the generators' own keys.
>
> - **Cost:** about 5.1 MB of runtime code on arm64 macOS, stripped: glslang 2.25 MB and
>   shadercross with SPIRV-Cross 2.86 MB.
> - **Coverage:** every one of the 49 programs the game's generators and `engineshader.cpp` write
>   today reached Metal this way.
> - **What it replaces:** the IR and second emitter in this file's plan. Step 5's SPIRV-Cross
>   alternative is taken, but fed by glslang, not DXC.
> - **Rejected:** DXC (measured below: 21 MB, a long build, and 3 of the 49 fail on Metal through it)
>   and hand-written MSL (a second source for shaders the game generates).

This section answers step 5 by doing it. The rest of this file was written for a Metal-only
backend; decision 3 made it SDL3 GPU, which takes SPIR-V on Vulkan and MSL on Metal.

### What was measured

**Which programs.** A scratch program was built with mingw-w64 from the unmodified `ffshader.cpp`,
`ffvertex.cpp` and `engineshader.cpp`, and run under Wine. It wrote the D3D11 HLSL for:

- every case `test_ffshadercompile` and `test_ffvertexcompile` compile: 19 pixel and 14 vertex
  programs;
- all 16 programs `engineshader.cpp` transcribes, including the three bumped terrain variants.

That is 49 programs, the game's own shader text.

**Machine.** Apple M3 Pro, 11 cores, macOS 27.0. The Metal driver was reached through SDL 3.4.16.

**Pinned versions.**

- glslang `vulkan-sdk-1.4.357.0` (reports 16.4.0), built with `ENABLE_HLSL=ON`.
- SPIRV-Cross `vulkan-sdk-1.4.357.0`.
- SDL_shadercross `1ff05bec`, with its own pinned SPIRV-Cross and its DXC fork `1.8.2502-SDL`
  (`2c84a1c5`).

**Results for the 49 programs:**

| Route | HLSL → SPIR-V | `spirv-val --target-env vulkan1.0` | SPIR-V → MSL | Metal accepts |
|:--|:--|:--|:--|:--|
| As generated, no register spaces, DXC | 49 | — | 12 | 12 |
| With register spaces, **DXC** (shadercross) | 49; 5.0 ms mean, 18 ms worst | 49 | 49 | **46**: the 3 bumped terrain programs are refused |
| With register spaces, **glslang HLSL** front end | 49; 1.1-1.3 ms mean, 15-21 ms worst (the first call includes setup) | 49 | 49 | **49** |

**Findings behind those numbers:**

- **Register spaces are the one change the text needs.** As generated, uniform buffers land in
  descriptor set 0, and SDL rejects that ("must be 1 or 3"). Every register therefore needs the space
  SDL assigns it:
  - vertex textures in `space0`, vertex uniforms in `space1`;
  - pixel textures and samplers in `space2`, pixel uniforms in `space3`.

  It is a mechanical rewrite of the declaration lines. Nothing in a function body changes.
- **Why DXC fails the bumped terrain.**
  - Those programs declare `NormalMap : register(t4)` with no `s4`, and leave `Texture2` and
    `Texture3` unused.
  - DXC strips the unused ones. SDL_shadercross's SPIR-V → MSL step then numbers the textures
    left and puts `NormalMap` at `[[texture(0)]]`, on top of `Texture0`.
  - Metal says: `cannot reserve 'texture' resource location at index 0`.
  - glslang keeps every declared resource at its register, and its MSL has `NormalMap` at
    `[[texture(4)]]`.

  Whoever takes DXC takes that bug with it.
- **A texture without a sampler of its own index is legal on both backends.** SDL binds
  texture-sampler pairs by index, so the backend binds a sampler at every index up to the highest
  texture the program uses. On Vulkan a separate `Texture2D` and `SamplerState` at the same binding
  read one combined-image-sampler descriptor, which the specification allows. On Metal they are
  separate `[[texture(n)]]` and `[[sampler(n)]]`.
- **What "accepts" covers, and what it does not.**
  - "Metal accepts" means `SDL_CreateGPUShader` compiled the MSL; nothing was drawn with those 49
    programs.
  - Drawing was checked on `w3d_view`'s own shader, written in the generators' HLSL style
    (`Tests/w3d_view/shaders/model_ps.hlsl`). On Metal it is pixel-identical to the GLSL and
    hand-written routes. On Vulkan (lavapipe) it is pixel-identical to the GLSL route.
  - For the 49, the Vulkan side is `spirv-val` only; SDL's Vulkan backend has not been handed them.
- **The Metal driver's compile is the slow step, once.** 50 to 225 ms per program on average on a first run (the mean over each batch), then
  about 0.2 ms once the operating system has cached it. Compile ahead of need; D2 has the note.

**What each dependency costs:**

| | Source it adds (tracked files) | Clean build, this machine | Runtime code (stripped, arm64) | Licence |
|:--|:--|:--|:--|:--|
| glslang, HLSL on | 3,481 files, 42.6 MB | 23 s (64 steps) | +2.25 MB | BSD-3, BSD-2, MIT, Apache-2.0 (and GPL-3 with the Bison exception on parts, per its LICENSE.txt) |
| SPIRV-Cross, MSL + GLSL backends | 4,772 files, 11.8 MB | 13 s (11 steps) | +2.20 MB | Apache-2.0 |
| SDL_shadercross without DXC (brings its own SPIRV-Cross, SPIRV-Headers, SPIRV-Tools) | 45 files, 0.4 MB, plus 40.4 MB of submodules | 23 s (22 steps) | 2.86 MB (SPIRV-Cross inside) | zlib |
| DXC, as shadercross pins it | 18,009 files, 119.4 MB, plus 30.6 MB of its own submodules | 450 s (1,308 steps, shadercross included) | 21.2 MB (`libdxcompiler`) | NCSA (LLVM) |
| Hand-written MSL | none | none | none | — |

The build times are clean configure-and-build runs, one after another, on all 11 cores. The machine
was shared with other sessions' containers, with a load average of 17 to 34 throughout, so the
absolute times are inflated. The ratio is the point: DXC is about twenty times the others.

Every licence above is compatible on its face with this tree's GPL-3.0 and EA's additional terms.
That is a reading of the licence files, not legal advice; whoever vendors them should confirm it.

**Two traps in vendoring shadercross:**

- `SDLSHADERCROSS_VENDORED=ON` insists on DXC's source even with `SDLSHADERCROSS_DXC=OFF`.
  Its `CMakeLists.txt:120` checks for the subfolder outside the `if(SDLSHADERCROSS_DXC)`. So vendor shadercross
  with `VENDORED=OFF` and hand it our own SPIRV-Cross, or carry a one-line patch.
- The offline Metal compiler (`xcrun metal`) is a separate Xcode download on this machine and was not
  installed. The runtime route does not need it: SDL hands MSL source to the driver. Precompiling
  `.metallib` would need it on every developer's machine.

### Why this and not the alternatives

- **Hand-written MSL next to glslang's SPIR-V.** For `w3d_view` it works, and it drew identical
  pixels. But the game has no hand-written shaders to write twice. Its 49 programs are generated at
  run time from state. Hand-writing means a second emitter, which is the IR-plus-MSL-emitter plan
  this file started with. That plan is not needed: the 49 existing programs reach both backends
  as they are, apart from the register spaces.
- **SDL_shadercross with DXC (HLSL in).** DXC is the reference HLSL compiler and the obvious choice.
  Against it:
  - it adds 21 MB of runtime code, 150 MB of source and the longest build of the four;
  - it fails 3 of the game's 49 programs on Metal, for a reason in shadercross's pipeline;
  - glslang does every one of the 49 for a tenth of the size.

  It stays the fallback: the same shadercross call takes HLSL once DXC is built in.
- **Emitting GLSL instead of HLSL.** GLSL through glslang is just as cheap; the spike drew with it.
  It would, however, mean a GLSL emitter next to the HLSL one Windows needs, and the regression gate
  in "Done when" is on HLSL. One language for Windows, Metal and Vulkan keeps that gate meaning
  something.

### What this changes in this task

- **Do, step 1 (the IR):** optional. The generators need a target, D3D11 or SDL, that adds the
  register spaces and, for SDL, swizzles BGRA vertex colours (see D2). The IR is not needed for a
  second language.
- **Do, steps 3 and 4 (the MSL emitter, `newLibraryWithSource:`):** replaced. SPIRV-Cross writes the
  MSL, and SDL hands it to the driver.
- **Done when:** the byte-identical HLSL gate stands for the D3D11 target. Add a POSIX twin of
  `test_ffshadercompile` and `test_ffvertexcompile`: every generator case through glslang → SPIR-V
  (`spirv-val`) → SPIRV-Cross → MSL, in ctest on macOS and Linux. The spike's dumper and harness are
  a prototype of that test.
- **The main risk:** glslang's HLSL front end is not the reference compiler. A construct the
  generators emit later could compile differently, or not at all. The ctest twin catches "not at
  all". D4's render comparisons catch "differently". DXC stays one build flag away.

## Scope

- `Libraries/Source/WWVegas/WW3D2/ffshader.cpp/.h` — texture stage states to a pixel shader
- `Libraries/Source/WWVegas/WW3D2/ffvertex.cpp/.h` — lighting and transform state to a vertex
  shader
- `engineshader.h` — the engine's own shaders
- `d3d8shadertranslate.cpp` — lifted from d3d8to9, translates D3D8 shader bytecode
- Tests: `test_ffshader`, `test_ffvertex`, `test_ffshadercompile`, `test_ffvertexcompile`,
  `test_ffvertexd3d9`, `test_engineshader`

Note those six test suites. The generators are already tested by comparing generated source and by
compiling it, which makes this task far safer than it sounds.

## Do

1. Split generation from emission. Right now the generators build HLSL text directly. Have them
   build a small description of the shader — the stage operations, the lighting terms — and have a
   separate emitter turn that into text. The description is the IR; it does not need to be
   sophisticated, it needs to be enough for two emitters.
2. HLSL emitter first, producing byte-identical output to today. That is the safety net, and
   `test_ffshadercompile` and `test_ffvertexcompile` verify it.
3. MSL emitter second. Metal Shading Language is C++14-based; the fixed-function pixel and vertex
   shaders this generates are simple enough that the translation is mostly syntax.
4. Runtime compilation on Metal is `newLibraryWithSource:` on `MTLDevice`. Cache compiled pipeline
   states the way the D3D11 backend caches — 42 distinct stage programs across four maps is small
   enough to compile on demand and keep.
5. SPIRV-Cross is an alternative to writing an MSL emitter: HLSL → SPIR-V → MSL, offline or at
   runtime. It is a real dependency and another moving part. If you take it, say why here; if you
   reject it, say why here.

## Done when

- The HLSL emitter's output is byte-identical to the current generators for every case the existing
  six test suites cover. That is the regression gate and it should be checked in as a comparison
  test if it is not already one.
- An MSL emitter produces source that `newLibraryWithSource:` compiles for the same cases.
- `test_ffshader`, `test_ffvertex` and the four others pass on Windows, and the platform-independent
  ones pass on macOS.

## Do not

- Do not change what the shaders compute. This is a translation task. If the picture changes, the
  IR lost something.
