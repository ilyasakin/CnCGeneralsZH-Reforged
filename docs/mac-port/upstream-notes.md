# Notes for upstream

Defects found while porting, in upstream's own code or data (the Reforged project, olcayseygan/CnCGeneralsZH-Reforged)
or in the libraries the port vendors. Each note is written so it can be offered or reported upstream as it
stands: the defect, the cause, the patch or repro, and how it was checked, saying what was measured and what is only
reasoning. Whether and when to send any of them is the user's decision. Nothing here has been sent anywhere.

## The Reforged project

### Twelve units lose their locomotors to a ReplaceModule, and the Chinooks crash (port defect #33)

**Introduced by:** fbe8dc6f "fix(data): second pass over the nine generals, 54 more data bugs" and
179e1f65 "fix(data): patch at least three data bugs in each of the nine generals", in
`GeneralsMD/Code/Data/INI/FixesReforged.ini`.

**Symptom:** the game crashes (a NULL read) as soon as an Air Force, Laser or Superweapon general's Supply
Center makes its Chinook. That happens in a skirmish or a LAN match, on every platform, since the cause is
data. In one sample match on Golden Oasis (seed 3, two AIs), it happened 56–66 s in, depending on the
machine. Nine more units lose theirs the same way and can't move.

**Cause:** in Zero Hour an object's `Locomotor = SET_...` lines, although written at object level, are
stored in its AI module's data. ThingTemplate.cpp:245 parses the field with
`AIUpdateModuleData::parseLocomotorSet`, which writes into `friend_getAIModuleInfo()`. A `ReplaceModule` of
that module builds fresh module data, so the unit is left with no locomotor sets unless the block re-states
them. The Chinook then crashes in `ChinookAIUpdate::isAllowedToAdjustDestination`
(ChinookAIUpdate.cpp:1026), which calls `getCurLocomotor()->isInvalidPositionAllowed()` without a check.

**The twelve units, and the lines each block needs.** They are copied from each object's own file in
INIZH.big; PatchINI.big doesn't touch any of them.

| object | lines to add inside the object's block, after its ReplaceModule |
|:--|:--|
| AirF_AmericaVehicleChinook, Lazr_AmericaVehicleChinook, SupW_AmericaVehicleChinook | `Locomotor = SET_NORMAL ChinookLocomotor` and `Locomotor = SET_TAXIING BasicHelicopterTaxiLocomotor` |
| AirF_AmericaVehicleHumvee, Lazr_AmericaVehicleHumvee, SupW_AmericaVehicleHumvee | `Locomotor = SET_NORMAL HumveeLocomotor` |
| Tank_ChinaTankECM, Infa_ChinaTankECM, Nuke_ChinaTankECM | `Locomotor = SET_NORMAL GattlingTankLocomotor` |
| ChinaVehicleNukeLauncher, Infa_ChinaVehicleNukeLauncher, Nuke_ChinaVehicleNukeLauncher | `Locomotor = SET_NORMAL ChinaNukeCannonLocomotor` |

For example:

```ini
Object Lazr_AmericaVehicleChinook
  ReplaceModule ModuleTag_07
    Behavior = ChinookAIUpdate ModuleTag_07_Fix
      ...
    End
  End
  Locomotor = SET_NORMAL    ChinookLocomotor
  Locomotor = SET_TAXIING   BasicHelicopterTaxiLocomotor
  UpgradeCameo1 = Upgrade_AmericaSupplyLines
End
```

The port's patch is in its #33 commit on FixesReforged.ini. It also adds a paragraph to the file's
header: a ReplaceModule of an AI module must re-state the object's Locomotor lines.

**How it was checked (in the port, on macOS and Linux):**
- The crash reproduces with upstream's data: a two-copy LAN match, seed 3, whose AI played the Laser
  General. With the lines added, both copies reach the same world CRC at frame 1800, and both replays play
  back to it.
- A load-time check found all twelve. After every INI has loaded, it lists each thing whose AI module was
  replaced and discarded locomotor sets, and that still has no SET_NORMAL. It named exactly these twelve on
  upstream's data and none after the fix. Upstream could use the same check: the parse side is
  `ThingTemplate::parseReplaceModule` counting the old module's sets, and the report is in
  `ThingFactory::postProcessLoad`.
