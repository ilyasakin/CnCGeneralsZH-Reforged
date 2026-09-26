# T1 — Simulation terrain out of W3DDevice

- **Milestone:** M2 (a headless game must stand on the same ground as Windows)
- **Depends on:** nothing
- **Blocks:** E1 (no POSIX replay can match a Windows one until this lands), C1 (f)'s engine subclass
- **Status:** done (-47): T1a merged; T1c on its branch; T1b, T1d and the rest of T1a dropped by decision 8
- **Size:** `W3DDevice/GameLogic/W3DTerrainLogic.cpp`, `W3DDevice/GameClient/WorldHeightMap.cpp`,
  `BaseHeightMap.cpp`: the height data and the maths that sample it

## Why

Found by B6 on 2026-09-26. **The simulation's ground height is computed by render code.**
`W3DGameLogic` overrides `createTerrainLogic` to return `W3DTerrainLogic`, whose `getGroundHeight`,
`getLayerHeight` and `isCliffCell` ask `TheTerrainRenderObject`, meaning the renderer's
`BaseHeightMap`/`WorldHeightMap` samples and interpolation. So every unit's height, every cliff test
and every pathfinding query that depends on them runs through W3DDevice.

A POSIX engine that returns the base `GameLogic` would simulate on different ground, and so would
anything headless without a terrain render object. It is the same shape as the well-known keys and
`MapObject`, which B6 already moved (simulation data misfiled in the device layer), only bigger.

## Do

1. Map every simulation-reachable entry point into the terrain render object: getGroundHeight,
   getLayerHeight, isCliffCell, and whatever else `W3DTerrainLogic` and pathfinding call. Find them
   by symbol, from the GameLogic side.
2. Move the height-map DATA and the height/cliff MATHS into gameengine, verbatim, the way B6 moved
   `MapObject`: the render half (textures, vertex buffers, render objects) stays in W3DDevice, and the
   class layout on Windows does not change unless it has to. If it has to, say why.
3. `W3DTerrainLogic`'s simulation methods, or a portable `TerrainLogic` subclass, read the
   gameengine-owned data on every platform.
