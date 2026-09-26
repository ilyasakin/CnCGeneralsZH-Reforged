# V1 — Video playback off Windows

- **Milestone:** M4 (the intro, the menu background and the campaign movies)
- **Depends on:** A1 (W3DDevice links on POSIX, done 2026-09-26); C4's upper half for the movies' sound
- **Blocks:** anything that plays a movie on macOS
- **Status:** not started
- **Size:** `Bink/ffmpeg/bink_ffmpeg.cpp` (the fork's Bink API over FFmpeg), `VideoDevice/Bink/BinkVideoPlayer.cpp`,
  one vendored library

## Why

Found at A1's checkpoint on 2026-09-26. `W3DGameClient` leaves `createVideoPlayer` to the platform
off Windows, and no task owned video. On Windows the game's `.bik` movies play through the fork's own
implementation of the Bink API, `Bink/ffmpeg/bink_ffmpeg.cpp`, which decodes with FFmpeg (its
`binkvideo` and `binkaudio` decoders), linked from a prebuilt Windows FFmpeg (`Libraries/Source/FFmpeg/dist`).

## Do

1. Reuse `bink_ffmpeg.cpp` and `BinkVideoPlayer.cpp` on POSIX, as decision 8 reuses the W3D
   classes. Guard only what is Windows-specific, and keep the Windows view identical.
2. Vendor FFmpeg from source through `Tools/vendor.sh` at a pinned release, built with the SMALLEST
   configuration the movies need: the Bink demuxer, the `binkvideo` and `binkaudio` (RDFT and DCT)
   decoders, and whatever swscale or pixel conversion `bink_ffmpeg.cpp` uses. No network, no
   programs, no GPL or non-free parts. Record the licence (LGPL) and how it is linked. Measure the build
   time and size, because FFmpeg is the largest dependency so far.
3. Movie sound goes through C4's audio path, however `bink_ffmpeg.cpp` hands audio to Miles on Windows.
4. A test that decodes every `.bik` in the install (read-only, rule 9) and checks each one's frame
   count, dimensions and duration against its own header. It also hashes the first frame's pixels, so
   an FFmpeg bump that changes decoding shows up.
