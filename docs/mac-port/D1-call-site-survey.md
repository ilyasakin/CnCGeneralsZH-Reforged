# D1 — survey of the direct Direct3D call sites

Step 1 of [D1](tasks/D1-dx8wrapper-funnel.md), done on its own. **No code changes.** This is the
plan whoever gets a Windows machine starts from, instead of a grep.

Measured on `feature/mac-port` at `4b08cb58`, 2026-09-22. Every number here is reproducible with
the commands shown; where a number disagrees with one already written down, the disagreement is
explained rather than averaged.

## 0. The short version

| | |
|:--|:--|
| The `236` in `dx11runtime.h` | **stale, not wrong.** It counted ~the right thing on 2026-09-09 and was overtaken by a commit landed the same day. The comparable number today is **125**. |
| The plan's `60` | **an undercount, and of the wrong thing.** 60 = 66 matches of the plan's own grep minus the 6 macro definitions in `dx8wrapper.h`; it still includes 13 sites inside `dx8wrapper.cpp`, which are not the problem, and misses 101 sites that are. |
| What D1 actually has to move | **170 call sites in 22 files** — 148 device calls reached through 89 escapes from the wrapper, plus 22 `DX8CALL` sites outside `dx8wrapper.{cpp,h}`. |
| How much of it needs new API | **about half.** 73 of the 148 have an exact `DX8Wrapper` method already, and it already mirrors into the D3D11 backend. 75 need something new. |
| Pull requests | **8**, ordered below. Not one, and not 60 one-liners either. |

The framing in the task file is wrong in three places, and each makes the job look different from
what it is: the 236 it quotes is stale (§1.3), the `DX8CALL` sites it waves off are not all
wrapper-internal (§2.6), and `Enable_Reports`, the progress meter it tells you to use, does not
exist (§3, "Progress meter").

## 1. The 236

### 1.1 Where it comes from

Three places in the tree say 236, and all three were written by the same commit:

```
Libraries/Source/WWVegas/WW3D2/dx11runtime.h:29   "reaches the Direct3D 9 device directly from 236 places"
Libraries/Source/WWVegas/WW3D2/dx11backend.h:23   "There are 236 places that do it and 5600 calls between them"
Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp:1337 "236 places still call the D3D9 device directly"
```

```console
$ git log --all --format='%ad %h %s' --date=short -S'236 places'
2026-09-22 0416292e docs: plan the macOS port
2026-09-10 a8582c9c feat(ww3d2): draw with Direct3D 11 by default, without the stutters
2026-09-09 bfb60e17 feat(w3d): native Direct3D 11 backend behind the D3D9 seam
```

`bfb60e17` is the origin — the commit that added the D3D11 backend. Its own message repeats it:
"The engine still reaches the D3D9 device from 236 places, so no draw goes through the backend
yet." The other two commits copied the sentence forward without re-measuring. `0416292e` is this
plan quoting it.

### 1.2 What it counted

No grep of the committed tree at `bfb60e17` returns exactly 236. The closest, and the only measure
of the right order of magnitude, is **every textual reference to the device accessor**, not just
the ones that immediately call through it:

| Measured at `bfb60e17`, `GeneralsMD/Code`, `*.cpp`/`*.h` | |
|:--|--:|
| `_Get_D3D_Device()` / `_Get_D3D()` — every occurrence | **233** |
| …including the `_Get_D3D_Device8()` spellings still left over from the D3D8 era | **246** |
| `_Get_D3D_Device()->` / `_Get_D3D()->` — the call form only | 167 |
| distinct lines mentioning the accessor | 229 |
| distinct enclosing functions | 81 |

233 against 236 is a working tree that moved by three lines between the measurement and the commit.
Nothing else is within 10%. Every alternative was tried and rejected: the `Generals/` tree adds
nothing (it is still on `_Get_D3D_Device8()`, 183 of them, and is not built); `DX8CALL` expansions
give 91; counting calls through captured device pointers as well gives 490.

