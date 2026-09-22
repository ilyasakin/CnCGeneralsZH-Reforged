# D1 — Finish the DX8Wrapper funnel

- **Milestone:** M3
- **Depends on:** nothing — **this is Windows-side work and can start on day one**
- **Blocks:** D2
- **Status:** not started
- **Size:** 2,630 `DX8Wrapper::` call sites across 93 files; 60 `_Get_D3D_Device()->` reaches past
  the wrapper; 92 `DX8CALL` macro expansions in 7 files; 22 WW3D2 headers expose D3D9 types

## Why

`dx11runtime.h` says it plainly:

> RENDERER-ROADMAP.md's phase 2 is not finished while this is here: the engine still reaches the
> Direct3D 9 device directly from 236 places, and until those go through DX8Wrapper a -dx11 run has
> a backend that only the wrapper's own state calls reach.

Every backend that is not Direct3D depends on this being finished. It is the single longest task in
the plan and it has no dependency on any Mac work, so if there are two people, one of them starts
here.

It is worth doing on its own merits too, though **not** for the reason `README.md` gives. That file
still says a D3D11 frame costs 13.6 ms against D3D9's 8.6 ms; it was last edited on that line on
2026-09-13 and `b0a6379a perf(ww3d2): cut the direct3d 11 frame to 7.6ms in the inferno test`
landed on 2026-09-19. D3D11 is already the faster path on that scene and the README is stale.
Re-measure before quoting either number, and fix the README while you are in there.

## Recon findings, 2026-09-22 — this file's central number was stale and its scope was wrong in BOTH directions

Survey at `docs/mac-port/D1-call-site-survey.md`. Read it before starting.

**The 236 is stale.** It was written by `bfb60e17` on 2026-09-09 and counts every textual reference
to `_Get_D3D_Device()`/`_Get_D3D()` in the tree at that commit (233, or 246 counting leftover
`_Get_D3D_Device8()` spellings). The very next renderer commit the same day, `ef8303a9` "draw the
game through the Direct3D 11 backend", took it from 246 to 135. **The comment was overtaken within
24 hours of being written** and has claimed for two weeks that none of the funnel had happened. It
appears three times — `dx11runtime.h:29`, `dx11backend.h:23`, `dx8wrapper.cpp:1337` — and this
task file copied it forward a fourth time. Comparable figure today: 125. Fix the three comments.
The companion "5600 calls" matches no static count and reads as a per-frame figure off
`number_of_DX8_calls`; do not try to grep it.

**And the 60 in this file undercounts, in the more expensive direction.** Two thirds of the device
calls do not look like `_Get_D3D_Device()->`. They look like a local — or, in `W3DWater.cpp`, a
class member assigned at `:960`/`:1174` and used twenty times, so the device pointer outlives the
function that fetched it:

    LPDIRECT3DDEVICE9 m_pDev = DX8Wrapper::_Get_D3D_Device();   // one grep hit
    m_pDev->SetRenderState(...);                                 // twenty calls

Real surface: **89 escapes from the wrapper → 148 device calls, plus 22 `DX8CALL` sites outside
`dx8wrapper.{cpp,h}` = 170 sites across 22 files.** Five files hold 114 of the 148;
`W3DProjectedShadow.cpp` alone is 28%.

The good news: **73 of the 148 already have an exact `DX8Wrapper` method that already mirrors into
the D3D11 backend** — a swap, not a design. Only 75 need new API, and 39 of those are the single
problem of streams and draws.

**This file was wrong that `DX8CALL` is always wrapper-internal.** True of the 69 inside
`dx8wrapper.{cpp,h}`; false of the 22 outside, which are in D1's scope.

### A live bug, not just a funnel gap

`shader.cpp:954-961` and `:1003-1012` set **texture stage 2** through raw `DX8CALL`, with the
comment "bypass the wrapper since it only supports 2 texture stages". `DX8CALL` expands to
`_Get_D3D_Device()->x` and mirrors nothing — so under `-dx11` the D3D11 backend never sees stage 2
(despite `DX11_BACKEND_TEXTURE_STAGES` being 4 and able to), **and** the wrapper's own
`TextureStageStates` shadow never records it, which its redundant-state filter then compares
against. Check this against any open rendering defect before treating it as port work. It cannot be
verified here — no Windows machine — so it is recorded in `WINDOWS-DEBT.md` as a suspected live
defect rather than fixed blind.

