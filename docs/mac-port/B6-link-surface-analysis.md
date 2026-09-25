# B6 prep — what `gameengine` actually links, and why

The analysis half of [B6](tasks/B6-gameengine-link-surface.md), which does not need B1–B5. For each
of the 17 entries in `target_link_libraries(gameengine PUBLIC ...)`: is it a real dependency or a
link-time accident, and what would a stub have to satisfy.

Measured against `b75ddbe3` on arm64 macOS. Everything below was checked rather than read off a
comment — twice today a plausible source comment turned out to be wrong when someone finally ran
the query, so the rule here is that a claim without a command behind it is not in this document.

## Two findings that change B6's plan

> ### 1. `gamespy` does not need stubbing. It builds.
>
> Item 4 says *"try building it for real on macOS before stubbing it."* Done:
>
> ```
> cmake -S Libraries/Source/GameSpy -B build-gs -DCMAKE_BUILD_TYPE=Release   # configures
> cmake --build build-gs -j4                                                 # 0 errors, 9 warnings
> ```
>
> Produces `libgamespy.a`, **arm64, 102 objects, 1488 defined symbols**, with the real entry points
> in it (`qr2_initA`, `qr2_create_socket`, …). It found pthreads and needed no flags beyond the
> defaults. The one item on B6's list that might simply work, does.
>
> **Delete "stub gamespy" from the plan.** The remaining question is whether the *game's* GameSpy
> call sites compile, which is a different and smaller problem than porting the SDK.

> ### 2. `ww3d2` is a real link dependency, it is GameLogic-reachable, and it is a determinism
> ### problem. B6 item 1's "the fix is one line" is wrong.
>
> The comment at `CMakeLists.txt:645` says WW3D2 is on the include path "for `d3dx9math.h` alone",
> and item 1 says if so, `gameengine` needs the directory and not the library. The first half is
> true — **GameEngine includes zero WW3D2 headers** other than `d3dx9math.h`, which I verified by
> intersecting all 146 WW3D2 header names against every `#include` in `GameEngine`. The conclusion
> drawn from it is not.
>
> `d3dx9math.h` is not header-only. It declares:
>
> ```cpp
> extern D3DXVec4TransformFunction	D3DXVec4Transform;     // :127
> ```
>
> a **function pointer**, defined in `WW3D2/d3dx9runtime.cpp:41` and filled in at :133 with
> `GetProcAddress(D3DX9Module, "D3DXVec4Transform")` out of `d3dx9_43.dll`. So `gameengine` needs
> `ww3d2` for a symbol, and needs a Windows DLL at runtime to make it non-null.
>
> `GameEngine` calls it four times — `BezierSegment.cpp:112`, `BezFwdIterator.cpp:69-71` — and
> `DumbProjectileBehavior.cpp` is the **only** consumer of `BezierSegment` in the tree. That file
> is `GameEngine/Source/GameLogic/Object/Behavior/`. The header says so itself, at `d3dx9math.h:28`:
>
> > *"Binding rather than reimplementing is deliberate. D3DXVec4Transform and D3DXVec4Dot reach
> > GameLogic through BezierSegment, which DumbProjectileBehavior steers a shell with, so their
> > arithmetic is part of the network and replay CRC. A hand-written 4x4 inverse or transform would
> > be a rounding difference nobody could see until a replay diverged."*
>
> I checked that claim rather than relaying it, because the last comment I relayed in this codebase
> was false. **This one holds.** `DumbProjectileBehavior.cpp:453` constructs a `BezierSegment` for
> the shell's flight curve, and it is the sole caller.
>
> So B6 must reimplement, on macOS, a function that is on the replay-CRC path and whose author
> deliberately refused to reimplement it. That is the single hardest thing left in M1 and it is not
> a one-line include change. It also belongs to E1 as much as to B6.

### Why it is nonetheless tractable

Three things make this much better than the comment fears, and they should go in B6's plan:

1. **The matrix is exact.** `BezierSegment::s_bezBasisMatrix` (`BezierSegment.cpp:241`) is the
   standard Bézier basis and every entry is a small integer: `-1 3 -3 1 / 3 -6 3 0 / -3 3 0 0 /
   1 0 0 0`. No entry is a value whose float representation is approximate.
2. **The semantics are pinned by an existing test.** `test_dx9_smoke.cpp:374-395` already runs this
   exact basis matrix through the real DLL and asserts the result, with the comment *"Each output
   component is the sum of one column"* — so the operation is confirmed row-vector × matrix,
   `out.x = v.x*m[0][0] + v.y*m[1][0] + v.z*m[2][0] + v.w*m[3][0]`, not the transpose. That test is
   also a ready-made golden-reference generator the moment a Windows machine exists.
3. **The sibling is already reimplemented.** `D3DXVec4Dot` is written out inline at
   `d3dx9math.h:152`, *"copied from d3dx8math.inl so the arithmetic that reaches a replay CRC is
   the arithmetic that always reached it."* That is the precedent and the method: copy the SDK's
   term order rather than invent one.