**So: 236 ≈ "every place the accessor's name appears in Zero Hour's sources", including the
wrapper's own 35.** It is a fair measure of "how much of the engine knows about the D3D9 device",
which is the question `dx11backend.h` was asking. It is not a count of work items.

The companion "5600 calls between them" is not a static count of anything in the tree — the static
totals are two orders of magnitude smaller. It reads as a per-frame figure from
`number_of_DX8_calls`, the counter `DX8CALL` already increments. Treat it as a runtime number and
do not try to grep it.

### 1.3 Why it is stale

The comment was overtaken the day after it was written, by the commit that did the thing it says
has not been done:

```console
$ # _Get_D3D_Device8?()|_Get_D3D8?() occurrences in GeneralsMD/Code, walking forward
246   2026-09-09  bfb60e17  feat(w3d): native Direct3D 11 backend behind the D3D9 seam
135   2026-09-09  ef8303a9  feat(ww3d2): draw the game through the Direct3D 11 backend
136   2026-09-14  f4503a8d  feat(ww3d2): draw fullscreen games through direct3d 11
138   2026-09-14  48f932ed  feat(w3ddevice): reflect the scene in map water
138   2026-09-22  4b08cb58  (HEAD)
```

`ef8303a9` removed 111 of them in one commit — 45% of the funnel — and left the sentence claiming
none of it had happened. Two later commits (`48f932ed`, the water reflection) added three back.

**This is the part of the task file's framing that is wrong.** D1's "Why" section quotes the 236 as
a live statement of what is left. It is not; it is a snapshot from before the largest single piece
of the funnel landed. The three comments should be corrected to **125** (or deleted in favour of
pointing at this file), and `dx11backend.h`'s "rewriting those into something D3D11 shaped is not a
phase, it is a different program" reads very differently at 125 than at 236.

Both files also cite `RENDERER-ROADMAP.md`, which **has never existed in this repository** — not on
any branch, not in any commit, `git log --all --diff-filter=A` finds no such file. **Ten** files
reference it: nine sources (`dx11runtime.h`, `dx11device.h`, `ffprobe.h`, `ffshader.h`,
`d3dx9math.h`, `d3dx9runtime.h`, `d3d8shadertranslate.h`, `test_dx9_smoke.cpp`, `W3DDisplay.cpp`)
and `dx11-check.ps1`, plus D1's own task file. Whoever fixes the counts should decide whether it is
a lost document worth reconstructing or a reference to delete; either way, "phase 2" has no
definition anyone can read.

### 1.4 The current numbers

```console
$ grep -rIoE '_Get_D3D_Device\(\)|_Get_D3D\(\)' --include='*.cpp' --include='*.h' GeneralsMD/Code
```

| `GeneralsMD/Code`, at `4b08cb58` | |
|:--|--:|
| accessor occurrences — the measure `236` was | **125** |
| …outside `dx8wrapper.{cpp,h}` — escapes from the wrapper | **89** |
|   of those, calling straight through (`()->`) | 47 |
|   of those, capturing the pointer into a variable instead | 42 |
| device calls made through those 42 captured pointers | **101** |
| **device calls outside the wrapper, total** | **148** |
| `DX8CALL` family uses outside `dx8wrapper.{cpp,h}` | **22** |
| leftover `_Get_D3D_Device8()` (WorldBuilder ×2, `wwshade`) — out of scope, not built for Mac | 13 |

**148 + 22 = 170 call sites, in 22 files.** That is D1.

## 2. The categories

### 2.1 The pattern the plan's grep misses

Two thirds of the device calls are not reachable by grepping for `_Get_D3D_Device()->` at all.
They look like this (`W3DProjectedShadow.cpp:294`, and 22 more like it):

```cpp
LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();
...
m_pDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
m_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
```

One grep hit, twenty calls. `W3DWater.cpp` is worse: `m_pDev` there is a **class member**, assigned
once at `W3DWater.cpp:960` and `:1174` and then used 20 times across the file, so the device pointer
outlives the function that fetched it.

