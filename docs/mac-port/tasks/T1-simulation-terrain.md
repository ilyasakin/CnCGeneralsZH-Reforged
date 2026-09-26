# T1 — Simulation terrain out of W3DDevice

- **Milestone:** M2 (a headless game must stand on the same ground as Windows)
- **Depends on:** nothing
- **Blocks:** E1 (no POSIX replay can match a Windows one until this lands), C1 (f)'s engine subclass
- **Status:** claimed (-47)
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
T1b (D, the mesh-extent reader); T1d (F).

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

**Still to do in T1a:** the portable terrain logic that owns a `WorldHeightMapData` and answers
`getGroundHeight`, `getLayerHeight`, `isCliffCell`, `isClearLineOfSight` and the extents through
`TerrainHeightSampling`, for C1 (f)'s engine; and a headless `setRawMapHeight`/`getRawMapHeight`.

## Also

W3DModuleFactory registers 19 draw modules by name, and object INIs name them. A headless module
factory needs those names too, or INI parsing fails. Coordinate with C1 (f)'s engine subclass.
