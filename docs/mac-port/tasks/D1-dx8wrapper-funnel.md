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
