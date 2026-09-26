# How the renderer reaches macOS without a Windows machine: a recon

Measured 2026-09-26 on `feature/mac-port` at `5897ed3d`, by -a9 (C1), for a routing decision. **No
code changes.** Every number below comes from a command on that tree; the scripts are described
where they matter.

## 0. The short version

| | |
|:--|:--|
| Where WW3D2 and W3DDevice stand on macOS | Neither is in the POSIX build. Compiled on their own (`-fsyntax-only`, gameengine's flags): **109 of 134** WW3D2 files and **84 of 91** W3DDevice files fail, **848 errors**. The errors undercount, because a missing `d3d9.h` is fatal at the first line. |
| What the engine asks of Direct3D | **~95 methods**: 66 of `IDirect3DDevice9`'s 116, about 20 on the texture, surface and buffer interfaces, and 11 on `IDirect3D9`. Plus **24 D3DX functions**. |
| What the engine *says* in Direct3D | **~5,000 uses of 477 distinct D3D names** in the code outside the wrapper and the D3D11 backend (92 files). **3,778 of them are fixed-function constants** (`D3DTSS_*`, `D3DTA_*`, `D3DTOP_*`, `D3DTEXF_*`, ...). D1's funnel moves calls; it does not touch any of this. |
| How the D3D11 backend hooks in | **It mirrors a real D3D9 device; it does not replace one.** The wrapper's calls are copied into D3D11 (`Direct3D11_Mirror_*`, twin buffers and textures) while D3D9 still creates, fills and locks every resource. Calls that escape the wrapper reach D3D9 only. **Off Windows there is no D3D9 underneath**, so no route can be a mirror. |
| Recommendation | **The POSIX D3D9-shaped device (route A)**, in four phases. D1 on POSIX only (route B) is route A plus 170 changed call sites, and does not avoid any of A's work. |
| Windows risk of A | **Low**: every file is POSIX-only except one mechanical step, the Win32 types, which is the B5 method again and checkable with `windows_view_diff.py`. |
| Two decisions it needs | (1) Whether a POSIX-only header may define the D3D9 SDK's names (the plan so far chose `FF_*` and `MSGBOX_*` for new code); (2) where reference images come from, with no Windows machine and no game executable. |

## 1. WW3D2 and W3DDevice on POSIX today

Neither target exists off Windows: `ww3d2` (and everything under it) sits in `CMakeLists.txt`'s
`if(ZH_PLATFORM_WINDOWS)` renderer block, and `W3DDevice` is part of the Windows-only
`gameenginedevice`.

**The sweep.** Every `.cpp`, compiled alone with `c++ -fsyntax-only -ferror-limit=0` and gameengine's
include paths and definitions, plus `WW3D2`, `wwshade`, `WWAudio`, `Miles6`, `Bink`,
`GameEngineDevice/Include`, `Main`, `Stubs`. 16 seconds for all 225.

| | Files | Failing | Errors |
|:--|--:|--:|--:|
| `WW3D2/*.cpp` | 134 | 109 | 385 |
| `W3DDevice/**/*.cpp` | 91 | 84 | 463 |

**The first error of each failing file**, which is what a real build would stop on:

| First error | WW3D2 | W3DDevice | What it is |
|:--|--:|--:|:--|
| `extra qualification on member 'operator='` / `'Compute_CRC'` | 41 | 34 | **Two header lines**: `rendobj.h:241` (`RenderObjClass & RenderObjClass::operator = (...)` inside the class) and `vertmaterial.h:274`. MSVC accepts the qualification; clang does not. |
| `expected identifier` | 12 | 22 | **One header line**: `shader.h:111`, the enum member `PASS_MAX`, which macOS's `<limits.h>` defines as a macro. |
| `'d3d9.h' file not found` | 24 | 3 | The D3D9 surface: sections 2 and 3. |
| `unknown type name 'TextureClass'` and kin | 13 | 11 | Downstream of the headers above. |
| `'windows.h' / 'io.h' file not found` | 5 | 7 | Win32 proper. |
| `'d3d11.h' file not found` | 3 | 0 | The D3D11 backend itself: Windows-only by definition, excluded off Windows. |
| `'SDL3/SDL.h'`, `srVector3i.hpp` | 2 | 0 | An include path, and `wwshade`, which the build already leaves out. |
| other | 9 | 7 | `XferVersion`, `HFONT`, `HANDLE`, `size_t` and the like. |

About 200 of the 848 errors are those three header lines. After them, the missing `d3d9.h` is the
wall, so the error counts say little about the D3D surface: the next two sections measure it from the
source instead.

## 2. The Direct3D surface the engine uses

### 2.1 Methods (calls through `->` or `DX8CALL`, in WW3D2 and W3DDevice, the D3D11 backend excluded)

Interface method lists are mingw-w64's `d3d9.h`. A method name shared by several interfaces
(`Lock`, `GetDesc`, `LockRect`, `SetPriority`) is counted under each, so the per-interface numbers
are upper bounds.

| Interface | Used / declared | The ones that matter |
|:--|--:|:--|
| `IDirect3DDevice9` | **66 / 116** | `SetTextureStageState` 33, `Reset` 21, `Clear` 19, `SetStreamSource` 16, `SetTransform` 13, `SetIndices` 13, `SetRenderTarget` 12, `DrawIndexedPrimitive` 12, `CreateTexture` 11, `SetRenderState` 11, `GetBackBuffer` 10, `SetTexture` 10, and 54 more used fewer than ten times |
| `IDirect3DTexture9` | 9 / 19 | `LockRect` 35, `UnlockRect` 32, `GetSurfaceLevel` 23, `GetLevelCount` 15, `GetLevelDesc` 8, `SetLOD` 5 |
| `IDirect3DSurface9` | 6 / 14 | `LockRect`, `UnlockRect`, `GetDesc` 27, `GetContainer` 1 |
| `IDirect3DVertexBuffer9`, `IDirect3DIndexBuffer9` | 5 / 11 each | `Lock` 50, `Unlock` 39, `GetDesc` 27 |
| `IDirect3DBaseTexture9` | 4 / 14 | `GetLevelCount`, `SetLOD`, priority |
| `IDirect3DSwapChain9` | 4 / 7 | `GetBackBuffer`, `Present`, `GetDisplayMode`, `GetFrontBufferData` |
| `IDirect3DCubeTexture9`, `IDirect3DVolumeTexture9` | a handful | cube maps for the environment, one volume `LockBox` |
| `IDirect3DStateBlock9` | 1 / 3 | `Apply` 9 |
| `IDirect3D9` | **11 / 14** | adapter enumeration, caps, `CheckDeviceFormat`, `CheckDeviceMultiSampleType`, `CreateDevice` |

**About 95 methods in all.** That is the size of a D3D9-shaped device the engine could run on.

### 2.2 D3DX (24 functions)

| Kind | Functions |
|:--|:--|
| Matrix and vector maths (the bulk) | `D3DXMatrixInverse` 21, `Scaling` 13, `Translation` 11, `Multiply` 10, `Transpose` 4, `Identity` 3, `RotationZ`, `D3DXVec3Transform`, `D3DXVec4Transform`, `D3DXVec4Dot` and the `D3DXMATRIX`/`D3DXVECTOR3`/`D3DXVECTOR4` types. B17 already puts a portable D3DX maths on the CRC path. |
| Texture helpers | `D3DXCreateTexture` 4, `CreateCubeTexture` 4, `CreateVolumeTexture` 2, `D3DXFilterTexture` 6, `D3DXLoadSurfaceFromSurface` 6, `CreateTextureFromFileExA` 1 |
| Shaders | `D3DXAssembleShader` 8 (the water, and `dx8wrapper.cpp`'s one path), `D3DXCompileShader` 1 (`ffshadercache`), `D3DXDisassembleShader` 1 (`d3d8shadertranslate`) |

The assembled shaders are D3's `engineshader` programs (trees, three water programs, six terrain,
roads), which D3 already generates for the SDL3 GPU target. A POSIX device needs only to recognise
what it is handed, as the D3D11 backend already does, rather than assemble anything.

### 2.3 Names (types, constants, interfaces), outside the wrapper and the D3D11 backend

**477 distinct names, 5,000 uses, in 92 files.** WW3D2 has 2,048 of them in 58 files, W3DDevice
2,952 in 34.

| Kind | Names | Uses | Commonest |
|:--|--:|--:|:--|
| Enum and flag constants | 317 | **3,778** | `D3DTSS_TEXCOORDINDEX` 127, `D3DTEXF_LINEAR` 121, `D3DTA_TEXTURE` 114, `D3DTSS_COLOROP` 106, `D3DTSS_ALPHAOP` 100, `D3DTOP_DISABLE` 98 |
| Other D3D names | 73 | 410 | `D3DLOCKED_RECT` 43, `D3D_OK` 38, `D3DSURFACE_DESC` 32 (plus the engine's own `D3DTexture`, `D3DSurface`, which only look like D3D) |
| D3DX | 37 | 381 | `D3DXMATRIX` 160, `D3DXVECTOR4` 73 |
| COM interfaces | 21 | 261 | `IDirect3DSurface9` 65, `IDirect3DTexture9` 42, `IDirect3DBaseTexture9` 30 |
| Types and structs | 21 | 106 | `D3DTRANSFORMSTATETYPE` 24, `D3DCAPS9` 22, `D3DFORMAT` 14 |
| Macros | 8 | 64 | the D3D8-era `D3DVSD_*` declarations, `D3DCOLOR_ARGB`/`XRGB` |

**The fixed-function state vocabulary is the renderer's language**, not a set of calls: it is in
the material, shader and mapper classes, not just at the device. D1's funnel moves calls into the
wrapper and leaves every one of these names where it is. Its own survey says so (§4, "the 22
headers that leak D3D9 types — for D2").

### 2.4 Win32 types in the same code

Outside the wrapper and the backend: **436 uses** of `DWORD` (133), `HRESULT` (60), `FLOAT`, `RECT`,
`BYTE`, `WORD`, `UINT`, `POINT`, `HWND`, `LONG`, `BOOL`, in 54 files. The wrapper adds 73. Every
route off Windows has to deal with these, because the plan does not define `DWORD` or `HWND` on
macOS.

## 3. How the D3D11 backend hooks in

`dx11backend.h` says it plainly: "What this is not is a Direct3D 9 device. It answers no COM
interface ... the calls have D3D9's names because the engine's calls have D3D9's names."

- **It is a mirror.** `DX8Wrapper` makes the D3D9 call and then copies it into D3D11:
  `Direct3D11_Mirror_Render_Target`, `_Surface_Copy`, `_Clear`, `_Stream_Source`, `_Indices`. The
  wrapper's own state calls land in `DX11BackendClass`'s shadow state, which it resolves at the draw.
- **Resources belong to D3D9.** Buffers and textures are created on the D3D9 device and get D3D11
  "twins" (`dx11twin`, `dx11texture`), refilled from the D3D9 copy.
- **What escapes the wrapper reaches D3D9 only.** That is D1's 148 calls: shadows, water, the shader
  manager, smudges, snow. `dx11runtime.h` counts them.
- **Size:** the `dx11*.cpp` family is 5,938 lines, and none of it has to allocate a texture or answer
  a `LockRect`, because D3D9 does.

So the D3D11 backend is the right *design* for a POSIX renderer: take the engine's D3D9-shaped calls
as they are, and resolve state at the draw into cached pipelines, which D3 has given an SDL3 GPU
target. But it is not a *base* to build on. Off Windows the device underneath has to be ours as well.

## 4. The routes

### A. A D3D9-shaped device, POSIX only, drawing with SDL3 GPU (recommended)

The engine compiles off Windows against a header that declares what section 2 lists, and runs on
classes that implement it. Resources live in CPU memory until a draw needs them on the GPU, and draws
resolve as `dx11backend` resolves them, onto D3's SDL3 GPU generators (`ffshader`, `ffvertex`,
`engineshader`). Escaping calls need no funnel: they reach this device directly, because it *is* the
device. Windows keeps its D3D9 device and D3D11 mirror untouched.

| Phase | What | Size I can defend | Windows risk |
|:--|:--|:--|:--|
| A0 | The three header lines (§1); the Win32 types in WW3D2 and W3DDevice (§2.4) moved to engine types, as B5 did for gameengine. **Checkpoint:** the sweep's errors are only D3D and genuine Win32 calls. | 3 lines; ~510 mechanical type edits | **The one Windows-visible phase.** `windows_view_diff.py` shows each change; `DWORD` → `UnsignedInt` keeps the size (32 bits on both), and `HRESULT` → an engine result type needs care where `FAILED()` is used. A WINDOWS-DEBT row. |
| A1 | A POSIX-only header declaring the 477 names and ~95 methods, D3DX maths from B17, and methods that fail loudly. **Checkpoint:** WW3D2 and W3DDevice compile and link on macOS. | ~1,200 lines of header, and CMake | None (POSIX-only files). Needs decision (1). |
| A2 | Resources: textures in the formats the engine uses (the 8888, 565, 1555 and 4444 families, DXT1/3/5, L8/A8), surfaces, locks, mip levels, cube maps, render targets, readback; buffers; D3DX's texture helpers. **Checkpoint:** WW3D2 loads every texture of the real install and reads it back, with CPU checks. | ~2,500 lines | None |
| A3 | The draw: `dx11backend`'s state-to-pipeline design on SDL3 GPU with D3's targets; FVFs and declarations to vertex layouts; `engineshader` recognition; the swap chain in C2's window. **Checkpoint:** frames. | ~3,500 lines | None |

**About 7,000 to 8,000 new POSIX-only lines, plus A0's mechanical edits.** The estimate stands on the
D3D11 backend's 5,938 lines for A3's half, and on section 2's counts for the rest.

### B. D1 finished, on POSIX only

That means moving D1's 170 call sites (148 escaped device calls and 22 `DX8CALL`s) into `DX8Wrapper`,
and then implementing the wrapper on SDL3 GPU. But:
- the sites are shared code, so the moves change Windows unless each one is `#if`ed. That Windows
  A/B is what stalled D1 from the start;
- it leaves the 5,000 D3D names and the resource interfaces (`LockRect` 35, `Lock` 50,
  `GetSurfaceLevel` 23, ... on objects the engine holds) exactly where they are, so it needs A0,
  A1 and A2 regardless;
- its draw work is A3's.

**B is A plus 170 changed sites.** Its only gain is a narrower device, and a POSIX device that has to
answer escaped calls as well costs little extra: the ~95 methods include them.

### C. A renderer interface under WW3D2 (D2's end state)

Rewriting the 5,000 names and 170 sites onto an abstract backend is the right long-term shape, and
the largest and most Windows-visible option. It needs Windows A/B throughout. It is not a route
*without* a Windows machine.

## 5. What A needs decided

1. **The names.** A1's header defines, off Windows only, names the D3D9 SDK owns (`IDirect3DDevice9`,
   `D3DFORMAT`, `D3DRS_ZENABLE`, ...). Nothing collides there, since there is no SDK off Windows, but
   the plan so far has chosen engine-own names for engine-facing code (`FF_*` in D3's generators,
   `MSGBOX_*` in B5). Renaming the 3,778 constants instead would be a very large Windows-visible
   diff for no runtime gain. **My recommendation:** one POSIX-only header, reviewed under the
   second-reader rule, with the D3D names and no `DWORD`, `HWND` or `HRESULT` (A0 has removed them
   from the engine first).
2. **Reference images.** Without a Windows machine or a game executable (E4), A3's frames have no
   Windows frame to compare with. Candidates: per-draw CPU checks against the fixed-function
   formulas (D3's generator tests already do this for single programs); the D3D11 backend's
   captures, if anyone has any; or the user's Windows install, if the user can run the published
   game with a capture tool. The PM's call.
3. **Where it lives.** A proposal: `GameEngineDevice/Source/PosixDevice/Render/` for the device, and
   the header under `Libraries/Include/Platform/`. C2 owns the window and the swap chain; A3 only
   needs a surface from it.

Also out of the renderer's way: `W3DMouse.cpp`'s hardware cursor goes to C3, as D1's survey §2.4
already says.
