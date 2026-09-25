# B5 — Win32 scalar types

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** `wwlib` pass done (-a9). `GameEngine` half in progress (-18): the `PreRTS.h` guard and the enum pass are done, see below
- **Size:** 21 of 604 files in `GameEngine/Source` — 8 Common, 7 GameClient, 6 GameNetwork

## Why

`DWORD`, `HRESULT`, `BOOL`, `LPSTR`, `HWND` leaking out of the platform layer into engine code.
The number is small, which is the point: this is a tidy-up that makes the engine library compile
without `windows.h`, and it is worth doing properly rather than with a compatibility header
because there are only 21 files.

## Scope

Start with the list:

```console
grep -rIl --include='*.cpp' -E '\bHWND\b|\bHRESULT\b|\bDWORD\b|windows\.h' GeneralsMD/Code/GameEngine/Source
```

`GameEngine/Include` has 4 more under `Common`, plus `Precompiled` and
`GameNetwork/WOLBrowser`. `GameLogic` has none, and finishing this task should keep it that way.

The largest single item on that list is **`GameEngine/Source/Common/System/ControlServer.cpp`**
(840 lines): a loopback WebSocket server behind the `-control` switch, written against Winsock 1.1
because, as its own comment says, `windows.h` "which PreRTS.h already pulled in, brings Winsock 1.1
with it. That is every call this file makes". Every call it makes is also plain BSD sockets, which
is the POSIX original — `SOCKET` is an `int`, `closesocket` is `close`, `ioctlsocket(FIONBIO)` is
`fcntl(O_NONBLOCK)`, and `WSAStartup` goes away. Port it rather than stubbing it; see the note on
`-control` in the plan's README for why it is worth having on macOS.

## Recon findings, 2026-09-22 — this changes the ORDER of the work

The survey is done (`docs/mac-port/B5-win32-type-survey.md`). Its most important conclusion is
about sequencing, and this task file had it wrong.

**Do the `PreRTS.h` guard early, not last.** Until `windows.h` is out of the precompiled header,
the PCH silently satisfies every one of the other fixes, so none of them is visible. The guard is
what turns the remaining work into a compile-error worklist. Three steps:

1. Give every file that genuinely needs Windows its **own** `#include`s while the PCH still
   provides them. Today the bucket-(c) files declare nothing themselves — `ChromaKeyboard.cpp`
   includes `wininet.h` but leans on the PCH for `HANDLE` and `CreateThread`; `StackDump.cpp`
   names `IMAGEHLP_LINE64` with no include of its own. **This step is a no-op on Windows by
   construction** — identical declarations, identical command line, identical object file — which
   makes it the one change in B5 that can be made with real confidence without a Windows machine.
   Its own commit.
2. Wrap `PreRTS.h:43–96` in `#if defined(_WIN32)`. Windows is unchanged by construction; macOS
   starts producing honest errors.
3. Then the bucket-(a) substitutions, which are now visible.

Counts to revise: **18 of the 21 files are compiled** (`simpleplayer.cpp`, `urllaunch.cpp` and
`GameSpyGameInfo.cpp` are already in `REMOVE_ITEM`). The headers under `Common` are **9, not 4**,
of which 6 are real. And the distribution is not what "scalar types" suggests: bucket (a) 7 files,
bucket (b) 3, **bucket (c) eleven** — over half the compiled sites are whole files of Windows
subsystem code, not type substitutions.

Bucket (b) is smaller than it looks: all three sites are the same handle, `ApplicationHWnd`, and
nothing inspects it. The engine wants a "show the user this fatal message" hook, not a window
handle.

**WOLBrowser is confirmed as bucket (c), and it is worse and easier than this file assumed.** It
is the reason `gameengine` has a *configure-time* dependency on `midl.exe`, and the only reason
`atlbase.h` is in the PCH. `TheWebBrowser` has never been non-NULL — the sole assignment site
(`GameEngine.cpp:923`) is commented out and every consumer already tests for NULL. Moving
`GameNetwork/WOLBrowser/` into `Win32Device/` is behaviour-preserving in the strongest sense and
removes `atlbase.h`, the MIDL custom command and `eabrowserdispatch` from the engine's graph at a
stroke. The `createWebBrowser` factory stays on `GameEngine`.

