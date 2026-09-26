# A — the renderer on POSIX through a D3D9-shaped device (decision 7)

Owner: -a9. The route and its measurements are in [`RENDERER-ROUTE-RECON.md`](../RENDERER-ROUTE-RECON.md),
and the decision is in the README (decision 7). Phases: **A0** (the only Windows-visible one), **A1**
(a POSIX-only header of the D3D9 names, so WW3D2 and W3DDevice compile and link), **A2** (CPU-backed
resources), **A3** (the draw on SDL3 GPU).

## A0: Win32 types out of the renderer (2026-09-26)

**Scope.** WW3D2 and W3DDevice, as the POSIX build compiles them: lines inside `#if _WIN32` are left
alone, since they never reach the POSIX device. Found with `unifdef -b` (Windows macros unset,
`__APPLE__` set), which keeps line numbers.

Left out, and why:
- the D3D11 backend (`dx11*`), `d3dx9runtime`, `d3d8shadertranslate`: Windows-only by definition;
- `test_dx8_smoke`/`test_dx9_smoke`: Windows tests;
- `FramGrab` (the AVI writer) and `dx8webbrowser`: not built off Windows;
- `render2dsentence` (GDI fonts): D6's;
- `W3DMouse` (the hardware cursor): C3's;
- `d3dx9math.h`: its `FLOAT`s are all in its Windows branch.

