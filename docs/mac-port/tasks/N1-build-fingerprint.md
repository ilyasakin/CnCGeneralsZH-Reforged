# N1 — Cross-platform build fingerprint for the compatibility CRC

- **Milestone:** M5 (cross-platform LAN play needs it; M2's replay-CRC gate does not)
- **Depends on:** nothing
- **Blocks:** a Mac or Linux build joining a Windows LAN game
- **Status:** done (-47, 2026-09-26); see "Result" below
- **Size:** one CMake generation step, one header, one hunk in `GlobalData.cpp`

## Why

Decision 5 in `docs/mac-port/README.md`. `GlobalData.cpp` (the "lets CRC the executable!" block)
folds the running executable's bytes into `m_exeCRC`, which LAN matchmaking (`LANAPI.h`), GameSpy
staging (`StagingRoomGameInfo.h`, `PeerThread.h`) and the replay header (`Recorder.cpp:618/906/1085`)
compare. No non-Windows binary can match `generals.exe`'s bytes.

## Do

1. Generate `BuildFingerprint.h` at BUILD time (not configure time only): a 32-bit value, the
   engine's own `CRC` class or a documented hash, over the content of every tracked source file
   under `GeneralsMD/Code`, in a fixed order (sorted by path, with `/` separators and
   case-sensitive names), independent of line endings (normalise CRLF, because a Windows checkout
   with autocrlf must produce the same value as a Mac one). The generated header must be rebuilt
   when any source changes.
2. Replace the executable-bytes term in `GlobalData.cpp` with the fingerprint on EVERY platform.
   Keep the version and the two `.scb` script terms, in their current order.
3. A ctest proving that two checkouts of the same commit produce the same fingerprint, one with
   LF and one with CRLF line endings, and that a one-byte change to any source changes it.
4. A `WINDOWS-DEBT.md` row: `m_exeCRC` changes on Windows, and old replays show "different
   executable" on playback, as they would after any rebuild.

## Do not

- Do not hash the platform's own sources only. The point is that Windows and Mac agree.
- Do not include build-directory or vendored files whose content differs by platform or by
  vendoring run.

## Result (-47, 2026-09-26)

**The value.** `Tools/fingerprint/build_fingerprint.cpp`, a host tool the build compiles, writes
`<build>/generated/fingerprint/BuildFingerprint.h` (`ZH_BUILD_FINGERPRINT`).
- The hash is CRC-32 (IEEE 802.3, zlib's) over, for each listed file in manifest order: its path, a
  NUL, its content with every run of CRs directly before an LF removed, and a NUL.
- `GlobalData.cpp` feeds those four bytes to the engine's `CRC` where the executable's bytes went, on
  every platform. The version and the two `.scb` terms follow, in their old order.
- Why CRC-32 and not the engine's `CRC`: the engine's is a rotate-and-add, where reordered bytes can
  collide. CRC-32 is documented and detects any single-byte change.
- Line endings: the normalisation drops CR *runs*, not one CR, because the tree holds `\r\r\n`. With
  one CR dropped, the normalisation was not idempotent, and the first check caught LF and CRLF copies
  disagreeing.

**"Tracked", without git at build time.** A release tarball has no `.git`, so the list is committed:
`GeneralsMD/Code/BuildFingerprint.manifest`.
- It holds paths relative to `GeneralsMD/Code`, with `/` separators, sorted bytewise. `.gitattributes`
  keeps it LF.
- `Tools/fingerprint-manifest.sh` rewrites it from `git ls-files`; run it after adding, removing or
  renaming a file.
- ctest `build_fingerprint_manifest` fails when it is stale, and skips without git.
- A listed file that is missing stops the build: that is not the same source.
- Left out: `Libraries/Source/FFmpeg/dist/`, which `Tools/ffmpeg-build.sh` rebuilds on Windows, so a
  vendoring run changes its bytes. Also the manifest itself.
- Vendored libraries whose content is downloaded (SDL3, glslang, litehtml, ...) are untracked, so they
  are not listed.

**What it cannot see:**
- A file added to the tree but not to the manifest. The ctest catches that wherever git is.
- Local edits to a vendored library outside the tracked set.
- A binary modified after the build. That is the part decision 5 gives up.

**Checks** (ctest `build_fingerprint_check`, 5 s):
- the build's header holds the value over the tree;
- an LF copy and a CRLF copy of all 3,700 files (made as autocrlf makes them; files with a NUL in their
  first 8000 bytes are left binary) give the same value;
- one byte of `GlobalData.cpp` changed gives another value, and restoring it restores the value;
- the same bytes under another name give another value.
Armed: with the normalisation switched off, the LF and CRLF copies disagree.

**Build cost:**
- A no-op rebuild: `ninja: no work to do`, 0.23 s.
- Touching a listed file without changing it re-runs the fingerprint (0.9 s over 64 MB), leaves the
  header as it was, and recompiles nothing.
- A one-byte change re-runs it and recompiles `GlobalData.cpp` alone.

**Replays:**
- The game-state CRCs are unchanged: seed 0 at 12000 frames 0xE896DEF3, seed 1 0x7C7DBA69.
- Playback still accepts its own recordings (replay_check passes), since the header's exe CRC matches
  within a build.
- On Windows, `m_exeCRC` changes (WINDOWS-DEBT), as it already did with every rebuild.