**`JobSystem.cpp` came out of this survey and is now its own task, B8.** It is a Win32 thread pool,
not a type leak, and it is the one bucket-(c) file the engine actually needs working on macOS.

Two quick wins worth taking first: `LookAtXlat.cpp:31` is `#include "windows.h"` and nothing else
in the file uses a Windows type; `AsciiString.h:60` is a bare `windows.h` include with no Windows
type named anywhere in the header — and that one is in most of the engine's include graph.

## The `wwlib` pass, 2026-09-25 — what was done and what it did not reach

Scheduled from `Tools/syntax_sweep.py build-mac wwlib` on `9f4c1811`, re-run after every step.
Front-end numbers throughout; `-fsyntax-only` is not a link.

**58 of 82 → 64 of 67.** The denominator shrank by fifteen files that are now left out of `wwlib`
off Windows, so read it as two numbers: fifteen excluded with a reason, six fixed in place. On Windows
`wwlib` still gets all 82, in the same order.

| What | How | Why |
|:--|:--|:--|
| DirectDraw: `convert`, `ddraw`, `dsurface` | excluded | PM's ruling. `ConvertClass` survives as a type: `wwfont.cpp` calls only its inline `Convert_Pixel`, and WW3D2's `txt*.cpp` only hold references to it |
| `_mono`, `mono` | excluded | a driver for the `\\.\MONO` second-monitor device. WW3D2's `rendobj.cpp` includes `_mono.h` and uses nothing from it, which is a dead include for the D-track to drop |
| `data`, `rcfile`, `msgloop`, `keyboard`, `LaunchWeb`, `WWCOMUtil`, `verchk` | excluded | Win32 resources, the message pump, ShellExecute, COM and version resources. **Zero callers** in `GeneralsMD/Code`. That answers the `oaidl.h` question: `WWCOMUtil.h` is included by `WWCOMUtil.cpp` and nothing else |
| `win.cpp` | excluded | defines the `HINSTANCE`/`HWND` globals that `win.h` declares only under `_WINDOWS` |
| `registry.cpp` | excluded; `registry.h` `#error`s off Windows | HKEY throughout. Its real callers are `dx8wrapper.cpp`, `W3DDisplay.cpp` and `WWAudio.cpp`, which persist device and audio settings; where those live on macOS is their owners' decision. `ww3d.cpp` includes it and uses nothing. WWDownload's `urlBuilder.cpp` includes its **own** `Registry.h` |
| `srandom.cpp` | excluded | `SecureRandomClass` has no caller, and its only non-Windows seed is `_UNIX` |
| `systimer.h`, `refcount.cpp` | `<windows.h>` guarded | dead since B2 moved the clock onto `Lib/Clock.h` |
| `systimer.h` | **width fix** | `StartTime`/`WrapAdd` were `unsigned long`, so `WrapAdd = 0 - StartTime` wrapped at 2^64 on LP64 and `Get` returned about 2^64 after the 49.7-day wrap. `Lib/Clock.h` names this exact expression as needing 32 bits |
| `ini.cpp` | `OutputDebugString` guarded; stderr off Windows | |
| `rawfile.cpp` | **ported to POSIX descriptors** | see below |

**`rawfile.cpp`'s `_UNIX` arms were holes, like `mutex.cpp`'s.** Opening READ\|WRITE used
`fopen("w")`, which truncates the file the Windows arm opens with `OPEN_ALWAYS` precisely so that it
is not destroyed. `Raw_Seek` returned `fseek`'s 0. `Read` took `ferror`'s answer as success.
`Get_Date_Time` returned Unix time where every caller keeps DOS time. `Set_Date_Time` asserted. They
are replaced by POSIX arms written call-for-call against the Win32 ones. Three tests in `test_wwlib`
cover them, and each was checked by putting the old behaviour back: a truncating open, seek answering 0,
Unix time, and local time instead of UTC each turned their test red. They were run against the real
`wwlib` objects in a scratch harness, since `test_wwlib` does not link yet. **The local-time mutation is
caught only under a non-UTC `TZ`.** `Set_Name`'s `_UNIX` arm (backslash rewriting and lowercasing) is
left alone: paths are C1's.

### What is left in `wwlib`, and whose it is