So the risk is summation order and FMA contraction, both of which are controllable — and
`-ffp-contract=off` is already mandatory per the README. **What B6 must not do is write the
transform in whatever order reads nicely.** Write it in the SDK's order, and have E1 diff it
against a Windows capture before M2 relies on it.

A second, smaller blocker in the same file: `d3dx9math.h:39` is `#include <d3d9.h>`, so
`BezierSegment.h` currently pulls the Direct3D 9 header into GameLogic. That has to go behind the
same split.

## The full inventory

| Entry | Real or accident? | Evidence | What B6 does |
|:--|:--|:--|:--|
| `ww3d2` | **real, and the hard one** | one symbol, `D3DXVec4Transform`, GameLogic-reachable | reimplement in SDK term order; see above |
| `wwlib` `wwmath` `wwsaveload` `wwdebug` `wwutil` | real | the engine's own libraries | port, not stub — B3/B5 |
| `compression` | real | `.big`/save codecs | port; currently blocked on `BaseType.h:137` |
| `gamespy` | **real and already portable** | builds clean, see above | link it, do not stub it |
| `eabrowserdispatch` | real, stubbable | 9 GameEngine files reference `WebBrowser` | null impl in `Stubs/` |
| `wwdownload` | real, stubbable | 11 files reference the download path | null impl in `Stubs/` |
| `benchmark` | real | 7 files; `GameLOD.cpp` sizes detail off it | **already builds on macOS** — keep |
| `profile` | real, thin | 2 files: `Shell.cpp`, `GameLogic.cpp` | keep — **but not portable as it stands** (2026-09-25): it is built on `debug` (`Debug::AddCommands`, `Debug::Command`, `DFAIL`/`DASSERT` throughout), `profile.cpp:31` includes `<mmsystem.h>`, and both targets are defined only inside `if(ZH_PLATFORM_WINDOWS)`. Keeping it on macOS means porting `debug` too, or stubbing both |
| `debuglib` | **accident** | **zero** live GameEngine calls. (`Debug_`/`debug.h` was the wrong search — the library's API is `Debug::`, `DFAIL`, `DASSERT`, `DLOG`, `DCRASH`. Re-run with those on 2026-09-25: 4 hits in GameEngine/GameEngineDevice, all commented out. Same conclusion.) | **dropped from the macOS line** (B6 drops). Still arrives through `profile` wherever `profile` is linked |
| `dinput8` | accident **for GameEngine**, load-bearing on Windows | 107 `DIK_` constants from `<dinput.h>`; **zero** calls to `DirectInput8Create`, `IDirectInput8` or `IDirectInputDevice` in GameEngine. But `gameenginedevice`'s `Win32DIKeyboard.cpp:120` and `Win32DIMouse.cpp:49` call `DirectInput8Create` and get `dinput8` only through `gameengine`'s PUBLIC line | **dropped from the macOS line only** (B6 drops). On Windows it must stay, or move to `gameenginedevice`. GameEngine still needs a key-code table for `KeyDefs.h` |
| `wininet` | real, Windows-only | `ChromaKeyboard.cpp` | Windows branch |
| `imagehlp` | real, Windows-only | `StackDump.cpp` — C5's | Windows branch |
| `imm32` | real, Windows-only | `IMEManager.cpp` | Windows branch |

### The stubs, and what each must satisfy

`Stubs/` already holds `NullAudioManager.h`, so the pattern exists.

- **`WebBrowser`** — `GameNetwork/WOLBrowser/WebBrowser.h:79`. Two pure virtuals:
  `createBrowserWindow(char *tag, GameWindow *win)` and `closeBrowserWindow(GameWindow *win)`, plus
  a virtual destructor. It is a `SubsystemInterface`, so `init`/`reset`/`update` come with it. A
  three-method null class. **Not GameLogic-reachable** — it is reached from `GameEngine.cpp` and
  `INIWebpageURL.cpp`, i.e. the shell and INI loading.
- **`DownloadManager`** — `GameNetwork/DownloadManager.h`, ~20 members. Patch downloading, which
  the launcher does now. **Not GameLogic-reachable.**

Both satisfy B6's "Do not stub anything `GameLogic` calls", and I checked that rather than assuming
it: neither appears anywhere under `GameEngine/Source/GameLogic`.

`D3DXVec4Transform` is the one thing on this list that **is** GameLogic-reachable, which is exactly
why it must be reimplemented rather than stubbed. A stub there would be the "Mac build is playing a
different game" failure the task warns about.

## What this does not cover

- **`gameengine` itself still does not compile**, so none of this is proved by a successful link.
  It is blocked on `wwdebug.cpp:46` `<windows.h>` and `BaseType.h:137` `__int64` — see
  [`TEST-VISIBILITY-AUDIT.md`](TEST-VISIBILITY-AUDIT.md), where the same two lines block eight
  tests. Every "real or accident" verdict above is from source analysis and from building the
  libraries that do build, not from watching a link succeed.
- **Whether the game's own GameSpy call sites compile.** The SDK builds; `GameNetwork/GameSpy/*`
  in GameEngine is untested and is the part that still might not.
- **Windows.** Nothing here has been near MSVC, and no change is proposed in this document — it is
  analysis only, so there is no `WINDOWS-DEBT.md` row.
