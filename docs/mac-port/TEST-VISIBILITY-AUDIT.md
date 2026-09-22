# Test visibility audit

Every `add_test` / `add_lib_test` in `GeneralsMD/Code/CMakeLists.txt`, which platform it actually
exists on, and whether `ctest` lists it there.

Commissioned after B7 shipped with its target inside `if(ZH_PLATFORM_WINDOWS)`: the test existed on
Windows, silently did not exist on macOS, and `ctest` never mentioned it. **A test that is absent
from `ctest` reads exactly like one that passed.** A1 added the platform guards at speed to unblock
five agents, so the question was whether B7 was the only one on the wrong side of one.

Measured against `b75ddbe3` by configuring on arm64 macOS and comparing `ctest -N` against the
declarations, then building every target individually. Nothing here is read off the CMake source
alone.

## The answer to the question that was asked

**B7 was the only mis-guarded target.** The other 40 declarations are all on the correct side of
their guard, and I verified that rather than reading the comments: for each of the 22 tests behind
`if(ZH_PLATFORM_WINDOWS)`, at least one library it links does not exist as a target in a macOS
configuration. The guards are driven by real absences, not by caution.

**But `ctest` green on macOS was still not what it looked like**, for a different reason, and that
is fixed here.

## What `ctest` said before, and what was actually true

| | before | after |
|:--|--:|--:|
| tests `ctest -N` listed on macOS | 11 | **16** |
| tests that actually ran and passed | 8 | 8 |
| tests silently omitted, no mention anywhere | **5** | 0 |
| tests reported as deliberately deferred | 0 | 5 |

The five were `test_wwmath`, `test_wwlib`, `test_wwsaveload`, `test_wwutil` and `test_gameengine`.
A1's `PENDING_MACOS` keyword built them `EXCLUDE_FROM_ALL` and then **skipped `add_test` entirely**,
so nothing in the build or the test output said they existed. Windows lists 16; macOS listed 11 and
said nothing about the difference.

That is the same defect as B7's, one layer up: deliberate rather than accidental, but equally
invisible, and invisible is the part that matters when "ctest is green" is being taken as evidence
of progress.

### The fix

`add_lib_test` now always calls `add_test`, and marks the deferred ones `DISABLED` instead of
dropping them:

```cmake
  add_test(NAME ${name} COMMAND ${name})
  if(NOT exclude STREQUAL "")
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    set_property(GLOBAL APPEND PROPERTY ZH_DEFERRED_TESTS ${name})
  endif()
```

`ctest` now ends with:

```
The following tests did not run:
	  1 - test_wwmath (Disabled)
	  2 - test_wwlib (Disabled)
	  3 - test_wwsaveload (Disabled)
	  4 - test_wwutil (Disabled)
	  7 - test_gameengine (Disabled)
```

and `cmake` says it at configure time too, so it is visible before anyone gets as far as running
the suite:

```
-- ctest: 5 test(s) registered DISABLED on this platform - test_wwmath, test_wwlib,
   test_wwsaveload, test_wwutil, test_gameengine
-- ctest: they are built EXCLUDE_FROM_ALL and will not link until their task lands
```

**On Windows nothing changes.** `exclude` is empty there, so no test is marked `DISABLED`, the
global property stays empty, the configure message does not print, and all 16 register exactly as
before. The `DISABLED` property needs CMake 3.9; the project requires 3.20.

## The full inventory

41 declarations. `add_test NAME` at line 847 is inside the `add_lib_test` function body, not a test
of its own, so 40 real tests.

### Unguarded, run on macOS — 8, all passing

| Target | Listed | Runs | Correct? |
|:--|:--|:--|:--|
| `test_w3dlayout` | yes | **pass** | yes — fixed by this audit's predecessor; was the bug |
| `bigfile_selfcheck` | yes | pass | yes — `Python3_Interpreter_FOUND` |
| `vfx_tune_selfcheck` | yes | pass | yes |
| `optionsmenu_selfcheck` | yes | pass | yes |
| `peacetime_selfcheck` | yes | pass | yes |
| `lobbysettings_selfcheck` | yes | pass | yes |
| `lobbyroom_selfcheck` | yes | pass | yes |
| `gametext_selfcheck` | yes | pass | yes |

### Deferred on macOS — 5, now visible