- `mixfile.cpp` — `_splitpath`, **C1**, as ruled.
- `mpu.cpp` — x86 `__rdtsc`/`__cpuid`, left alone as ruled.
- **`cpudetect.cpp` — handed on whole, not split.** Guarding its `<windows.h>` and `<intrin.h>`
  exposes 23 errors. The Windows-API ones (`OSVERSIONINFO`, `VER_PLATFORM_*`,
  `GetTimeZoneInformation`) are B5-shaped, but they sit in the same file as x86 `__rdtsc`/`__cpuidex`,
  `__int64` and an MSVC-only extra qualification (`:1064`), and the two halves cannot be separated:
  `Init_Memory()` and `Init_OS()` run **only inside `if (Has_CPUID_Instruction())`**, so what the
  memory port does depends on the arm64 answer to CPUID. And the outputs are not diagnostics:
  `W3DShaderManager::testMinimumRequirements` hands `Get_Processor_Speed`, the Intel/AMD model tiers
  and `Get_Total_Physical_Memory` to `GameLOD`, which chooses the default detail preset from them.
  "What tier is an Apple M-series chip" is a product decision, not a type substitution. **Recommended:
  one task, `cpudetect.cpp` + `mpu.cpp`, "CPU detection and the tick clock on arm64".**

So `wwlib` is not one task from a front-end build: it is that task plus C1's `_splitpath`.

### Found on the way, not fixed here

- **The link tail is three symbols, and none comes from an exclusion.** A trial link of the 64
  objects leaves exactly `AutoPoolClass<GenericSLNode,256>::Allocator`,
  `AutoPoolClass<MultiListNodeClass,256>::Allocator` and `Int<64>::Remainder` unresolved.
  `mempool.h`'s `DEFINE_AUTO_POOL` and `int.cpp:48` spell these `template<> T X<...>::m;`, and in
  standard C++ an explicit specialisation of a static data member **with no initialiser is a
  declaration, not a definition**, so clang emits nothing. `int.cpp`'s three siblings survive only
  because they have `= 0`. MSVC presumably treats the form as a definition, since Windows links; that
  is not verified. The fix is an initialiser (`{}`). It changes Windows-compiled text in a macro with
  ten users, so it wants its own commit and a second reader. It was introduced by `8a468857`.
  What this check cannot see: Debug-only references, the three files that do not compile, and
  consumers outside `wwlib`.
- **Debug configuration:** `refcount.cpp`'s two `DebugBreak` calls are the only addition; they wait
  for B16's portable break rather than invent a second one.
- **`Tools/syntax_sweep.py` reads `CXX_FLAGS` and `CXX_INCLUDES` but not `CXX_DEFINES`, and compiles
  `.c` as C++.** `compression` builds, yet sweeps 13 of 28. `wwlib`'s defines are only
  `_CRT_NONSTDC_NO_WARNINGS` and `_CRT_SECURE_NO_WARNINGS`, so its number is unaffected; a target
  with real `target_compile_definitions` is not.
- `texturethumbnail.cpp:339`/`:459` read and write a DOS date as `sizeof(unsigned long)`: 8 bytes on
  macOS, 4 on Windows. The thumbnail cache is therefore not portable between the two. D-track or B10.
- Shared-code quirks now documented in the tests, identical on both platforms: `RawFileClass::Size()`
  stores its answer in `BiasLength`, so a second call on the same object never sees the file change.
  `Read` retries forever on a persistent read error, because the base `Error()` does nothing.

## `test_wwlib` on macOS, 2026-09-25

The first run of `test_wwlib` on macOS found eight failures in four families, all against published
vectors or exact values. Fixed in three commits, and it now passes **75 of 75**:

- **`#ifdef BIG_ENDIAN`** in `fixed.h` and `base64.cpp`. POSIX defines `BIG_ENDIAN` as the *name of a
  byte order*, unconditionally, so every little-endian POSIX machine took the big-endian branch. It
  is now `WWLib/wwendian.h`'s `WW_BIG_ENDIAN`, from `__BYTE_ORDER__`. Whether a Windows header
  defines `BIG_ENDIAN` is unverified; see `WINDOWS-DEBT.md`.
