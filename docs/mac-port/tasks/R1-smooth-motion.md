# R1: smooth motion

- **Why:** a user on a 2560x1440, 144 Hz Mac reported, 2026-09-27, that "the main menu was really choppy".
- **Owner:** -a9. The PM's decision (a), render-time interpolation, was approved as designed with four
  additions.
- **Decision:** 11 in the README.

## What the game did

- **The logic ticks at 30 Hz, and the renderer is not capped:** `W3DDisplay.cpp` says so. Presents are
  paced by the swapchain's vsync, which is SDL's default mode.
- **But the client moves a model only on the render frame that follows a tick.**
  - `W3DView::update` draws drawables only `if (WW3D::Get_Frame_Time())`.
  - `W3DDisplay::draw` freezes W3D's clock when the client frame has not changed.
  - Particles, `updateDrawable`, tree sway and skeletal animation are gated the same way.
- **So at 144 Hz, about four frames in five re-render the same transforms.** The camera's scripted pans move
  by milliseconds on every frame, so the units step at 30 Hz against a ground that glides.
- **144/30 is 4.8.** Each unit position is shown for 5, 5, 5, 5 and 4 refreshes, which reads as judder. At
  60 Hz it would be an even 2.
- **Transforms are pushed at logic time,** by `Thing::set*` → `Object::reactToTransformChange` → `Drawable`
  → `W3DModelDraw::reactToTransformChange` → `Set_Transform`. They are set again in `doDrawModule` on draw
  frames, with the instance matrix, the physics tilt and bone attachment. Nothing kept a previous transform.

## What it does now

Three steps in `W3DDisplay::draw`, all of them client only:

| step | where | what |
|---|---|---|
| `smoothMotionBegin` | before `updateViews` | Computes the blend's alpha, `GameEngine_logicTickFraction()`: the time since the last logic tick over the tick's length. It is 1 in fast mode and before any tick. On a new tick it records each drawable's position, which the locked camera follows. |
| `smoothMotionApply` | after the particles, before the render targets and the scene | On a new tick, each model module captures the transform its render object holds now, and the tick's verdict is decided and counted. Then every model is shown at the blend of its last two transforms. |
| `smoothMotionRestore` | after the render loop | Every model gets its logic transform back, before picking (the message stream) and the logic run. |

- **The blend** (`W3DSmoothMotion.h`, header-only) lerps translation, slerps rotation as a quaternion of the
  orthonormalised basis, and lerps each column's scale.
- **A model snaps to its logic transform** when:
  - it has no earlier transform (a new drawable);
  - it has a new render object (a model-state change, or a replaced drawable);
  - it was hidden in either tick (containment, parachutes, garrison);
  - more than one tick passed between captures (catch-up);
  - it moved more than 60 world units in the tick;
  - it turned more than 120 degrees in the tick;
  - its matrix is not rotation times scale;
  - or `Drawable::markMotionDiscontinuity()` was called. The AI's dozer teleport calls it.
- **The camera:** with the option on, a camera locked to a unit follows on every render frame, toward the
  unit's blended position. Each per-step factor f becomes 1-(1-f)^(ms/33). With the option off, both are
  exactly as before.
- **The option:** Options.ini `SmoothMotion`, and Options > Display > Picture > Smooth Motion. It defaults
  to on off Windows and off on Windows. Headless runs never blend.

## Proof that it never changes the game

- **The setup:** the seed-1234 generated skirmish, rendered offscreen at 120 Hz presents on finer, to frame
  1800. HEADLESS CRC at the end:
  - SmoothMotion on: 0x3A3442A2. 119,516 of 119,619 model ticks were blended, so the blend really ran.
  - SmoothMotion off (Options.ini): 0x3A3442A2, **the same**. No stats line was written, so the option
    really turned it off.
  - **Armed control:** on, with `ZH_R1_LEAK=1`, which writes the blend into the objects: 0xF174F507,
    **different**. A leak would be seen.