Any count that greps for `_Get_D3D_Device()->` sees 47 of these 148 calls. The other 101 are
invisible to it — and they include every draw call outside the wrapper.

The 42 escapes that do not call straight through are not all the same thing:

| Kind | Count | What it wants |
|:--|--:|:--|
| Captured into a local, then called through | 23 | the call sites move; the capture disappears with them |
| Captured into a **member** (`W3DWater.cpp:960`, `:1174`) | 2 | the same, but the pointer currently outlives the call |
| `_Get_D3D_Device() == NULL` guards | 14 | one `DX8Wrapper::Has_Device()`; no new thinking needed |
| The pointer handed to another subsystem | 3 | `W3DShaderManager.cpp:3322`/`:3352` into `d3d8shadertranslate`, and `dx8webbrowser.cpp:82` — the last is dead on macOS |

The 14 null guards are the cheapest 14 sites in the whole task and can ride along in any PR that
touches their file. Six of them are in `textureloader.cpp`, which otherwise has no device calls at
all.

### 2.2 By category

All 148, by what the engine is asking the device for:

| Category | Calls | Existing `DX8Wrapper` method | Needs |
|:--|--:|:--|:--|
| **State setting** | **43** | | |
| `SetRenderState` | 24 | `Set_DX8_Render_State` | nothing — swap |
| `SetTextureStageState` | 14 | `Set_DX8_Texture_Stage_State` | nothing — swap |
| `SetTransform` | 5 | `Set_Transform` | nothing — swap |
| **Shader constants and binding** | **19** | | |
| `SetPixelShaderConstantF` / `…Constant` | 13 | `Set_Pixel_Shader_Constant` | nothing — swap |
| `SetVertexShaderConstantF` | 4 | `Set_Vertex_Shader_Constant` | nothing — swap |
| `SetVertexShader` | 2 | `Set_Vertex_Shader` | nothing — swap |
| **Stream / draw** | **39** | | |
| `SetStreamSource` | 11 | `Set_Vertex_Buffer` (takes `VertexBufferClass`) | raw-buffer overload |
| `DrawIndexedPrimitive` / `DrawPrimitive` | 10 | `Draw`, `Draw_Triangles` | raw-buffer draw path |
| `SetIndices` | 8 | `Set_Index_Buffer` (takes `IndexBufferClass`) | raw-buffer overload |
| `SetFVF` | 6 | — | new |
| `SetVertexDeclaration` | 2 | — | new |
| `ProcessVertices` | 2 | — | new, or delete (see §2.5) |
| **Resource creation** | **21** | | |
| `CreateVertexBuffer` | 6 | — (`dx11twin` already hooks this in the wrapper) | new |
| `CreateIndexBuffer` | 5 | — (same) | new |
| `CreatePixelShader` | 4 | `Set_Pixel_Shader` exists, creation does not | new |
| `CreateOffscreenPlainSurface` | 3 | `_Create_DX8_Surface` | nothing — swap |
| `CreateTexture` | 2 | `_Create_DX8_Texture` | nothing — swap |
| `CreateRenderTarget` | 1 | `Create_Render_Target` | nothing — swap |
| **Targets and readback** | **10** | | |
| `GetRenderTarget` | 3 | — | new, one group |
| `GetDepthStencilSurface` | 2 | — | new, same group |
| `GetRenderTargetData` | 2 | — | new, same group |
| `GetBackBuffer` | 1 | — | new, same group |
| `StretchRect` | 1 | — | new, same group |
| `Clear` | 1 | `DX8Wrapper::Clear` | nothing — swap |
| **Queries** | **8** | | |
| `GetRenderState` | 4 | `Get_DX8_Render_State` — reads the shadow, no device round-trip | nothing — swap |
| `TestCooperativeLevel` | 4 | — | one named method |
| **One-offs** | **8** | | |
| `ShowCursor` | 4 | — | cursor group (see §2.4) |
| `SetCursorProperties` | 2 | — | cursor group |
| `SetCursorPosition` | 1 | — | cursor group |
| `EvictManagedResources` | 1 | — | one named method |