- **SHA-1 on `long` words:** 64-bit rounds, a 128-byte block, and an 8-byte store into the block's last 4
  bytes, which AddressSanitizer reports as a stack-buffer-overflow when the old store is put back.
  Every word is `uint32_t` now.
- **MD5's `UINT4`** was `unsigned long`. It is `uint32_t` now.

**Does anything the game links call these?** Checked by symbol, through every tracked source outside
`Tools/`, with comments, strings and `#if 0` blocks stripped. **No:**
- `PKey`, `PKPipe`, `PKStraw` and `RandomStraw` are referenced only by each other.
- `SHAEngine` is used only by `srandom.cpp`, which has no callers and is excluded off Windows, and by
  the tests.
- Base64 and MD5 are used only by the tests.
- `INIClass::Put_UUBlock`/`Get_UUBlock` are called only from `RegistryClass::Save_Registry_Values`
  and `Load_Registry`, which nothing outside `registry.cpp` calls.

The fixes matter for anything that uses these later, not for the game as it stands.
## The `GameEngine` half, 2026-09-25 — the guard, and what was behind it

**Sequencing, decided: step 2 before step 1, and step 1 per file.** With `#if defined(_WIN32)` around
`PreRTS.h`'s Windows block, Windows still gets the whole precompiled header, so giving each Windows
file its own `#include`s (step 1) changes nothing on Windows today. On macOS those files fail
either way, because they are excluded or ported. Step 1 only matters on the day someone slims the
*Windows* side of `PreRTS.h`. And it cannot be verified here: which SDK header a file needs is a
guess without an SDK, and a wrong guess stays invisible until that day. So each bucket-(c) file gets
its own includes **when it is excluded or moved**, not as a batch up front. Do not re-open this
without a Windows machine.

**The guard** wraps lines 43-96 unchanged. The `#else` is the portable subset of that block, plus
`<wchar.h>`. The Windows evidence is in `WINDOWS-DEBT.md`: include sequence and `-dM` macro set are
identical, each with a differing control. **The token-stream diff is blind for this file**: it
preprocesses to one line, and its control was identical too.

**What it exposed was not mostly Win32 types.** Before the guard, all 602 files stopped at
`atlbase.h`. After it, a census (every file at `-ferror-limit=0`, errors de-duplicated by site)
found **1,868 distinct error sites**. By files affected:

| Root cause | Files |
|:--|--:|
| Forward-declared enums (`enum X;`), an MSVC extension ISO C++ forbids; reached through `STLTypedefs.h` in the PCH | **602** |
| Windows APIs and types: this task's buckets (a)-(c) | 154 |
| "Non-constant-expression cannot be narrowed from unsigned long" in INI `FieldParse` tables | 66 |
| `__int64` | 2 |

**The narrowing errors are a cascade of the enums, not a root cause of their own.** Probed on the
three worst files, with `-fms-compatibility` used only to make clang accept the forward enums:

| File | Narrowing | Forward-enum | Total errors |
|:--|--:|--:|--:|
| `GameLOD.cpp` | 6 → 0 | 22 → 0 | 60 → 124 |
| `ObjectCreationList.cpp` | 45 → 0 | 40 → 0 | 141 → 140 |
| `INIMiscAudio.cpp` | 36 → 0 | 13 → 0 | 63 → 109 |

The mechanism: each table's target struct has a member of a forward-declared enum type. The struct
is therefore broken, `offsetof` over it is no longer a constant, and the `Int` field reports
narrowing. The totals rising is why `-fms-compatibility` is rejected; see the plan's rules.

**The enum pass.** Every forward-declared enum got an explicit underlying type on its declaration
and on its definition. It is spelled `: Int` where the file already uses `Int`, and plain `: int`
elsewhere; the two are the same type (`BaseType.h:130`). MSVC gives an unfixed unscoped enum `int`
as its underlying type, so this should be the same type there. On clang it pins an implicit choice
to MSVC's, and it removes clang's license to assume an enum holds only its declared range, which
closes a codegen difference as well as a front-end one. 62 enums: 60 definitions, 136 forward
declarations, 101 files. Six of those forward declarations are in `GameEngineDevice` and `WorldBuilder`, which
is required: MSVC rejects a forward declaration whose underlying type differs from the definition's.

