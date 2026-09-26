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

## Also

W3DModuleFactory registers 19 draw modules by name, and object INIs name them. A headless module
factory needs those names too, or INI parsing fails. Coordinate with C1 (f)'s engine subclass.
