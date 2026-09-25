# N1 — Cross-platform build fingerprint for the compatibility CRC

- **Milestone:** M5 (cross-platform LAN play needs it; M2's replay-CRC gate does not)
- **Depends on:** nothing
- **Blocks:** a Mac or Linux build joining a Windows LAN game
- **Status:** not started
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