- **Range:** every definition was scanned for values at or above `0x80000000` and for `1 << 31`-style
  shifts, and none was found. Clang also rejects any enumerator that does not fit a fixed type, and it
  reported none.
- **Size:** a throwaway TU asserted `sizeof == 4` and an underlying type of `int` for nine
  enums that go through `Xfer` or INI (`ObjectID`, `DrawableID`, `BodyDamageType`, `KindOfType`,
  `WeaponSlotType`, `ModelConditionFlagType`, `ScienceType`, `_TerrainLOD`, `ObjectStatusType`), plus
  one deliberately false control. Only the control fired.
- **Diff:** 191 changed lines are an old line plus one `: Int`/`: int`. Four are new opaque
  declarations. One is `typedef enum _TerrainLOD;`, a typedef that declared no name, which became
  `enum _TerrainLOD : Int;`.
- **Elaborated first use.** `View.h` named `enum FilterModes` and `enum FilterTypes` in its virtuals
  before either was declared, which in C++ is an implicit, unfixed declaration. `W3DShaderManager.h`
  does the same with `FilterModes`, and `W3DBridgeBuffer.h` with `BodyDamageType`. Each of the three
  now declares the enum before using it, so it no longer depends on what its includer brought in
  first. Two of the three are Windows-only.
- **Left alone:** `CustomScenePassModes`, `GraphicsVenderID` and `waveType` are declared and defined
  only in `GameEngineDevice`, and `GameLODLevel` is declared only there.

Found on the way:

- **`ObjectStatusType` is declared and never defined.** `RiderChangeContain.h:49` has a member of
  that type, which `parseIndexList` fills and `MAKE_OBJECT_STATUS_MASK` reads as an index. MSVC
  treated the opaque enum as a complete int-sized type with no enumerators, and `: Int` makes exactly
  that legal C++.
- **`HackerAttackMode` and `GameLODLevel` are dead declarations**, with no uses. Delete them some day.

Census after the pass: **1,868 → 467 distinct sites**, with no enum or narrowing site left. The
first-error sweep still reads 0/602, because every file now stops first at `UnicodeString.h:407`
(`_wcsicmp`).

