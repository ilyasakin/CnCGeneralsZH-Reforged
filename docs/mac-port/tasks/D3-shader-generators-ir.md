# D3 — Shader generators target SDL3 GPU (was: emit an IR)

- **Milestone:** M3
- **Depends on:** nothing since decision 4 (it depended on D2 when the plan was an IR)
- **Blocks:** D4
- **Status:** in review (-a9)
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

  > **Corrected 2026-09-26 (D3).** glslang does not keep every declared resource; it strips
  > unused textures as DXC does. The bumped terrain passed through glslang only because it samples
  > all four stages. The collision is SDL_shadercross's: it pairs samplers with textures by
  > position, and a texture read through another slot's sampler gets no index of its own. Through
  > glslang, ffshader's normal mapped programs collide the same way. The SDL3 target now gives every
  > texture its own sampler (below), which removes the collision for either compiler. Decision 4
  > was corrected to match: DXC is rejected on footprint alone.
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
  - it fails 3 of the game's 49 programs on Metal, for a reason in shadercross's pipeline (since
    corrected: the reason is shadercross's pairing and hits glslang too; see above);
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

## Done under decision 4 (-a9, 2026-09-26)

### What changed, and what did not

- **The generators compile without the DirectX SDK.** ffshader, ffvertex and engineshader read
  D3D9 fixed-function state and nothing else.
  - They named it through `<d3d9.h>`, which exists only on Windows. They now name it `FF_*`, with a
    `FixedFunctionValue` type.
  - `ffstate.h` and `ffstate_values.h` hold these. `ffstate_values.h` is the one table, generated
    from mingw-w64's `d3d9types.h`, not typed.
  - On Windows, `ffstate.h` is `<d3d9.h>` as before, `FixedFunctionValue` is `DWORD`, and all 78
    values are `static_assert`ed equal to the SDK's.
  - Off Windows it is the table alone. No name the Windows SDK owns is defined anywhere.
  - So the answer to "do the generators compile standalone on POSIX without WW3D2" is: yes,
    `ffshader.cpp`, `ffvertex.cpp` and `engineshader.cpp` need `ffstate.h` and the C++ standard
    library, and nothing else. They built with clang `-Wall -Wextra` and with mingw-w64, with no
    warnings.
- **An SDL3 GPU target.** `COMBINER_SHADER_TARGET_SDL3_GPU` and `VERTEX_SHADER_TARGET_SDL3_GPU`
  are new. The engine programs take the target as a defaulted last parameter, so no caller changed.
  - Each generator answers the SDL3 target by writing its D3D11 text and rebinding it
    (`sdl3target.h`). The D3D11 and D3D9 code paths are untouched.
  - The rebinding is five rules; they are D4's contract below.
- **One compile seam.** `sdl3shadercompile.cpp` has three functions:
  - `SDL3_Compile_HLSL_To_SPIRV` is the only place the tree names glslang;
  - `SDL3_Translate_SPIRV_To_MSL`;
  - `SDL3_Create_Shader`.

  test_shader_sdl and w3d_view go through it. Taking decision 4's exit (DXC through
  SDL_shadercross, or Slang) is that one function and its link line.
- **Windows compiles exactly what it did, apart from the rename.** The generated D3D9 and D3D11
  text is byte-identical (next section).

### The byte-identical proof

`Tests/shader_cases.h` holds 69 cases:

- the 49 decision 4 was measured on, copied field for field from `test_ffshadercompile`,
  `test_ffvertexcompile` and engineshader;
- 20 that reach the branches those tests never did: normal mapped and shadow receiving pixel
  programs, normal mapped and pre-transformed vertex programs, and the engine pixel programs with
  the alpha test and the fog.

`Tests/ffshader_dump` writes every case for every target, one file each, with an index of what was
refused.

- **The golden:** built with mingw-w64 from the generators before any change (`feature/mac-port`
  `e3b2c9ed`) and run under Wine. 108 D3D9 and D3D11 programs; the 30 others are refusals or do not
  apply, such as a normal map on D3D9. Its 49 reference programs are byte-identical to the
  D-spike's capture, which confirms the case table matches the tests.
- **After the rename commit (`034a9015`) alone:** 108 of 108 identical, index identical (macOS
  build).
- **After the whole change,** in three builds:

  | Build | D3D9 and D3D11 programs | Index |
  |:--|:--|:--|
  | mingw-w64 Windows view, under Wine | 108 of 108 identical | identical |
  | native macOS | 108 of 108 identical | identical |
  | the tree's own `ffshader_dump` target | 108 of 108 identical | identical |

  The 69 SDL3 programs are identical between the Windows view and macOS.
- **`windows_view_diff` refuses this change,** and that is a limit of the tool, not a result. Its
  control needs a *modified* file with a platform conditional, and the only one here is the new
  `ffstate.h`, which it skips as added. Its own `view()` was run on the six modified generator files
  instead. MSVC x64's view differs by exactly:
  - 94 lines that differ only by `D3D*_X` → `FF_*_X` and `DWORD` → `FixedFunctionValue`;
  - the include swap, the two new enumerators, the two defaulted parameters and the SDL3 wrappers.