### Two more corrections

- **`Enable_Reports` does not exist.** Step 4 of this file told you to use it; it appears exactly
  once in the tree, inside the same stale comment block as the 236. What exists is better and
  should be named instead: `W3DDisplay.cpp:490-545` already logs a seven-way refusal taxonomy (no
  buffer / no texture stage / no input layout / no program / no device object / engine shader
  bound / unmirrored texture) plus per-pipeline and foreign-shader reports. That is the progress
  meter.
- **`RENDERER-ROADMAP.md` has never existed in this repository** — not on any branch, not in any
  commit, though ten files cite it. "Phase 2", which this task is defined as finishing, has no
  definition anyone can read. Treat the survey's batching plan as the definition.

### Batching (from the survey, 8 PRs)

PR1 adds API and **calls nothing** — additive, zero behaviour change by construction, and the only
part of D1 safe to do without a Windows machine. PRs 2–4 are the 71 no-new-API swaps; PR2 is
`W3DProjectedShadow.cpp`'s 34 alone, the right place to prove the pixel comparison. PR5 is
`shader.cpp` on its own, because it is the only batch that can change pixels. PRs 6–8 consume PR1,
streams and draws (39) last.

**Move the 7 `W3DMouse` cursor calls out of D1 into C3** — they are the D3D9 hardware cursor, Metal
has no equivalent, and wrapping them would invent an abstraction for one platform's accident. That
leaves D1 at 141. Two sites are worth deleting rather than moving: `HeightMap.cpp`'s two
`ProcessVertices` calls (D3D9-only; check with `-ffprobe` whether the path still runs) and
`W3DScene.cpp:1333`, which captures the device and never uses it.

## Scope

```console
# the reaches past the wrapper — the actual work
grep -rIn --include='*.cpp' --include='*.h' -E '_Get_D3D_Device\(\)->|_Get_D3D\(\)->' \
  GeneralsMD/Code/GameEngine GeneralsMD/Code/GameEngineDevice GeneralsMD/Code/Libraries/Source/WWVegas

# D3D9 types in public headers — what D2 will have to hide
grep -rIl --include='*.h' -E 'IDirect3D|D3DFORMAT|D3DMATRIX|D3DCAPS|D3DPOOL' \
  GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
```

`dx8wrapper.h:152–159` defines `DX8CALL`, `DX8CALL_HRES` and `DX8CALL_D3D`, all of which expand to
`DX8Wrapper::_Get_D3D_Device()->x`. Those are wrapper-internal and fine. The 60 sites outside are
the problem.

**Re-measure before you start.** The 236 in the comment and the 60 this plan measured are not the
same number, and the difference matters for scoping. Whichever is right, put the real number in
this file.

## Do

1. Categorise the reaches first, in a commit of its own if it helps: which are state setting, which
   are resource creation, which are queries, which are present/clear, which are one-offs that want
   a named wrapper method rather than a general one.
2. Add the wrapper methods each category needs. Keep them named after what the engine is asking
   for, not after the D3D call underneath — that naming is what lets D2 put a different backend
   behind them.
3. Move the call sites over, in batches small enough to bisect. 60 sites is several pull requests,
   not one.
4. `Enable_Reports` in `dx11runtime.h` "says how far that has got on any given run rather than
   leaving it to be guessed". Use it. It is the progress meter this task already has.

## Done when

- No `_Get_D3D_Device()->` or `_Get_D3D()->` outside `dx8wrapper.cpp`.
- `-dx11` runs with the backend reached by everything, and `Enable_Reports` says so.
- The existing pixel comparison is the acceptance test: same frame of the same match, before and
  after, pixels counted. `README.md` describes this as how a graphics change is argued in this
  project, and this is a graphics change.
- Frame cost recorded before and after on the Inferno Cannon scene, against a freshly measured
  baseline rather than the README's. More of the engine reaching the backend built for it should not
  cost frame time; if it does, that is a finding worth more than the funnel.
- `ctest` green, including the `dx11*` suites and `dx9_smoke`.

## Do not

- Do not start D2 inside this task. Funnel first, abstract second; doing both at once produces a
  diff nobody can review and a bisect nobody can run.
- Do not change what is drawn. Pixels identical or explained.