**73 of 148 need no new wrapper API at all** — an exact equivalent already exists and already
mirrors into the D3D11 backend. Those are a mechanical swap plus a pixel comparison. The remaining
75 are where the design work is, and 39 of those are the one problem of streams and draws.

### 2.3 By file

| File (under `GeneralsMD/Code/`) | Calls | Escapes | Shape |
|:--|--:|--:|:--|
| `GameEngineDevice/…/Shadow/W3DProjectedShadow.cpp` | 42 | 5 | 34 state, 6 stream/draw, 2 buffer creation |
| `GameEngineDevice/…/Water/W3DWater.cpp` | 29 | 11 | its own vertex/pixel shader path, end to end |
| `GameEngineDevice/…/W3DShaderManager.cpp` | 18 | 19 | shader constants, render targets, the translator |
| `GameEngineDevice/…/Shadow/W3DVolumetricShadow.cpp` | 16 | 7 | stream/draw, buffer creation |
| `GameEngineDevice/…/W3DSmudge.cpp` | 9 | 6 | state, offscreen surfaces, readback |
| `GameEngineDevice/…/W3DMouse.cpp` | 7 | 3 | the D3D9 hardware cursor, nothing else |
| `GameEngineDevice/…/W3DDisplay.cpp` | 6 | 3 | screenshot readback |
| `GameEngineDevice/…/HeightMap.cpp` | 6 | 6 | the `ProcessVertices` path |
| `GameEngineDevice/…/W3DSnow.cpp` | 4 | 4 | one draw |
| `Libraries/…/WW3D2/dx8vertexbuffer.cpp` | 3 | 4 | buffer creation, inside WW3D2 |
| `Libraries/…/WW3D2/dx8indexbuffer.cpp` | 2 | 3 | buffer creation, inside WW3D2 |
| `GameEngineDevice/…/W3DTreeBuffer.cpp` | 2 | 2 | one constant, one declaration |
| `GameEngineDevice/…/W3DShroud.cpp` | 1 | 2 | `TestCooperativeLevel` |
| `GameEngineDevice/…/W3DScene.cpp` | 1 | 2 | `GetRenderState`; the capture at `:1333` is unused |
| `GameEngineDevice/…/BaseHeightMap.cpp` | 1 | 2 | `TestCooperativeLevel` |
| `Libraries/…/WW3D2/ww3d.cpp` | 1 | 2 | `TestCooperativeLevel` |
| `Libraries/…/WW3D2/textureloader.cpp` | 0 | 6 | six null guards, no calls |
| `Libraries/…/WW3D2/render2dsentence.cpp` | 0 | 1 | one null guard |
| `Libraries/…/WW3D2/dx8webbrowser.cpp` | 0 | 1 | hands the device to the browser control; dead on macOS |
| | **148** | **89** | |

Five files hold 114 of the 148. `W3DProjectedShadow.cpp` alone is 28% of D1.

Plus the five files holding the 22 `DX8CALL` sites of §2.6, two of which (`textureloader.cpp`,
`dx8vertexbuffer.cpp`) are already above: `shader.cpp` (16), `textureloader.cpp` (4),
`dx8caps.cpp` (1), `dx8vertexbuffer.cpp` (1), `missingtexture.cpp` (1). Twenty-two files in all.

### 2.4 The cursor is not a renderer problem

`W3DMouse.cpp`'s 7 calls are `SetCursorProperties`, `SetCursorPosition` and `ShowCursor` — the D3D9
hardware cursor. Metal has no equivalent and never will; on macOS this is `NSCursor`. Putting them
behind a `DX8Wrapper` method would be inventing an abstraction for one platform's accident.

Recommendation: **take them out of D1 and hand them to C3 (input)**, which has to own the cursor
anyway. Note it in the task file rather than silently dropping it. That leaves D1 at 141 device
calls.