4. **The proof is a height golden.** Load a real map (read-only, rule 9: never with the install as the
   engine root) and sample getGroundHeight, getLayerHeight and isCliffCell over a grid. Compare three
   builds: the ORIGINAL W3D code compiled with mingw-w64 and run under Wine (the Windows oracle, as
   C1's fs_oracle does); the moved code on macOS arm64; and the moved code on x86_64 under Rosetta
   (E3's axis). All three must be bit-identical. State the Wine caveat.
5. A `WINDOWS-DEBT.md` row, high: the simulation's terrain changes library on Windows.

## Recon, 2026-09-26 (-47): what the simulation depends on in W3DDevice

All of it is reached through `TheTerrainLogic`/`TheTerrainVisual` virtuals, which is why B6's link
census could not see it.

- **A. Height data.** One `WorldHeightMap`, `W3DTerrainVisual::m_logicHeightMap`, loaded with the map;
  `DO_SEISMIC_SIMULATIONS` is off, so it is also the render object's `m_map`. The simulation reads
  its `m_width`, `m_height`, `m_borderSize`, `m_boundaries`, `m_data` and `m_cellCliffState`. The cliff
  bits come from the `BlendTileData` chunk (version 7 has its own odd byte width), or from
  `initCliffFlagsFromHeights` for maps older than 7, and are read before the chunk's textures.
- **B. Height maths.** `BaseHeightMapRenderObjClass` members, self-contained arithmetic over (A):
  `getHeightMapHeight` (two triangles, 12-sample smoothed normal), `getClipHeight` (edge clamp, reads
  `m_map`), `isCliffCell`, `isClearLineOfSight` (Bresenham, which stops at the render object's
  `m_maxHeight`, set by `initHeightData`'s min/max pass), `getMaxCellHeight`. GameLogic reaches them
  129 times through `getGroundHeight`, 38 through `getLayerHeight`, and through `isCliffCell`,
  `isClearLineOfSight` and `getExtentIncludingBorder`.
- **C. Height writes.** Craters and flattening: `TheTerrainVisual->setRawMapHeight`, 20 sites, which
  only lowers, writes (A) and tells the render object; `getRawMapHeight`, 1.
- **D. Bridges.** `W3DTerrainLogic::newMap` -> `TheTerrainRenderObject->loadRoadsAndBridges` ->
  `W3DBridgeBuffer::loadBridges`. Each bridge's end heights come from (B); then `W3DBridge::load`
  **loads the bridge's `.w3d` meshes through WW3D2's asset manager**, and `getBridgeInfo` derives the
  bridge's logic **width and four corners from the mesh vertices' min/max Y times the scale**. That is
  what `TerrainLogic::addBridgeToLogic` gets - pathfinding, layers, `getLayerHeight`. **Bridge geometry
  in the simulation depends on parsing render assets.** Decided: T1b reads the meshes with a portable
  `.w3d` mesh-extent reader, not precomputed extents, so custom maps and mods keep working.
- **E. Water grid.** `TerrainLogic::isUnderwater` reads `TheTerrainVisual->getWaterGridHeight` on maps
  that enable the grid (a `WaveGuide1` waypoint). The grid steps on the client pass; the fork's logic
  catch-up breaks EA's one-step-per-logic-frame. Defect 17.
- **F. Saves.** `W3DTerrainVisual::xfer` version 2 writes the heights.

**With no terrain render object** (answered 2026-09-26): `getGroundHeight` and `getLayerHeight` return
0 for every layer, `isCliffCell` dereferences NULL. Windows never gets there: `-headless` keeps the
device and the terrain visual, and `-nodevice` still creates the terrain visual.

**Phases, decided:** T1a (A, B, C) with the height golden; T1c's fix (E, step the grid per logic frame);
T1b (D, the mesh-extent reader); T1d (F). **Changed by decision 8 (README, 2026-09-26); see "Scope after
decision 8" below.**

## The oracle, 2026-09-26: the original code, three ways, one answer

`Tools/terrain_oracle_extract.py` takes the original text - `ParseHeightMapData`, `ParseBlendTileData`
cut where the textures start, the cliff functions, `getHeight`, `getHeightMapHeight`, `getClipHeight`,
`isCliffCell`, `isClearLineOfSight`, `getMaxCellHeight`, and `initHeightData`'s min/max block - out of
`009795ce` with `git show`, and `Tests/terrain_oracle.cpp` wraps it in stand-ins holding only what
it touches, with its own `CkMp` chunk walker. It samples a grid 7.25 world units apart, from 60 units
outside the map to 60 past its far edge: height with normal, height alone, cliff, max cell height,
and lines of sight both ways from every fifth point. Nine maps cover blend-tile versions 8, 7 and 6,
square and not, one with four boundaries, 160 to 710 cells a side.

**Built three ways, identical output on all nine maps:** mingw-w64 with MSVC's branches, run under
Wine (CrossOver, a private bottle); native arm64; x86_64 under Rosetta. The golden is
`Tests/terrain_golden.txt`. What it cannot say: Wine ran GCC's x86-64 code, not MSVC's, so this is the
original C++ under GCC's code generation (single precision, no contraction, no FMA); nothing here
has run on Windows.

## T1a's move and its golden, 2026-09-26

- `GameLogic/WorldHeightMapData.{h,cpp}`: the height map's data half - its members in their original
  order, and 17 of `WorldHeightMap`'s functions verbatim (`ParseHeightMapData`, `ParseSizeOnly`,
  `ParseObjectData`, `ParseWorldDictDataChunk`, the cliff, flip and seismic-flag functions,
  `freeListOfMapObjects`), plus `parseBlendTileCells`, the first half of `ParseBlendTileData`.
  `WorldHeightMap` derives from it after `RefCountClass` and `WorldHeightMapInterfaceClass`.
  `parseHeightsAndCells` and `parseLogicalMap` load a map into it alone, with callbacks of its own
  (the `void*` DataChunkInput hands back must be cast to the type that was passed).
- `GameLogic/TerrainHeightSampling.{h,cpp}`: `getHeightMapHeight`, `isClearLineOfSight`,
  `getMaxCellHeight`, `isCliffCell`, `getClipHeight` and `initHeightData`'s min/max pass, verbatim but
  for `m_map` and `getMaxHeight()` becoming parameters. The render object calls them.
- **Verbatim check** (a script against the pre-move commit, in the scratch record): every moved
  function identical, after the listed substitutions. It caught a `getMaxHeight()` left in
  `isClearLineOfSight`'s inactive `#else` branch.
- **`test_terrain_golden`** (ctest, needs `ZH_GAME_DATA`): the nine maps out of the install, through the
  engine's own decompression and `DataChunkInput`, parsed by `parseHeightsAndCells` and sampled by
  `TerrainHeightSampling` over the oracle's grid (`Tests/terrain_grid.h`, shared, so the two cannot
  drift). **All nine identical to the golden, on arm64 and on x86_64 under Rosetta.** Red once: with the
  triangle test changed from `fy > fx` to `fy >= fx`, seven of the nine maps fail.
- The x86_64 leg found a B1 bug on the way: the wide formatter did not compile for x86_64 (a `va_list`
  parameter bound to `va_list&`). Fixed separately.
- Windows: `WINDOWS-DEBT.md`'s T1a row.

## Scope after decision 8, 2026-09-26

Decision 8 (README): the POSIX engine uses the same W3D factories Windows' `-headless` uses, because
decision 7's A1 builds W3DDevice on POSIX. So on POSIX, as on Windows, `W3DTerrainLogic` runs over a
real `HeightMapRenderObjClass` - now forwarding to `TerrainHeightSampling` - and `W3DBridgeBuffer` loads
the bridge meshes through WW3D2 as Windows does. Parity comes from running Windows' own classes, so:

- **Dropped:** the rest of T1a (a portable terrain logic owning a `WorldHeightMapData`, and a headless
  `setRawMapHeight`/`getRawMapHeight`); T1b (the portable `.w3d` mesh-extent reader); T1d (saves).
- **Kept:** T1a as merged. The extraction and `test_terrain_golden` are what show the simulation's
  heights are the same on every platform, whichever class asks.
- **Kept:** T1c's fix, because defect 17 is a bug in the shipping Windows game.

## T1c: the water grid steps once per logic frame, 2026-09-26

Defect 17. The grid's mesh motion ran in `WaterRenderObjClass::update`, on the client pass, gated on the
logic frame having changed: one step a pass. EA's loop ran one pass per logic frame; this fork's
catch-up runs several, and a machine that caught up k frames stepped the grid once.

- **Where it runs now.** At the top of `GameLogic::update`, after `setFPMode`, through a new pure virtual
  `TerrainVisual::updateWaterGrid(frame)` (W3DTerrainVisual forwards to the render object's
  `updateMeshMotion`). EA's order within a pass was: the client pass (which stepped the grid if the frame
  had changed), then the logic frame. Nothing between the two changes the frame or writes the grid, so
  the top of the logic update is the same point in the frame's sequence: before `startNewGame`, before
  the scripts, before `WaveGuideUpdate` pushes velocity and before anything reads `isUnderwater`. It was
  chosen over `W3DTerrainLogic::update`, which runs after the scripts, and over a catch-up loop in the
  render pass. EA's frame gate is kept, so a frozen or held frame (the camera freeze returns before
  `m_frame++`) still steps once.
