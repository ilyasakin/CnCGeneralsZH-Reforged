# B9 — Backslash include paths

- **Milestone:** M1
- **Depends on:** nothing (A1 is done; this needs no build)
- **Blocks:** B1, and everything else that compiles `GameLogic`
- **Status:** in review
- **Size:** **147 directives across 15 files**, measured 2026-09-22

## Why

Created 2026-09-22 from A1's findings. No task owned this and it is a hard error on the first
compiler that sees it.

`#include "Common\GameAudio.h"` — a DOS path separator inside an include directive. MSVC accepts
it; clang does not, and cannot, because `\G` is an undefined escape sequence in a header name.
Every one is a build-stopper.

They cluster hard. `GameEngine/Source/GameLogic/Object/Update/` holds the worst of it:

| Count | File |
|--:|:--|
| 27 | `SpectreGunshipUpdate.cpp` |
| 26 | `SpectreGunshipDeploymentUpdate.cpp` |
| 23 | `ParticleUplinkCannonUpdate.cpp` |
| 14 | `AutoFindHealingUpdate.cpp` |
| 13 | `CleanupHazardUpdate.cpp` |
| 12 | `DemoTrapUpdate.cpp` |

A1 fixed exactly one — `WWDebug/wwdebug.h`, because it was the first error out of the compiler and
`wwdebug` sits on A1's critical path. The rest are untouched.

## Scope

All of `GameEngine`, `GameEngineDevice`, `Libraries/Source/WWVegas`. Count them yourself first;
the number above is from one measurement and other agents are committing in parallel.

## Do

1. Backslash to forward slash inside `#include "..."` directives. That is the entire change.
2. **Do not touch any other backslash in the tree.** `.big` archive-internal paths use them and
   they are part of the on-disk format; B3 was told the same thing. String literals in general are
   out of scope here. Only include directives.
3. A script can do this, and should — 147 near-identical edits is not a place for judgement. But
   read the diff, because the same files contain string literals with backslashes that must not
   change, and a careless regex will catch those too.
4. Check for the mixed form (`"Common/Foo\Bar.h"`) and for any include whose path contains a
   backslash inside a *comment* on the same line.

## Done when

No `#include "..."` in scope contains a backslash, and the same set of headers resolves — verify by
configuring and building whatever currently builds, which after A1 is `benchmark` plus the Python
selfchecks. The real proof comes when B3 and B5 get `wwdebug` compiling.

**This is a no-op on Windows by construction.** MSVC resolves both spellings to the same file, so
the preprocessor sees identical content and produces an identical object. Say so in the pull
request and add one `WINDOWS-DEBT.md` row for the whole change rather than per file.

## Do not

- Do not reformat, reorder or dedupe includes while you are here. A 147-line mechanical diff is
  reviewable; a 147-line diff with tidying mixed in is not, and this one needs to land fast because
  B1 is behind it.