### 2.5 Two call sites worth deleting rather than moving

- `HeightMap.cpp:2054` and `:2216` call `ProcessVertices` — fixed-function vertex processing into a
  destination buffer, a D3D9 feature with no D3D11 or Metal counterpart. Before writing a wrapper
  method for it, find out whether this path still executes. `-ffprobe` can answer that.
- `W3DScene.cpp:1333` captures the device into `m_pDev` and never uses it. Dead line.

### 2.6 `DX8CALL` outside the wrapper is not "wrapper-internal and fine"

The task file says: *"`dx8wrapper.h:152–159` defines `DX8CALL`… Those are wrapper-internal and
fine."* That is true of the 69 inside `dx8wrapper.{cpp,h}`. It is not true of the 22 outside:

| File | `DX8CALL` uses | What they do |
|:--|--:|:--|
| `WW3D2/shader.cpp` | 16 | `SetTextureStageState(2, …)` and `SetTexture(2,0)` |
| `WW3D2/textureloader.cpp` | 4 | `UpdateTexture` |
| `WW3D2/dx8caps.cpp` | 1 | `GetDeviceCaps` |
| `WW3D2/dx8vertexbuffer.cpp` | 1 | `CreateVertexBuffer` |
| `WW3D2/missingtexture.cpp` | 1 | `CreateOffscreenPlainSurface` |

`DX8CALL` expands to `DX8Wrapper::_Get_D3D_Device()->x` and does nothing else. It does not mirror.
So `shader.cpp:954–1012` sets texture stage 2's combiner on the D3D9 device where **neither the
D3D11 backend nor the wrapper's own shadow state ever sees it** — compare
`dx8wrapper.h:1015–1040`, where the same call through `Set_DX8_Texture_Stage_State` writes
`TextureStageStates[stage][state]` and then calls `Direct3D11_Mirror_Texture_Stage_State`.

That is not only a funnel problem. The wrapper's redundant-state filter compares against a shadow
that these 16 calls have just invalidated, so a later `Set_DX8_Texture_Stage_State` for stage 2 can
decide a state is already set when the device holds something else. Worth checking whether it
explains any open rendering defect. **These 22 belong in D1's scope; the task file should say so.**

### 2.7 What D1 does not fix

`Peek_D3D_Texture()` and friends hand raw `IDirect3D*9` resource pointers straight out of the
texture classes — **134 uses, 64 of them outside WW3D2**. They are not device calls, so they are
not D1, but no backend that is not Direct3D can survive them. They are D2's, and D2 should be told
the number now:

| | |
|:--|--:|
| `Peek_D3D_Texture` | 87 |
| `Peek_D3D_Base_Texture` | 21 |
| `Peek_D3D_Surface` | 12 |
| `Peek_D3D_Volume_Texture` / `Peek_D3D_VolumeTexture` | 9 |
| `Peek_D3D_Cube_Texture` / `Peek_D3D_CubeTexture` | 5 |

## 3. The batching

Eight pull requests. The order is chosen so that every PR is independently bisectable, the risky
ones come after the cheap ones have proved the acceptance test works, and no PR mixes a call-site
move with a new wrapper method it also has to design.

Each of these is one task-file "Do" step for one category. Rule 4 of the plan (one task per PR) is
read here as one *batch* per PR; say so in each PR description.

| # | PR | Sites | Files | New API | Risk |
|:--|:--|--:|--:|:--|:--|
| 1 | Add the wrapper methods the later batches need | 0 | 1 | all of §2.2's "new" | none — nothing calls them yet |
| 2 | Move `W3DProjectedShadow`'s state setting | 34 | 1 | none | low |
| 3 | Move the remaining state, the queries and the one-offs | 18 | 8 | `Device_Is_Lost`, `Release_Unused_Resources` | low |
| 4 | Move the shader constants and shader binding | 19 | 3 | none | low |
| 5 | Route `shader.cpp`'s stage-2 `DX8CALL`s through the wrapper | 16 | 1 | none | **medium — fixes a live bug, see §2.6** |
| 6 | Move resource creation | 21 | 9 | uses PR 1 | medium |
| 7 | Move targets and readback | 10 | 3 | uses PR 1 | medium |
| 8 | Move streams and draws | 39 | 7 | uses PR 1 | **high — this is the one** |