- **E1 and net_check run headless,** where the pass is off by construction.
- **The full suite at the branch's tip** (d8edfe74, finer): 86 of 87, test_smooth_motion and the
  optionsmenu selfcheck included. The one failure is test_ffvertex, inherited from the base; it is fixed by
  feature/mac-port-ffvertex-test, which the PM's gate batch carries.
- **`test_smooth_motion`** checks the blend maths (lerp, the short-way slerp across the ±180 seam, scale,
  both ends of alpha) and every snap verdict.

## Measured

**Snap counts** (`ZH_SMOOTH_MOTION_STATS=1`), one per model per tick:

| run | model ticks | blended | first | new model | hidden | frame gap | distance | rotation | marked |
|---|---|---|---|---|---|---|---|---|---|
| mobstress, 6000 frames | 3,283,981 | 3,283,389 | 566 | 0 | 0 | 0 | 26 | 0 | 0 |
| airbelow (MiG, Comanche, troop crawler), 1500 frames | 193,175 | 180,841 | 139 | 3 | 11,706 | 484 | 0 | 2 | 0 |

- The hidden count is the crawler's passengers on every tick they ride.
- The frame gaps are catch-up passes.
- No common snap reason turned up that the design did not expect.

**Attached things on fast units.** Exhaust, health bars and selection decals stay on the logic ticks in v1.
- In airbelow, an aircraft's logic position led its drawn place by a mean of 0.51 world units and at most
  4.52, over 8,048 samples.
- The largest aircraft's bounding radius is 23.8, so the worst lead is inside the airframe. The PM agreed
  that is not visibly detached, and the (blend − cur) offset stays out of v1.

**Cost.** The blend and restore together cost 149 us a render frame, averaged over 7,108 frames of
mobstress at 1920x1080 on finer. The frame's work p50 there was 7.66 ms, so about 2%, in the heaviest scene
the game has.

**This Mac.** A short hidden-window run, silent, with the install verified unchanged. It measured
present-to-present p50 8.2–9.0 ms (about 116–122 Hz): the hidden window's pacing, which need not be the
visible panel's.
- That is 3.96 presents per 33.3 ms tick, so without R1 almost every tick showed four identical frames.
- The 3D shell map did not load in those runs; they drew the 2D menu. The cause is open, and it is traced
  separately (not `isReallyLowMHz`, which was ruled out).

## Known limits of v1

These stay on the logic ticks:
- particles, including emitters attached to a unit;
- lasers, ropes, projectile streams and tracers;
- health bars, icons and captions;
- skeletal animation, texture flipbooks, treads, wheels, recoil and turret bones;
- terrain tracks.

What v1 does keep together: a rider on its container's bone blends with the same alpha as the container.

v2 candidates, if a player notices:
- advance W3D's clock on every render frame, for smooth animation;
- offset attached effects and health bars by (blend − cur).

## Files

- `GameEngineDevice/Include/W3DDevice/GameClient/W3DSmoothMotion.h` (new): the blend, the verdicts and the
  counters.
- `W3DModelDraw.h/.cpp`: the per-module track, with capture, apply and restore.
- `W3DDisplay.cpp`: the three steps, the stats, and the armed control.
- `W3DView.cpp`: the locked camera.
- `Common/DrawModule.h`: three no-op virtuals.
- `GameClient/Drawable.h/.cpp`: the position history and the discontinuity mark.
- `Common/GameEngine.h/.cpp`: the tick fraction.
- `Common/GlobalData.h/.cpp` and `OptionsCatalog.cpp`: the option.
- `AI/AIPlayer.cpp`: the dozer mark.
- `Data/Window/Menus/OptionsMenu.wnd` (regenerated by `Tools/optionsmenu_layout.py`), `Data/Patch.str` and
  the Turkish table: the check box.
- `Tests/test_smooth_motion.cpp` (new).
