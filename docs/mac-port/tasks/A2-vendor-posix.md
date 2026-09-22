# A2 — POSIX vendor script

- **Milestone:** M1
- **Depends on:** nothing
- **Blocks:** A3
- **Status:** claimed
- **Size:** one new script, ~150 lines, mirroring `GeneralsMD/Code/Tools/vendor.ps1`

## Why

A fresh clone cannot configure until the third-party sources EA stripped are on disk.
`Tools/vendor.ps1` fetches them and is PowerShell. Nothing else blocks a Mac clone from starting.

## Scope

New: `GeneralsMD/Code/Tools/vendor.sh`. Read `vendor.ps1` first and mirror its behaviour — it is
well commented and its idempotence (a directory check per library on a second run) is a feature
worth keeping.

What it fetches, and what a Mac actually needs:

| Library | Mac? |
|:--|:--|
| zlib 1.1.4 | yes |
| LZH-Light 1.0 | yes |
| GameSpy SDK | yes — `gameengine` links it and it is CMake-based and portable |
| DirectX 8 SDK headers | no. Windows only. Gate it. |
| FFmpeg dist | not for M1. It is a Windows `.lib` dist; Mac wants Homebrew or a dylib build. Leave a clear `TODO(C4/D-track)` rather than guessing now. |
| `art-latest` upscaled art | yes, and verify the sha256 against `art.json` exactly as the PowerShell does |

## Do

1. Plain `sh` or `bash`, `set -euo pipefail`, `curl -fsSL` and `shasum -a 256`. No Homebrew
   dependency for the script itself.
2. Same paths, same `-Force` equivalent (`--force`), same "already there, skipping" output shape,
   so the two scripts are recognisably the same tool.
3. Verify the art checksums. This is the one step where a silent failure produces a game that
   looks subtly wrong much later, so it fails loudly.
4. Do not touch `vendor.ps1`. Two scripts that drift are better than one that neither platform
   trusts, and a shared implementation is not worth a Python dependency here.

## Done when

On a clean clone on macOS, `GeneralsMD/Code/Tools/vendor.sh` puts zlib, LZH-Light, the GameSpy SDK
and the art where `CMakeLists.txt`'s `foreach(vendored ...)` guard expects them, A1's configure
gets past that guard, and a second run of the script is a no-op that prints so.

Windows is untouched: `vendor.ps1` unchanged, `build.bat` unchanged.

## Do not

- Do not fetch the DirectX SDK on Mac. Nothing portable includes `d3d8.h`.
- Do not solve FFmpeg here. C4 and the D track own it and will want a say in whether it is a
  vendored dylib, a Homebrew dependency or a static build.
