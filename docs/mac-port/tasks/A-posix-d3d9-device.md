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