157 sites over seven moving PRs: the 141 device calls left after the cursor goes to C3, plus
`shader.cpp`'s 16. PR 3's eight files are one or two lines each — `W3DSmudge`, `W3DVolumetricShadow`,
`W3DShaderManager`, `W3DScene`, `W3DWater`, `BaseHeightMap`, `W3DShroud`, `ww3d.cpp` — and it is the
PR that clears four whole files out of the survey.

Plus, outside D1: the 7 cursor calls to C3 (§2.4), and the two dead/doomed sites in §2.5 in
whichever PR touches their file.

**PR 1** is the only design PR. It adds, and nothing calls: a raw-buffer `Set_Vertex_Buffer` /
`Set_Index_Buffer` / FVF / vertex-declaration path, a raw draw entry, buffer and pixel-shader
creation, the render-target/readback group, `Device_Is_Lost()` for `TestCooperativeLevel`, and
`Release_Unused_Resources()` for `EvictManagedResources`. Name them after what the engine is asking
for, per the task file. It compiles, changes nothing, and can be reviewed on its own.

**PRs 2–4** are 71 of the 73 sites that need no new API (the other two are
`CreateTexture`/`CreateRenderTarget`, which travel with PR 6's file), split so no PR exceeds ~34
mechanical changes. PR 2 is a single file and is the right one to go first: it is the largest
single block, it is all state setting, and it is where the pixel-comparison acceptance test gets
proved on something whose failure mode is obvious. PR 3 carries the only two new methods that are
trivial enough not to wait for PR 1.

**PR 5** stands alone because it is the only batch that can change what is drawn — it is a bug fix
wearing a refactor's clothes (§2.6), and it must not be bisected together with changes that
cannot.

**PRs 6–8** consume PR 1's API. 8 is last because it is the only batch that touches the draw path
itself, and because by then `Enable_Reports` should be reporting close to zero, which makes a
regression in it visible immediately.

### Progress meter

The task file's step 4 says to use `Enable_Reports`. **There is no such symbol.** It appears once
in the whole tree, in the same stale comment block as the 236 (`dx11runtime.h:30`), and nothing
defines or calls it. Do not go looking.

What exists is better. `W3DDisplay.cpp:490–545` already logs, at shutdown of a `-dx11` run, a
refusal taxonomy that is exactly D1's progress meter:

```
-dx11 refusals: N no buffer, N no texture stage, N no input layout, N no program,
                N no device object, N engine shader bound, N unmirrored texture
-dx11 refused:  <the distinct states behind those counts>
-dx11 foreign:  <one line per shipped shader the refused draws had bound>
-dx11 drew:     <the pipelines that did resolve>
```

`no texture stage` and `unmirrored texture` are what §2.6's `shader.cpp` writes look like from the
backend's side; `no buffer` and `no input layout` are §2.2's stream/draw category. Every PR in the
table above should quote those seven counters before and after. That is what turns "several pull
requests" into something a reviewer can check, and the task file should name them instead of
`Enable_Reports`.

## 4. The 22 headers that leak D3D9 types — for D2

`grep -rIl --include='*.h' -E 'IDirect3D|D3DFORMAT|D3DMATRIX|D3DCAPS|D3DPOOL' …/WW3D2` returns 22,
confirming the plan's number. But they are not 22 of the same thing, and D2 should not treat them
as one list. "Outside" below is how many files outside WW3D2 include the header — the blast radius.

**Tier 1 — the real leaks. Engine code sees D3D9 types through these.**

| Header | Outside | Types exposed |
|:--|--:|:--|
| `dx8wrapper.h` | 29 | `D3DMATRIX` `D3DFORMAT` `D3DPOOL` `D3DCOLOR` `D3DCOLORVALUE` `D3DLIGHT9` `D3DMATERIAL9` `D3DADAPTER_IDENTIFIER9` `D3DPRIMITIVETYPE` `D3DRENDERSTATETYPE` `D3DTEXTURESTAGESTATETYPE` `D3DSAMPLERSTATETYPE` `D3DTRANSFORMSTATETYPE` `IDirect3DDevice9` `IDirect3DVertexShader9` `IDirect3DPixelShader9` `IDirect3DTexture9` `IDirect3DCubeTexture9` `IDirect3DVolumeTexture9` `IDirect3DSurface9` |
| `texture.h` | 21 | `IDirect3DBaseTexture9` `IDirect3DTexture9` `IDirect3DCubeTexture9` `IDirect3DVolumeTexture9` `IDirect3DSurface9` |
| `dx8indexbuffer.h` | 19 | `IDirect3DIndexBuffer9` |
| `dx8vertexbuffer.h` | 19 | `IDirect3DVertexBuffer9`, the `D3DFVF_*` constants |
| `surfaceclass.h` | 3 | `IDirect3DSurface9` |
| `ww3dformat.h` | 3 | `D3DFORMAT` |
| `formconv.h` | 1 | `D3DFORMAT` |
| `textureloader.h` | 1 | the five texture interfaces |
| `dx8caps.h` | 1 | `IDirect3D9` `IDirect3DDevice9` `D3DCAPS9` `D3DADAPTER_IDENTIFIER9` |
| `rddesc.h` | 1 | `D3DCAPS9` `D3DADAPTER_IDENTIFIER9` |
| `dx8texman.h` | 0 | `D3DPOOL_DEFAULT` |
| `missingtexture.h` | 0 | `IDirect3DSurface9` `IDirect3DTexture9` |
| `ddsfile.h` | 0 | `IDirect3DSurface9` `IDirect3DVolume9` |
| `dx8webbrowser.h` | 0 | `IDirect3DDevice9` — and the whole file is dead on macOS |

`dx8wrapper.h` and `texture.h` are the whole problem: 50 of the 82 outside-WW3D2 includes, and
between them every D3D9 type the engine can name. `dx8indexbuffer.h` and `dx8vertexbuffer.h` are
another 38 includes for one interface each, which is the easiest 38 to fix.

**Tier 2 — the D3D9/D3DX shim. It *defines* the surface; hiding it is a different job.**

`d3dx9runtime.h` (8 outside), `d3dx9math.h` (7), `d3d8shadertranslate.h` (3).

**Tier 3 — the D3D11 backend's own headers.** They mention D3D9 types on purpose, at the seam where
a D3D9 object is handed to the backend. D2 changes their signatures rather than hiding them.

`dx11resource.h` (3 outside), `dx11runtime.h` (2), `dx11texture.h` (1), `ffshadercache.h` (1),
`ffprobe.h` (1).

D2's target is therefore **14 headers, not 22**, and two of those fourteen are most of the work.

## 5. What this survey could not do

No Windows machine, so nothing here was compiled or run. Specifically not established:

- Whether `HeightMap.cpp`'s `ProcessVertices` path still executes (§2.5). `-ffprobe` answers it.
- Whether `shader.cpp`'s unmirrored stage-2 writes (§2.6) cause a visible defect today, or only a
  latent one. A `-dx11` run with `Enable_Reports` answers it.
- The current foreign-report count, which is the real progress meter and should be the first
  number in D1's own PR 1.
- Everything in D1's "Done when" — the pixel comparison, the frame cost on the Inferno Cannon
  scene, `ctest`. Unchanged and still required.

Categorisation is from reading the call sites, not from running them. Two are flagged in §2.5 as
possibly dead rather than assigned a category on the strength of a guess.
