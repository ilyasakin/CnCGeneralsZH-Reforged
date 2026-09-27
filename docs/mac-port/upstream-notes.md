# Notes for upstream

Fixes found in upstream's own code or data while integrating it into the port. Each note is written so
it can be offered upstream as it stands: the defect, the cause, the patch and how it was checked. Whether
and when to offer one is the user's decision. Nothing here has been sent anywhere.

## Twelve units lose their locomotors to a ReplaceModule, and the Chinooks crash (port defect #33)

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
