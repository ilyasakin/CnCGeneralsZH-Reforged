# D3 — Shader generators emit an IR

- **Milestone:** M3
- **Depends on:** D2
- **Blocks:** D4
- **Status:** not started
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