- **mingw-w64 syntax checks, before and after:** `test_ffshader`, `test_ffvertex`,
  `test_engineshader`, `test_ffshadercompile`, `test_ffvertexcompile`, `test_ffvertexd3d9` and
  `ffshadercache.cpp` are clean in both. `dx11backend.cpp` has the same 2 errors in both. They are
  pre-existing: it calls `sqrtf` without `<math.h>`, which MSVC's headers bring in and mingw's do
  not.
- mingw defines `_WIN32` but not `_MSC_VER`. The only conditional in this change is on `_WIN32`, so
  mingw takes MSVC's branch here.

### D4's input: the SDL3 GPU contract

What a program written for `SDL3_GPU` expects of the backend that binds it. `sdl3target.h` writes
it and `test_shader_sdl` checks it; the backend must honour it.

1. **Register spaces.**
   - Vertex programs: textures and samplers in `space0`, constant buffers in `space1`.
   - Pixel programs: textures and samplers in `space2`, constant buffers in `space3`.
   - This is SDL_gpu.h's rule for `SDL_CreateGPUShader`. A constant buffer in set 0 is refused.
2. **Varying locations by semantic.** Every vertex output and pixel input carries
   `[[vk::location(N)]]`:
   - `COLOR0` 0 and `COLOR1` 1;
   - `TEXCOORD0`-`TEXCOORD7` 2-9;
   - `FOG` 10.

   Two stages link by semantic, whatever each declares. **The rule for anyone adding a program:**
   never rely on declaration order. glslang numbers by declaration order. The engine's Trees vertex
   program writes two coordinate sets and the fog, while every pixel program declares four
   coordinate sets and the fog. By order, the tree's fog would arrive as the pixel's `TexCoord2`.
   A new semantic goes into the table in `sdl3target.h`; an unknown one is refused.
3. **Vertex attribute locations by semantic:**
   - `POSITION` 0, `NORMAL` 1;
   - `COLOR0` 2, `COLOR1` 3;
   - `TEXCOORD0`-`TEXCOORD7` 4-11.

   The backend builds each SDL vertex layout from the FVF by this table.
4. **Vertex colours are read as BGRA.** The backend declares a `D3DCOLOR` attribute
   `UBYTE4_NORM`, over the same bytes. The program swaps `.bgra` itself: one line after `main`'s
   brace, for each colour attribute. D3D11 does the swap with `DXGI_FORMAT_B8G8R8A8_UNORM`
   (`dx11layout.cpp:26`); SDL has no such format.
5. **Textures and samplers bind as pairs.**
   - **Pairs by slot.** Slot n is texture n with sampler n. A program that reads a texture through
     another slot's sampler is rebound, and says so in its first lines:

     ```
     // SDL3 slot 4: texture t4, sampler state s0
     // SDL3 slot 6: texture t4, sampler state s1
     ```

     Each line means: bind the texture the D3D11 path had at `tT` to slot N, with stage S's sampler
     state. Every slot not listed is texture n with stage n's sampler state. Today only the normal
     map is rebound: `t4` through `s0`, and in the bumped terrain also through `s1` (at slot 6, a
     second name for the same texture).
   - **Slot count.** A program is created with `num_samplers = highest texture slot + 1` and
     `num_uniform_buffers = highest constant buffer slot + 1`, read from its SPIR-V bindings
     (`SDL3_Shader_Slots`). Not the count of resources used.
     - glslang drops declared textures a program never reads, so a program reading `t0` and `t5`
       uses two and needs six.
     - shadercross's reflection would give two, and `[[texture(5)]]` would never be bound. Metal
       accepts that shader without complaint, so nothing but this rule catches it.
   - **Every slot bound.** The backend binds all `num_samplers` pairs, filling the ones the program
     does not read.
6. **Constant blocks are the D3D11 ones, byte for byte.**
   - `VertexPipeline` is `append_constants_d3d11`'s order. `CombinerConstants` and the engine's
     blocks are as declared.
   - Every field is a `float4` or a `row_major float4x4`. A column-major matrix's 16 floats are
     uploaded unchanged, because the program multiplies `mul(v, M)`.

Two findings about which program meets which, for D4's pipeline cache:

- **Which pairs occur.** Trees (`W3DTreeBuffer`) binds `Trees.vso` and no pixel shader: it loads
  `Trees.pso` and never binds it. So Trees meets only ffshader's programs. The engine's `.pso`
  programs meet only ffvertex's. Trees with a water or terrain pixel program does not link on
  Metal (inputs `user(locn4)`, `user(locn5)` not written), and the game never draws it.
- **Unread inputs are fine.** Pixel inputs a vertex program does not write, and which the pixel
  program does not read, do not stop a pipeline: glslang drops them.

