# Windows verification debt

There is no Windows machine on this project (decided 2026-09-22, risk accepted deliberately).
Every task file says "Windows full build and `ctest` green"; none of it can be checked here.

This file is the ledger. It exists so that the accepted risk is a **list**, not a feeling. When a
Windows machine or a CI runner appears, work it from the top down.

## How to add a row

You add a row when you change anything a Windows build compiles, links or runs — which includes
code inside `#ifdef _WIN32`, MSVC flags in `CMakeLists.txt`, a shared header, or a call site whose
Windows branch you did not execute. If you are unsure whether your change qualifies, it qualifies.

Be specific about **Risk**. "Might break" helps nobody. "If MSVC's `char16_t` and `wchar_t`
overload resolution differs here, this call binds to the wrong overload and the string is empty"
is a thing somebody can check in ninety seconds.

## Severity

- **high** — would break the Windows build, or change what the simulation computes
- **medium** — would break a Windows-only feature at runtime
- **low** — cosmetic, a warning, or a flag that moved without changing the command line

## Ledger

| Date | Task | Files | What changed | Risk if wrong | Severity | How to verify |
|:--|:--|:--|:--|:--|:--|:--|
| | | | | | | |

## Standing items

These are known now, before any work has been done, and they do not belong to one task.

| Item | Why it matters |
|:--|:--|
| **Suspected live defect: texture stage 2 is invisible to the D3D11 backend.** | `shader.cpp:954-961` and `:1003-1012` set stage 2 through raw `DX8CALL`, which reaches the device directly and mirrors nothing. Under `-dx11` the backend never sees those sets, and the wrapper's own `TextureStageStates` shadow does not record them either, so its redundant-state filter compares against a stale shadow. Found by D1 recon, verified by reading only. **Not a port artifact — this is the shipping Windows default renderer.** Needs a Windows machine to confirm and to fix; do not fix blind. D1's PR5 is where it lands. |
| **E1 cannot do its job.** | The determinism gate was designed as a cross-platform checksum comparison: same seed, Windows and macOS, same result. With one platform it can only prove the Mac build is self-consistent and record a checksum for a future comparison. That is worth doing and is **not** the same thing. The question E1 exists to answer — does this port desync against Windows — stays open until a Windows machine exists. Nothing downstream of M1 should be described as validated until it is. |
| **`replay-check.ps1` cannot run.** | PowerShell, and it is the tool `CONTRIBUTING.md` names for proving a change leaves replays and network games alone. Any change reaching `GameLogic` or the maths under it is unverified against it. |
| **The MSVC flag semantics in `CMakeLists.txt` are unverified.** | A1 moves `/EHa`, `/Oy-`, `/Zi`, `/FS`, `/MP` and `CMAKE_MSVC_RUNTIME_LIBRARY` behind a platform guard. The intent is that Windows produces an identical compiler command line. Nobody has seen one. |
| **`#pragma pack` layout is assumed, not measured, on MSVC.** | B4 adds `sizeof`/`offsetof` asserts. They will be checked by a clang build only. If MSVC disagrees on any packed struct, the asserts fire there and the build fails loudly — which is the good outcome, but it happens on someone else's machine, later. |