| Target | Was listed | Now | Builds on macOS? | Correct? |
|:--|:--|:--|:--|:--|
| `test_wwmath` | **no** | DISABLED | no | yes — genuinely will not link |
| `test_wwlib` | **no** | DISABLED | no | yes |
| `test_wwsaveload` | **no** | DISABLED | no | yes |
| `test_wwutil` | **no** | DISABLED | no | yes |
| `test_gameengine` | **no** | DISABLED | no | yes |

Each was built individually to confirm the deferral is real rather than assumed. All five fail, and
all five fail at the same line — see below.

### Registered and failing on macOS — 3, and these are honest failures

| Target | Status | Root cause |
|:--|:--|:--|
| `test_compression` | Not Run | `BaseType.h:137` `__int64` |
| `compression_selfcheck` | Not Run | `BaseType.h:137` `__int64` |
| `wwmath_selfcheck` | Not Run | `wwdebug.cpp:46` `<windows.h>` |

**These are not mis-guarded and I have deliberately left them failing.** `wwdebug`, `wwmath` and
`compression` carry no `ZH_NOT_YET_PORTABLE`, so they are targets the project expects to build on
macOS today. A test of a target that is supposed to work and does not should fail loudly. Marking
them `DISABLED` would have made `ctest` green by hiding three real regressions, which is the exact
failure this audit exists to stop.

`wwmath_selfcheck` deserves singling out. Somebody went to real trouble to make it work on macOS —
`CMakeLists.txt:238-245` gives it a separate non-Windows link line, with the comment *"this
self-check is the first evidence that determinism survives clang on arm64, so it is not allowed to
wait for them."* It still does not run, and it is one `#include` away from running.

### Windows-only — 22, all correctly guarded

`test_ww3d2`, `test_ffshader`, `test_dx11device`, `test_dx11state`, `test_dx11layout`,
`test_ffvertex`, `test_ffvertexcompile`, `test_ffvertexd3d9`, `test_ffshadercompile`,
`test_dx11pipeline`, `test_dx11resource`, `test_engineshader`, `test_dx11backend`, `test_dx11post`,
`test_dx11twin`, `test_dx11texture`, `test_pixelcentre`, `test_wwdownload`, `test_debug`,
`test_support`, `dx9_smoke`, `dx9_smoke_msaa`, `bink_smoke`, `miles_smoke`.

Verified by listing the library targets a macOS configuration actually defines and checking each
test's link line against it. Every one needs at least one of `ww3d2`, `ffshader`, `dx11*`,
`engineshader`, `d3d9`, `wwdownload`, `debuglib` or `eabrowserdispatch`, and none of those exists on
macOS. `test_support` links `benchmark` — which *does* exist on macOS — but also
`eabrowserdispatch`, which does not, so it stays guarded.

These come back with D2–D5 and their own Metal-side tests, as the comment above them says.

## The finding worth acting on: two lines block eight tests

Every blocked test on macOS traces to one of exactly two root causes.

| Root cause | Blocks | Owner |
|:--|:--|:--|
| `WWDebug/wwdebug.cpp:46` — `#include <windows.h>` | `test_wwmath`, `test_wwlib`, `test_wwsaveload`, `test_wwutil`, `test_gameengine`, `wwmath_selfcheck` | B5 / B3 |
| `Libraries/Include/Lib/BaseType.h:137` — `__int64`, and `:184` `<windef.h>` | `test_compression`, `compression_selfcheck` | B5 |

Six of the eight are one `#include` in one file. The line already carries EA's own hint at the
answer:

```cpp
#include <windows.h>
//#include "win.h" can use this if allowed to see wwlib
```

I have not fixed either. `wwdebug.cpp` and `BaseType.h` are B5's and B3's, both in progress, and
reaching into them from a test audit would collide with work in flight. But whoever takes them
should know the leverage: **`wwdebug.cpp:46` is the single highest-value line in the M1 build**, and
clearing it turns `wwmath_selfcheck` — the first evidence that determinism survives clang on arm64,
which E1 wants and nobody has — from an unbuildable target into a running one.

## What this audit does not cover

- **Windows.** No Windows machine. The claim that all 16 tests register there unchanged rests on
  `exclude` being empty on Windows, which is how `ZH_NOT_YET_PORTABLE` is defined, not on a build.
  A row is in [`WINDOWS-DEBT.md`](WINDOWS-DEBT.md).
- **Whether the tests that pass are testing anything.** This audit is about whether a test is
  *visible and runs*, not whether its assertions are meaningful. The 7 Python self-checks pass;
  I did not read them.
- **`Generals/`.** Out of scope for the whole port; it has no `CMakeLists.txt`.
