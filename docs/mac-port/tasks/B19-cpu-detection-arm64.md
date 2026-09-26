# B19 — CPU detection and the tick clock on arm64

- **Milestone:** M1
- **Depends on:** B5's `wwlib` pass
- **Status:** done: merged; tier decided (option (c), decision 2); verifiable once `gameengine` compiles
- **Size:** `WWLib/cpudetect.{h,cpp}`, `WWLib/mpu.cpp`, one test

## Why

These were the last two `wwlib` sources that were neither ported nor ruled out, apart from C1's
`mixfile.cpp`. They belong together because they are one question: what can this engine learn about
a processor that has no CPUID and no RDTSC? They were not split by platform half, because they
cannot be split. `Init_Memory()` and `Init_OS()` are Windows API, but they ran only inside
`if (Has_CPUID_Instruction())`, so what they did on arm64 depended on the x86 answer.

## What was done

- **`mpu.cpp` is excluded off Windows, not ported.** Its four functions (`Get_CPU_Rate`,
  `Get_CPU_Clock`, `Get_RDTSC_CPU_Speed`, `RDTSC`) have no callers anywhere in `GeneralsMD/Code`.
  `cpudetect.cpp` and `Except.cpp` include `mpu.h` and use nothing from it, and cpudetect reads
  `__rdtsc` itself.
- **`cpudetect.cpp`'s x86 half** compiles only under `CPUDETECT_X86` (`_M_IX86 || _M_X64`). That is
  MSVC's own macros, so Windows x64 compiles what it always did. Elsewhere `Has_CPUID_Instruction()`
  is false, and everything CPUID would have answered keeps its "unknown" default. That is a true
  answer on arm64, not a stub.
- **The Windows half has Darwin arms**, replacing `#elif defined(_UNIX)` arms that read `#warning FIX`:
  - memory: `hw.memsize`, Mach `host_statistics64`, `vm.swapusage`, clamped exactly as the Win32
    arm clamps, so 0x7FFFFFFF on any machine with 2GB or more;
  - OS version: `kern.osproductversion`, `kern.osversion`;
  - the brand string: `machdep.cpu.brand_string`;
  - the compact log's time-zone bias.
- **`Init_Memory`/`Init_OS` moved out of the CPUID block.** On x86 that block always runs, so the
  order is unchanged. On arm64 they would otherwise never run, total memory would read 0,
  `m_memPassed` would be false, and GameLOD would turn off trees, the shell map and full-size textures.
- `__int64` became the header's `sint64`, and the MSVC-only `CPUDetectInitClass::` qualifier inside
  its own class is gone.

`wwlib`: **65 of 66** at the front end. `mixfile.cpp` is the one left, and it is C1's. A trial link of
all 65 objects resolves completely.

Measured on an M3 Pro: brand string `Apple M3 Pro`, OS `macOS`, memory 2047MB after the clamp, and
every x86 feature bit `No`.

## The open decision: what speed an unmeasurable processor reports

`CPUDETECT_UNMEASURED_PROCESSOR_MHZ` in `cpudetect.h`. It is one macro, it is the only place, and the
full reasoning is in the comment above it. It is **0**, the value this file already reported for a
processor it could not time, so it adds no behaviour of its own. It is a product decision, not a
port decision, because of where it goes:

`testMinimumRequirements` hands it to `GameLODManager` beside `CpuType` `XX` (the model tables are
x86 tables). On first launch `XX` sends GameLOD to the `RunBenchmark` stub, which is set to beat
every 2003 profile, and this value is ignored. **On every later launch it is `m_cpuFreq`, and below
`ReallyLowMHz` (400) the shell map is turned off.** So at 0, the first launch and every later launch
disagree.

| Option | Where | Effect |
|:--|:--|:--|
| (a) 0, as now | `cpudetect.h` | Honest; the shell map runs once and never again |
| (b) a nominal figure at or above the top preset, e.g. 3000 | `cpudetect.h` | Later launches agree with the first; the log calls a chosen number a clock speed |
| (c) 0 here, and treat unknown as fast in `testMinimumRequirements` | `W3DShaderManager.cpp` | Same outcome as (b), decided in the renderer rather than claimed by this class |
| (d) name an arm64 `CpuType`, e.g. `P4`, and skip the stub | `W3DShaderManager.cpp`, GameLOD presets | The most explicit; touches GameEngineDevice |

`test_wwlib`'s cpudetect test checks the value by name.

## What this does not establish

- The Darwin arms were run on one machine, an M3 Pro on macOS 27. An Intel Mac would take the
  no-CPUID path too. It is out of scope and would want `<cpuid.h>`.
- Available memory is free plus inactive pages. That is close to Windows' "free plus standby", but
  it is not the same accounting. The clamp hides the difference on any real machine.
- The compact log's time-zone bias includes daylight saving; Windows' `Bias` does not.
- Nothing here was compiled by MSVC. See `WINDOWS-DEBT.md`.

## Decision taken, 2026-09-25: option (c)

The tier is decided; the marked point is no longer open. `CPUDETECT_UNMEASURED_PROCESSOR_MHZ` stays
`0` — what the measurement class honestly knows — and `testMinimumRequirements` treats an unknown
CPU as meeting the top preset. Reasoning in the plan README, "Decisions taken".

**Built, 2026-09-26 (B6):** off Windows `testMinimumRequirements` is PosixDevice's
(`PosixRenderHooks.cpp`), and reports an unmeasured CPU at `UNMEASURED_CPU_REPORTED_MHZ` = 3049, the
fastest profile the shipped `GameLODPresets.ini` names; `posix_render_hooks_selfcheck` proves it meets
every preset in the game's own file. The end-to-end half - a first and a later launch choosing the
same preset - waits on `test_gameengine`. Windows' W3DShaderManager body is unchanged: on x86 cpudetect
always measures, so its branch would never run.

~~**Not yet built.**~~ `testMinimumRequirements` is GameEngine code and `gameengine` does not compile on
macOS until B5's GameEngine half lands. Whoever takes it: make the unknown-CPU branch explicit and
named, and prove it by checking that a first launch and a later launch choose the SAME preset — the
inconsistency between them is the bug option (a) would have left in place.
