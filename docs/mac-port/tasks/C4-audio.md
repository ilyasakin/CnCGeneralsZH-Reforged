# C4 — Audio

- **Milestone:** M5
- **Depends on:** C2
- **Blocks:** E2
- **Status:** not started
- **Size:** `MilesAudioManager.cpp` is 3,637 lines; the XAudio2 implementation under it is
  `Libraries/Source/WWVegas/Miles6/xaudio2/miles_xaudio2.cpp`

## Why

The retail game used the Miles Sound System, a binary-only 32-bit DLL a 64-bit process cannot load.
This fork kept `MilesAudioManager.cpp` unchanged, still calling `AIL_*`, and wrote those functions
over XAudio2 underneath. That is a good structure for a port: the Mac work is a third
implementation of the same `AIL_*` surface, not a rewrite of the audio manager.

## Scope

- `Libraries/Source/WWVegas/Miles6/xaudio2/miles_xaudio2.cpp` — the XAudio2 implementation, and the
  file to mirror
- `Libraries/Source/WWVegas/Miles6/include` — the reconstructed `AIL_*` declarations, which stay as
  they are
- `Tests/miles_smoke.cpp` — the existing check, which should end up running on both
- `GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp` — **do not modify**

Also here: `bink_ffmpeg.cpp` links FFmpeg and XAudio2 together for video sound, so the video player
has an audio dependency that comes along with this task.

## Do

1. Read `miles_xaudio2.cpp` first and enumerate the `AIL_*` functions it implements. That list is
   the contract.
2. Pick an implementation and say why on this task. `AVAudioEngine` is the native answer;
   `miniaudio` is a single-header cross-platform one that would also serve a future Linux port and
   costs a vendored dependency. Either is defensible. Record the reason.
3. FFmpeg on macOS is A2's deferred question. Resolve it here: vendored dylibs in the app bundle is
   the likely answer for a shipping build, since a Homebrew dependency is not something a player
   should need.
4. `miles_smoke` and `bink_smoke` are the checks. Both currently run from `Run/` and print "skip"
   without game data; keep that behaviour.

## Done when

`miles_smoke` passes on macOS. A skirmish plays with unit speech, weapon effects, music and
video sound, and `.bik` movies play with audio.

`MilesAudioManager.cpp` is unchanged — check the diff and make sure.

## Do not

- Do not modify `MilesAudioManager.cpp`. Three thousand six hundred lines of EA's audio logic that
  currently works is not what you want in the diff of an audio backend port. If it genuinely cannot
  be satisfied by the `AIL_*` surface, that is a finding to raise here first.

## Notes

`fix(milesaudio): finish a sound on the main thread, not the audio thread` is a recent commit in
this tree. Read it before starting — whatever threading assumption it fixed will exist in the Mac
backend too, and the fix tells you where the sharp edge is.