**Enum bitfields: checked, and none exist.** A fixed-`int` enum bitfield is signed, and clang made a
bitfield of an unfixed enum with non-negative values unsigned. MSVC has always made enum bitfields
signed. So this pass moves clang to MSVC's behaviour, and an enum bitfield that uses its field's top
bit would now read negative on clang, as it always has on Windows. A scan of all 3,252 tracked
sources for member declarations with a bit width found **no bitfield of any of the 62 enums**. The
same scan found 127 bitfield lines of other types (`int` 60, `unsigned` 18, `char` 18, `Bool` 17, and a
typedef'd `zoneStorageType`), which shows it would have seen one. If an enum bitfield is ever added,
this paragraph is the reason to check its width.

**After the enum pass, 2026-09-25:**

- `_wcsicmp` is bridged in `MSVCCompat.h` off MSVC, with MSVC's "C"-locale meaning: fold `A`-`Z`
  only, and return the difference. It is not `wcscasecmp`: measured, in a UTF-8 locale that calls
  E-acute and e-acute equal, and MSVC's "C" locale does not. Call sites are unchanged. B1's
  char16_t move retypes `compareNoCase` onto `WideCharICmp`, which gives the same answer.
- Three missing `typename`s (`SparseMatchFinder.h` x2, `ScriptConditions.cpp`) and eight extra
  qualifications (`Object.h` x3, `ThingTemplate.h`, `PartitionManager.h`, `CommandXlat.h`,
  `ChinookAIUpdate.h`, `FlightDeckBehavior.h`). Each diff is exactly the added or removed token, and
  MSVC accepts the standard form.
- `GameMemory.h`'s global `operator new`/`delete` are **-47's** (`feature/mac-port-linux`), not
  this task's.

- **GameSpy's own `_strlwr`/`_strupr` are left as they are, deliberately** (`gsplatformutil.c`). Their
  `tolower` on a plain `char` is undefined for bytes above 0x7F under `-fsigned-char`. It is dead-service
  code, and changing vendored behaviour is out of scope; only the C++ declaration clash is patched.

**The vendored GameSpy SDK defines `_UNIX`**: `gsplatform.h:38`, on `__linux__` or Apple. Every
engine file that includes a GameSpy header therefore has the macro the plan forbids from that point
on, and any `_UNIX` branch in a header read for the first time after that point is taken. Measured
across all 602 engine files, 2026-09-25: 34 get `_UNIX`. Only two headers are first read after it, and
only those two evaluate their `_UNIX` branch: `GameNetwork/udp.h` in 25 files, where it adds
`<errno.h>` and is harmless, and WWLib `thread.h` in 3 (`BuddyThread`, `PeerThread`,
`PersistentStorageThread`), where it includes `osdep.h` and is fatal. `osdep.h` has never existed in
this tree, yet 20 WWVegas files include it under `_UNIX`, which makes a leak loud for those. It would
be silent for any `_UNIX` branch that does something else. `matrix3d.h` and `vector3.h` show up
after the define in `Recorder.cpp`, but only as guard-skipped re-entries: they carry a `#pragma once`
before their guard, so clang re-opens them, and their first real entry is earlier. Counting line
markers instead of first entries says otherwise; that is how this was nearly misreported. **Resolved, same day:** `vendor.sh` renames the SDK's macro to `GSI_UNIX`. All 117 SDK objects have
identical machine code before and after, and a control shows `GSI_UNIX` still drives its POSIX paths.
Two tripwires keep `_UNIX` out: `MSVCCompat.h` `#error`s on it, and the ctest `unix_define_check`
scans every source (vendored included) and every compile command, with an armed control. `thread.h`'s
dead `osdep.h` include is left in place on purpose, because it is what makes a leak fail loudly.

<details><summary>The 62 enums</summary>

| Enum | Definition | Forward declarations |
|:--|:--|--:|
| `_TerrainLOD` | `GameClient/TerrainVisual.h:173` | 1 |
| `AcademyClassificationType` | `Common/AcademyStats.h:71` | 2 |
| `AIDebugOptions` | `GameLogic/AI.h:67` | 1 |
| `AIStateType` | `GameLogic/AIStateMachine.h:61` | 1 |
| `AnimTypes` | `GameClient/AnimateWindowManager.h:83` | 1 |
| `ArmorSetType` | `GameLogic/ArmorSet.h:46` | 1 |
| `AttitudeType` | `GameLogic/AI.h:605` | 1 |
| `AudioAffect` | `Common/AudioAffect.h:35` | 2 |
| `AudioPriority` | `Common/AudioEventInfo.h:52` | 1 |
| `AudioType` | `Common/AudioEventInfo.h:44` | 1 |
| `BattlePlanStatus` | `GameLogic/Module/BattlePlanUpdate.h:101` | 1 |
| `BodyDamageType` | `GameLogic/Module/BodyModule.h:53` | 7 |
| `BridgeTowerType` | `GameClient/TerrainRoads.h:49` | 1 |
| `BuildableStatus` | `Common/ThingTemplate.h:217` | 1 |
| `CanAttackResult` | `GameLogic/WeaponSet.h:188` | 4 |
| `CanMakeType` | `Common/BuildAssistant.h:75` | 1 |
| `ChipsetType` | `Common/GameLOD.h:76` | 1 |
| `CommandOption` | `GameClient/ControlBar.h:75` | 2 |
| `CommandSourceType` | `Common/GameCommon.h:235` | 8 |
| `CpuType` | `Common/GameLOD.h:67` | 1 |
| `DamageType` | `GameLogic/Damage.h:50` | 1 |
| `DrawableID` | `Common/GameType.h:48` | 3 |
| `EditorSortingType` | `Common/ThingSort.h:36` | 1 |
| `EvaMessage` | `GameClient/Eva.h:41` | 1 |
| `FilterModes` | `GameClient/CommandXlat.h:98` | 2 |
| `FilterTypes` | `GameClient/CommandXlat.h:87` | 2 |
| `GadgetGameMessage` | `GameClient/Gadget.h:136` | 1 |
| `GeometryType` | `Common/Geometry.h:49` | 1 |
| `GUICommandType` | `GameClient/ControlBar.h:168` | 2 |
| `HackerAttackMode` | **none** - see below | 2 |
| `HordeActionType` | `GameLogic/Module/HordeUpdate.h:49` | 1 |
| `KindOfType` | `Common/KindOf.h:44` | 2 |
| `LegalBuildCode` | `Common/BuildAssistant.h:89` | 1 |
| `LocomotorSetType` | `GameLogic/Module/AIUpdate.h:77` | 1 |
| `MaxHealthChangeType` | `GameLogic/Module/BodyModule.h:75` | 2 |
| `ModelConditionFlagType` | `Common/ModelState.h:93` | 3 |
| `NameKeyType` | `Common/NameKeyGenerator.h:51` | 4 |
| `ObjectID` | `Common/GameType.h:41` | 7 |
| `ObjectStatusType` | **none** - see below | 1 |
| `ParticlePriorityType` | `GameClient/ParticleSys.h:92` | 1 |
| `ParticleSystemID` | `GameClient/ParticleSys.h:61` | 7 |
| `PhysicsTurningType` | `GameLogic/Module/PhysicsUpdate.h:42` | 2 |
| `ProductionID` | `GameLogic/Module/ProductionUpdate.h:46` | 1 |
| `ProductionType` | `GameLogic/Module/ProductionUpdate.h:51` | 1 |
| `RadarPriorityType` | `Common/Radar.h:129` | 2 |
| `RadiusCursorType` | `GameClient/InGameUI.h:92` | 1 |
| `ScienceType` | `Common/Science.h:43` | 14 |
| `ShadowType` | `GameClient/Shadow.h:41` | 4 |
| `SpecialPowerType` | `Common/SpecialPowerType.h:40` | 5 |
| `StaticGameLODLevel` | `Common/GameLOD.h:45` | 3 |
| `StealthLookType` | `GameClient/Drawable.h:238` | 1 |
| `TerrainDecalType` | `GameClient/Drawable.h:279` | 1 |
| `TimeOfDay` | `Common/GameType.h:68` | 3 |
| `UpgradeStatusType` | `Common/Upgrade.h:49` | 1 |
| `WaypointID` | `GameLogic/TerrainLogic.h:55` | 1 |
| `WeaponBonusConditionType` | `GameLogic/Weapon.h:172` | 3 |
| `WeaponChoiceCriteria` | `GameLogic/WeaponSet.h:173` | 1 |
| `WeaponLockType` | `GameLogic/WeaponSet.h:180` | 1 |
| `WeaponSetConditionType` | `GameLogic/WeaponSet.h:110` | 1 |
| `WeaponSetType` | `GameLogic/WeaponSetType.h:41` | 3 |
| `WeaponSlotType` | `Common/GameType.h:173` | 1 |
| `WeaponStatus` | `GameLogic/WeaponStatus.h:33` | 1 |

</details>

## Do

1. For each site, decide which of three it is:
   - **A scalar in disguise.** `DWORD` standing in for `uint32_t`, `BOOL` for the engine's own
     `Bool`. Replace with the engine type from `Libraries/Include/Lib/BaseType.h`. Most sites are
     this.
   - **A genuine handle crossing the boundary.** `HWND` reaching engine code from the device layer.
     These want an opaque typedef the platform layer defines — the engine should not know what a
     window handle is made of. `GameEngine/Include/Common/GameEngine.h`'s factory seam is the model
     for how this code already separates the two.
   - **Windows code that should not be in `GameEngine` at all.** `GameNetwork/WOLBrowser` and the
     `WebBrowser` factory are the likely candidates. Do not move files in this task; name them in
     the pull request and let B6 decide whether they are stubbed or excluded.
2. `GameEngine/Include/Precompiled/PreRTS.h:51` is `#include <windows.h>`, and it is the
   precompiled header the whole engine library compiles against. Whatever else happens, that one
   line has to become conditional or every other fix in this task is invisible.

## Done when

`gameengine`'s headers and sources compile on macOS without any `windows.h` in the include graph —
which is not the same as the library linking, which is B6. Windows full build and `ctest` green.

## Do not

- Do not write a `WinTypes.h` that defines `DWORD` and `HWND` on macOS. It would make the errors go
  away and leave the problem in place, and every later task would build on it. There are 21 files;
  fix them.
- Do not touch `GameEngineDevice`. Windows types belong there and stay there.