- **What moved.** The step, verbatim with its gate (a script in the scratch record: identical after the listed
  substitutions), is `WaterGridMotion::updateForLogicFrame` in gameengine (`GameLogic/WaterGridMotion.h`).
  `WaterRenderObjClass::WaterMeshData` is a typedef of its `MeshPoint`: the same members in the same order.
  It moved so that the step can be tested on a machine where W3DDevice does not build yet.
- **The oracle.** `Tools/water_grid_oracle_extract.py` takes the original block and the mesh point type
  out of `7b209198`; `Tests/water_grid_oracle.cpp` steps them on the client pass as the engine did, over
  `Tests/water_grid_scenario.h` (a 12 x 9 grid, a push every third frame of the first 120 as
  `WaveGuideUpdate` would push, then left to settle; GameData.ini's gravity). Built with mingw-w64 and run
  under Wine, native arm64, and x86_64 under Rosetta: **identical output**. With one pass per logic frame
  (EA's loop) the grid is in motion on 183 of 240 frames and settles. **Armed control:** the original under
  passes of 3, of 5, and a mixed schedule (1,4,2,1,6,3,1,1,5,2) gives three different results, each
  still in motion at frame 240.
- **`test_water_grid`** (ctest): the moved step, called at the top of each logic frame, under all four
  schedules, **prints EA's-loop line every time**, and a second call for the same frame does not step
  again. Red twice: with the gate removed, the double-call test fails; with the damping moved by one ulp
  (0.93f to 0.9300001f), six checks fail.
- **Windows.** The mingw-as-MSVC sweep over every Windows-side file: 0 new errors, 0 gone, 688 to 689
  compiling clean (`WaterGridMotion.cpp`); `W3DWater.cpp` and `W3DTerrainVisual.cpp` reach their ends
  (no fatal error). `WINDOWS-DEBT.md`'s T1c row is **high**: on a water-grid map, grid heights change
  under the catch-up, back to EA's count.
- **What it cannot see.** That `GameLogic::update` makes the call at its top is read in the source, not
  run: the engine loop needs W3DDevice. A client pass now sees the grid as of the previous logic frame's
  step, one step behind EA within a pass (drawing only). A `_DEBUG`/`_INTERNAL` `-jumpToFrame N` run,
  whose client pass skipped the terrain visual's update until frame N and so never stepped the grid
  before it, now steps it every frame. Nothing has run on Windows; no replay of CHI03, GLA01 or USA06 was
  played.

## Also

W3DModuleFactory registers 19 draw modules by name, and object INIs name them. A headless module
factory needs those names too, or INI parsing fails. Coordinate with C1 (f)'s engine subclass.
(Superseded by decision 8: there is no headless module factory; W3DModuleFactory registers its own.)