`WORD`, `BYTE`, `BOOL`, `UINT`, `USHORT` and `LPCSTR` stay: `bittype.h` already gives them their
Windows widths off Windows (B10's decision, recorded there).

**The conventions.** B5's rule: a scalar in disguise becomes an engine type, and a type that crosses
a boundary gets a platform name.
- **A scalar that never reaches the device by address** becomes each library's own type:
  - `DWORD` becomes `uint32` in WW3D2 (its idiom: `bittype.h`, 121 uses already) and
    `UnsignedInt` in W3DDevice;
  - `LONG` becomes `Int`, `ULONG` becomes `uint32`, `FLOAT` becomes `float`, `CONST` becomes
    `const`.
- **A type that crosses the device interface** takes a name from `Libraries/Include/Platform/RenderTypes.h`,
  which on Windows **is** the SDK type (a typedef), and off Windows a fixed width:

  | Name | Windows | Off Windows | For |
  |:--|:--|:--|:--|
  | `RenderUInt32` | `DWORD` | `uint32_t` | a variable the device writes through a pointer (`GetRenderState`, `GetTextureStageState`, `ValidateDevice`), an array it reads (vertex declarations, shader bytecode) |
  | `RenderResult` | `HRESULT` | `int32_t` | a device call's result, and functions passing one on |
  | `RenderRect`, `RenderPoint` | `RECT`, `POINT` | `{int32_t ...}` | rectangles and points handed to the device |
  | `RenderWindow` | `HWND` | an opaque pointer | the device's window (`_Hwnd` in `dx8wrapper.cpp` and `ww3d.cpp`), which is C2's |
  | `Render_Failed`, `Render_Succeeded` | `FAILED`, `SUCCEEDED` | `< 0`, `>= 0` | the tests on a result; `HRESULT` is signed and `FAILED` tests `< 0`, and so do these |
  | `RENDER_FAIL` | `E_FAIL` | `0x80004005` | the renderer's own generic failure |

  `S_OK` becomes `D3D_OK`, D3D9's own name, which the SDK defines as `S_OK`.
- **Where the header comes in.** `RenderTypes.h` includes `<windows.h>` on Windows. So it comes
  into `dx8wrapper.h` straight after `<d3d9.h>`, and into any other file only after that file's own
  includes, and only where the file already used an SDK type there, so that `windows.h` is always
  in before it. That keeps `win.h`'s and winsock's ordering where it was. Each insertion point was
  checked.

**What changed.**
- 3 commits:
  - `fix(ww3d2)`: 20 in-class `Class::member` declarations that MSVC tolerates and clang refuses,
    found by re-sweeping until none were left, plus `PASS_MAX`. macOS's `<limits.h>` defines that as
    a macro; Westwood's own Solaris `#undef` is widened to cover it;
  - `feat(platform)`: `RenderTypes.h`;
  - `refactor(ww3d2,w3d)`: 37 files.
- **The first pass was scripted, and every result was read.** The script's mistakes, all corrected
  before commit:
  - `RenderUInt32` on reinterpreting casts (`F2DW`, the fog and depth-bias casts) and on local
    tables and arrays;
  - a file-wide name match that caught loop counters;
  - `Vertex_Format`'s definition and declaration getting different types;
  - `SceneClass::POINT` taken for the Win32 type;
  - four edits inside comments;
  - a header whose `<d3d9.h>` came after the insertion point.
- **Every `HRESULT`, `FAILED` and `SUCCEEDED` site was read.** Each tests a D3D or D3DX result, a
  `RenderResult` variable, or an engine function that returns one. The two `%lx`/`%ld` formats
  print casts or `unsigned long`s that did not change.

**Checked.**
- `windows_view_diff.py HEAD..WORKTREE` over the 37 files, normalised by the table above: every
  changed line is its old self under the substitutions, except the two intended changes (W3DWater.h's
  qualifications; `shader.cpp`'s `ULONG` mask, 32 bits either way).
- `RenderTypes.h` compiles with mingw-w64 against the real `windows.h` and with clang off Windows. Its
  sizes are `static_assert`ed: 4, 4, 16 and 8 bytes, and `RenderResult` signed.
- The macOS build and ctest (42 of 42) are unaffected.
- **Not compiled with MSVC.** WINDOWS-DEBT has the row.

**Left for A1** (Win32 calls, not types, in code the POSIX build compiles; they go behind
`#if _WIN32` with POSIX bodies where the device needs one):
- `dx8wrapper.cpp`: `LoadLibrary` of d3d9 (`HINSTANCE`), the monitor and gamma DCs (`HDC`,
  `GetDesktopWindow`, `GetDC`), `GetWindowLong`/`SetWindowPos`/`ShowWindow`/`GetClientRect` on the
  device window, and `Direct3D11_Create`;
- `ww3d.cpp`: `GetWindowRect`;
- `W3DDisplay.cpp`: the BMP screenshot writer (`CreateFile`/`WriteFile`, `HANDLE`, `LPBYTE`), the
  process launch (`dwTmp`, `exitCode` stay `DWORD`), and `ApplicationHWnd`, which is C2's;
- `W3DWaterTracks.cpp` (`ApplicationHWnd`), `W3DInGameUI.cpp` (`GetDC`),
  `texturethumbnail.cpp` (`FindFirstFile` for `*.mix`, which should go through C1's listing),
  `part_ldr.cpp` (`lstrlen`), `surfaceclass.cpp` (`ZeroMemory`);
- `W3DShaderManager.cpp`: `*(LARGE_INTEGER*)&m_driverVersion = did.DriverVersion`. A1's header
  decides the type of `D3DADAPTER_IDENTIFIER9::DriverVersion`.

## A1: WW3D2 and W3DDevice compile and link off Windows (2026-09-26)

**The checkpoint holds.** WW3D2 and W3DDevice compile on macOS with no errors, and `w3d_link_probe`
links all of both (whole archive, `-force_load` on macOS) against `posixdevice` and `gameengine`, so
every symbol they reference resolves; `nm` shows W3DDisplay and DX8Wrapper in it. Both libraries and the
probe are in the default build. From 152 failing translation units to 0.

**The D3D9 names.** `Libraries/Include/Platform/D3D9Posix.h`, POSIX only, written from Direct3D 9's
published values and `#error` on `_WIN32`; no `DWORD`, `HRESULT`, `HWND`, `IID` or `GUID`.
`D3D9PosixMath.h` holds `D3DVECTOR` and `D3DMATRIX` alone, for `d3dx9math.h`, so GameEngine still sees
no more of D3D9. `Platform/PosixD3D9/{d3d9,d3d9types,d3d9caps}.h` redirect to it, on the POSIX renderer
targets' include path only, so no `#include <d3d9.h>` changed.
`Tools/d3d9posix_check.py` compiles the header against MinGW-w64's `d3d9.h` and asserts 404 enumerators,
156 macros, 23 function-like macro samples, `D3DDECL_END()`'s fields, 22 structures (204 field offsets)
and both IIDs' bytes; a false control must fail, and it fails on any function-like macro without samples
or enumerator without a written value. `*_FORCE_DWORD` is not compared: Wine gives some `0xffffffff`,
Microsoft `0x7fffffff`; the header uses Microsoft's and asserts the four-byte width. -18 second-read the
header and `RenderTypes.h`; their one finding (`IID`) and both hardenings are in.

**The device** (`GameEngineDevice/Source/PosixDevice/Render/`, library `posixd3d9`, split by file with
-18, who owns A2): `PosixDevice9.h` declares the adapter and the device; `Direct3DCreate9` returns a real
adapter; `CreateDevice` accepts a null window, since `-headless` starts no video and its device has to
work as Windows' hidden-window one does. With no window a draw succeeds and does nothing; with one it
fails loudly until A3. State is kept as set; D3D9's documented defaults for the render states are A3's.

**D3DX off Windows** (`WW3D2/d3dx9posix.cpp`): the matrix functions compute (renderer only: the
simulation's `D3DXVec4Transform`/`D3DXVec4Dot` stay `d3dxportable.h`'s); `Get_FVF_Vertex_Size` computes
from the FVF bits; the pointers stay pointers, since call sites test them, and all are bound. Shader
assembly fails for good (A3 draws generated HLSL). The texture helpers are -18's
(`d3dx9posix_texture.cpp`). `d3dx9posix_selfcheck` checks the FVF size against 12 of `dx8fvf.h`'s vertex
structures, and the matrices against a double-precision reference and known images; four mutation
controls fail it. It does not compare rounding with `d3dx9_43.dll`: `Tests/d3dx_oracle` could, and that
is left for A3, where the matrices reach the picture.

**Direct3D 11 off Windows:** `dx11runtime_posix.cpp` answers all 76 calls, and the buffer twin's lock, as
a machine where Direct3D 11 failed to start.

**The compile fixes**, by kind (the commits list each):
- MSVC tolerances clang refuses, spelled the ISO way with MSVC's meaning: functional casts, `false` as a
  null pointer, default arguments at the definition, `register`, friend-only names, a dependent base's
  members, in-class `Class::member`, extern-then-static, `StringClass(NULL)`, `StringClass` through
  varargs, rvalues to non-const references, the address of a temporary, forward-declared enums;
- `Xfer.h` first in six draw modules: it reaches `BitFlagsIO.h`, whose templates use `Xfer` before it
  is declared, and clang parses template bodies where they stand. `Drawable.h` goes first now;
- Win32 API with an exact C equivalent at the call site (`lstrcpy`, `lstrcpyn`, `ZeroMemory`, ...);
- Win32 behaviour with no POSIX meaning in the renderer behind `_WIN32`, each with its POSIX answer
  beside it: the registry, the embedded browser, window style and size, desktop gamma and display mode,
  movie capture, the front-buffer screenshot, the `.ANI` cursor images, the wave editor, the ffmpeg
  launch;
- 335 includes respelled to the on-disk case (`include_case_check` reads W3DDevice now).

**Handed on:**
- **C2 (-18):** define `RenderWindow ApplicationHWnd` (null under `-headless`) and
  `Bool ApplicationIsBorderless` off Windows; W3DDisplay's window style and size calls are no-ops off
  Windows, so the window follows a display-mode change on C2's side. `W3DGameClient` off Windows leaves
  `createKeyboard`, `createMouse` and `createVideoPlayer` to the platform's subclass. The stand-ins in
  `PosixRenderHooks.cpp` go when `generals` links `w3ddevice` (-18 agreed to take that).
- **C3:** `W3DMouse` is out of the POSIX build; the cursor and the order-cursor images are yours.
- **D6:** `render2dsentence`'s GDI font is behind `_WIN32`; off Windows every glyph is blank and zero
  wide until the rasteriser lands.
- **A2 (-18):** resources, surfaces, `Clear`, the caps (VS/PS 0.0 until A3 implements a shader path,
  so `getChipset` answers `DC_UNKNOWN` and every renderer path is fixed-function).
- **A3:** draws, `Present`, gamma, render-state defaults, screenshots off Windows, stage-0 colour
  `D3DTOP_DISABLE`, the D3DX oracle comparison.

**Findings on the way, not changed:**
- W3DDisplay's Windows `CreateBMPFile` read `(w+7)/8*h*24` bytes from a `3*w*h` image: an over-read
  whenever the width is not a multiple of 8, and a skewed picture when it is not a multiple of 4.
  Defect #18, fixed after A1 by making the POSIX writer the one writer.
- `d3dx9math.h`'s Windows `D3DXMatrixInverseFunction` returns `HRESULT` where D3DX returns a
  `D3DXMATRIX *`; nobody reads the result, so nothing breaks today.
- `dx8fvf.h`'s `VertexFormatXYZNUV2DMAP` is 48 bytes against its FVF's 52; nothing sizes a buffer by
  either.
- CMake's comment calls `dx8webbrowser.cpp` inert; its header sets `ENABLE_EMBEDDED_BROWSER` to 1.
- Two pointer truncations, fixed because clang refuses them, neither reachable by a player:
  `surfaceclass.cpp`'s would fault above 4 GB but only `Font3D`, which the game never creates, calls it;
  `assetmgr.cpp`'s subtracts two truncated pointers, which is right modulo 2^32. Both are in the
  README's latent list. (An earlier report of mine called both shipping defects; that was wrong.)

**Windows:** every changed `.cpp` compiles under MinGW-w64 against the Windows headers with no error it
did not have before; the Windows `ww3d2` source set is unchanged. Not compiled with MSVC: WINDOWS-DEBT
has the row.