### test_shader_sdl, the POSIX twin

For each of the 69 programs under `SDL3_GPU` it runs these steps:

1. generate the SDL3 text;
2. compile it through `SDL3_Compile_HLSL_To_SPIRV`;
3. run `spirv-val --target-env vulkan1.0` on the result;
4. translate it to MSL through SDL_shadercross;
5. check the slot contract in that MSL: each `[[texture(n)]]` has `[[sampler(n)]]` and every
   n < `num_samplers`;
6. on macOS, create it on a real Metal device (`SDL_CreateGPUShader`, which is the driver's
   `newLibraryWithSource`, not the uninstalled `xcrun metal`).

A second test links every vertex and pixel pair the game can form into a real pipeline, on macOS.
Any step that cannot run (no `spirv-val`, no GPU) prints `SKIPPED`, with its reason, on a line of
its own.

| Platform | Reference set (49) | Beyond it (20) | Pairs linked |
|:--|:--|:--|:--|
| macOS 27.0, Apple M3 Pro, clang 21 | generated, compiled, spirv-val, MSL, slots kept, Metal: 49/49 each | the same, 20/20 each | 866/866 on Metal |
| Linux arm64, ubuntu:24.04, GCC 13.3 (`linux-check.sh`) | test_shader_sdl passed; full build 0 failures; ctest 25/25 | — | Metal only |
| Linux arm64, ubuntu:24.04, Clang 18.1 (`linux-check.sh`) | the same | — | Metal only |
| Linux amd64, both compilers | **not run**: the disk filled and OrbStack's Docker engine stopped during the amd64 image build | | |

**What the Linux rows do not show.** `linux-check.sh` prints a test's output only when it fails. So
the arm64 rows show that test_shader_sdl *passed*, not that its spirv-val step *ran*: a missing
`spirv-val` prints SKIPPED and still passes. The image now carries spirv-tools for that reason. The
run that reads the step through `ctest -V` is written and ready (the SPIRV-Cross translation and
the slot contract run on Linux regardless). It was blocked by the same disk.

The numbers above were read through `ctest -V`, not from a direct run of the binary.

### The stretch: one program draws

`w3d_view --shaders generated` draws the Crusader with the generators' own programs:

- **Vertex:** ffvertex for a lit, uncoloured mesh (position, normal, one coordinate set; one
  directional light; material colours).
- **Pixel:** ffshader for stage 0 modulating the texture by the lit colour, which is the reference
  set's `ps_op_modulate`, byte for byte. With the alpha test the game sets (`shader.cpp:476-479`:
  `GREATEREQUAL` against 0x60) for the alpha-tested draws.
- **Plumbing:** compiled through `sdl3shadercompile`, bound by the contract above. The pixel
  programs declare 4 sampler slots and read one; all 4 are bound.
- **The frame** is the D-spike's Crusader. It differs from the spike's own shader by up to
  40/255, because the generator lights per vertex and saturates, as D3D9's fixed function does,
  where the spike's shader lit per pixel. The Linux lavapipe render of this route was blocked by the same disk; the
  D-spike's lavapipe renders of the spike's own shaders stand.
- **What this proves:** one of the 49 (`ps_op_modulate`) and two programs outside them now draw,
  not only compile.

### Vendoring

`Tools/vendor.sh` fetches the three libraries off Windows; `vendor.ps1` says it skips them.

| | Pinned | Vendored |
|:--|:--|:--|
| glslang | `168d452a` = `vulkan-sdk-1.4.357.0`. **Past the HLSL front end's deprecation** (KhronosGroup/glslang#4210, removal not before about 2027-10); the `vendor.sh` comment says so for whoever bumps it | 6.9 MB, 152 files, from 42.6 MB: no `Test/`, no gtests, no CI |
| SPIRV-Cross | `1a616956`, the commit SDL_shadercross pins, which the D-spike measured | 3.2 MB, 46 files, from 11.8 MB: no `reference/`, `shaders*/`, `samples/` |
| SDL_shadercross | `1ff05bec` (it has no releases) | 0.13 MB, 5 files: the source, the header, the licences |

That is 10.3 MB in 203 files, fetched and unpacked in 15 s.

- **Build.** CMake builds glslang and SPIRV-Cross as `EXCLUDE_FROM_ALL` subprojects, with
  `CMP0077` set: without it, SPIRV-Cross's `option()` ignores the parent's settings silently. Only
  core, GLSL, MSL and the C API of SPIRV-Cross are built.
- **shadercross** is built from its one C file, without its CMakeLists: its vendored mode demands
  DXC's source even with DXC off.
- **Footprint.** A clean build of the three is 54 steps, 30 s on 11 cores, measured in the
  prototype under load from other sessions' containers. Runtime is about 5 MB: glslang with HLSL
  2.25 MB plus SPIRV-Cross (MSL + GLSL) 2.20 MB, stripped, arm64, per the D-spike.
- Configure prints glslang's own deprecation warning, on purpose.

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