- EA's own data has 118 things with an AI module and no SET_NORMAL locomotor (structures, turrets riding
  other units, bombs, trains). A check has to look for a *discarded* set, not a missing one.

**Not checked:** Windows itself, though the cause is data read by the same code on every platform. The
load-time check covers every INI file the game loads, upstream's other override files included. It doesn't
cover a map's own map.ini, which loads with the map.

## SDL

### An animated cursor on a video driver without native animated cursors crashes `SDL_QuitMouse`

- **Found:** 2026-09-27 by -a9, on thinkerer: Arch Linux, x86_64, and SDL's `offscreen` video driver
  with no display.
- **Versions:** SDL 3.4.16 (commit fa2c02bb6e21974a89ea9824bc53c9932abe5f9c, vendored here). SDL `main` as
  of 2026-09-27 (3.5.0) has the same code in both places named below; that is read, not run.

**What happens.** The process gets SIGSEGV at exit, in `SDL_QuitMouse` (`src/events/SDL_mouse.c`, the
`next = cursor->next;` line of the loop that destroys `mouse->cursors`), reached from
`SDL_QuitSubSystem(SDL_INIT_VIDEO)`. It needs these conditions:
- the video driver has no `CreateAnimatedCursor`. Both the offscreen and the dummy driver lack it, and lack
  `CreateCursor` too, in which case `SDL_CreateColorCursor` makes a generic cursor and still links it in;
- the application made an animated cursor with `SDL_CreateAnimatedCursor` and more than one frame;
- the application did not destroy that cursor itself before quitting video.

The game hit it with the offscreen driver on Linux. Its cursors are Windows `.ani` files, made with
`SDL_CreateAnimatedCursor` and never destroyed, because SDL frees what is left at quit. The same runs with
the dummy driver on macOS exited cleanly. The code path is the same, so that is most likely the same
use-after-free reading freed memory that macOS's allocator had left intact, where glibc had already
reused it. That is inferred, not measured.

**Why (reading the code).** Without a driver `CreateAnimatedCursor`, `SDL_CreateAnimatedCursor` calls
`SDL_CreateCursorAnimation`, which makes one cursor per frame with `SDL_CreateColorCursor`.
`SDL_CreateColorCursor` links each frame into `mouse->cursors`. Then the parent cursor is created and
linked in as well, at the head. So the list reads: the parent, its frames, and then everything older.
`SDL_QuitMouse` walks that list:

```c
cursor = mouse->cursors;
while (cursor) {
    next = cursor->next;          // the parent's next is its own last frame
    SDL_DestroyCursor(cursor);    // destroys the parent, and through SDL_DestroyCursorAnimation its frames
    cursor = next;                // a frame that has just been freed
}
```

Destroying the parent destroys its frames, which are still on the list, including the one `next` points
to. The loop then reads freed memory. `SDL_DestroyCursor` on the parent unlinks each frame properly when an
application calls it itself, so only the quit path is affected.

**A minimal reproduction (the shape of it; not run as a separate program).**
1. `SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen")`, then `SDL_Init(SDL_INIT_VIDEO)`.
2. Make two 32x32 ARGB8888 surfaces, and `SDL_CreateAnimatedCursor` with both frames (duration 100 each).
3. `SDL_QuitSubSystem(SDL_INIT_VIDEO)` without destroying the cursor.

Expected: a clean quit. Observed, in the game with these steps: SIGSEGV in `SDL_QuitMouse`.

**Our workaround** (feature/mac-port-l2, 0412e14b). `SdlMouse_releaseCursors` destroys every cursor the
game made before SDL's video quits. With that, the thinkerer run exits cleanly. No SDL patch is carried.

**Possible upstream fixes**, for whoever files it:
- keep animation frames off `mouse->cursors`, for example by creating them through the driver's
  `CreateCursor` directly;
- or have `SDL_QuitMouse` destroy the cursors that own animations first;
- or restart the walk from `mouse->cursors` after each destroy.
