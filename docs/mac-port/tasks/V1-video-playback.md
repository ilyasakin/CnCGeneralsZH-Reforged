# V1 — Video playback off Windows

- **Milestone:** M4 (the intro, the menu background and the campaign movies)
- **Depends on:** A1 (W3DDevice links on POSIX, done 2026-09-26); C4's upper half for the movies' sound
- **Blocks:** anything that plays a movie on macOS
- **Status:** done on macOS (-47, 2026-09-26): the movies decode and play their sound through C4's mix; not yet seen on screen (that needs A3's draws)
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

## Done (-47, 2026-09-26)

Design approved by the PM on 2026-09-26, and built in the order given: the FFmpeg script and vendor
step, then the reuse, the sound, the factory, and the test.

### What was built

- **The same two files as Windows.** `BinkVideoPlayer.cpp` had nothing Windows-only in it and builds
  unchanged. `bink_ffmpeg.cpp` guards its XAudio2 sites with `#if defined(_WIN32)`: the includes, the
  Movie struct's audio members, `openAudio`, `queueAudioFrame`, and the audio lines of `BinkClose`,
  `BinkGoto` and `BinkSetVolume`. Off Windows, `BinkOpen` resolves the engine's spelling of the path
  (`Data\Movies\x.bik`) through `PosixPath_Resolve`. `bink.h` defines `__stdcall` as empty off
  Windows. Nothing else in either file changed.
- **The factory.** `W3DGameClient::createVideoPlayer` and the `BinkVideoPlayer.h` include lost their
  `#if defined(_WIN32)`. `PosixW3DGameClient` existed only to answer that one factory with the
  engine's empty `VideoPlayer`, so it is removed, and `SdlGameEngine` makes a `W3DGameClient` (-18
  agreed, 2026-09-26). Under `-headless`, `VideoOn` is false, so `open()` answers NULL as before, and
  `replay_check` passes unchanged.
- **FFmpeg 8.1.2**, the release Windows' `dist/` is built from. `Tools/vendor.sh` `install_ffmpeg`
  keeps only the tarball (sha256 `464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c`)
  in `Libraries/Source/FFmpeg/`. The POSIX build's `ffmpeg_posix` custom command runs
  `Tools/ffmpeg-build-posix.sh`, which unpacks it into the build tree, configures, builds and installs
  `include/`, five static libraries and the LICENSE into `<build>/ffmpeg`, then deletes the unpacked
  tree. It reruns only when the tarball or the script changes. `vendor.ps1` skips it.
- **The configure line:** `--enable-static --disable-shared --disable-everything --disable-autodetect
  --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-network
  --disable-debug --enable-pic --disable-asm --enable-swscale --enable-swresample
  --enable-demuxer=bink --enable-decoder=bink,binkaudio_dct,binkaudio_rdft --enable-protocol=file`.
  There is no mp3 or wav: off Windows, miniaudio decodes the game's own audio (C4). The script
  checks each component and the "LGPL version 2.1 or later" line in configure's output.
- **The sound through C4's mix.** On Windows each movie opens an XAudio2 engine of its own, so the
  movies never went through Miles there. Off Windows they play through C4's device instead of a
  second one: `miles_miniaudio.cpp` gains a pushed-PCM voice (`MSS/mss_ex_pcm.h`: `AIL_ex_open_pcm`,
  `_queue_pcm`, `_flush_pcm`, `_set_pcm_volume`, `_close_pcm`), mixed by the same callback. It mixes
  as that XAudio2 voice did, left to left and right to right at the volume's gain (a mono stream to
  both; no movie is mono). A Miles voice sums its channels at a pan's constant-power gains instead.
  With no device (`-headless`, or no audio hardware) `AIL_ex_open_pcm` answers NULL and the movie
  plays silent, as a failed `XAudio2Create` does on Windows. A/V sync is Windows': wall-clock pacing,
  with audio queued as it is decoded.

### Measured (Mac arm64, shared machine at load 5-23)

| What | Value |
|:--|:--|
| Tarball kept per checkout | 11.7 MB |
| Unpacked source (transient, deleted after the build) | 110 MB |
| configure + make -j | 23 s + 7 s; 28.6 s through ninja |
| Kept in the build tree (`<build>/ffmpeg`) | 4.6 MB, of which 3.2 MB static libraries |
| A decoder linking all five libraries | 2.1 MB (roughly what the game grows by) |
| The install's movies | 70 in `zerohour/`, 594 MB, 1761 s of video, 52,603 frames |
| Decode of all 70 (video only, 8 threads, in the test) | 3.5 s |
| Decode + BGRA + audio of all 70, one core, C vs NEON | 141 s vs 128 s (12.5x real time) |

**Why `--disable-asm`.** It was measured over all 70 movies before choosing. The Bink decoder is
bit-exact between the NEON and C builds (the YUV planes hash identically). swscale's NEON yuv420p to
BGRA path is not the same arithmetic as its C path, so the BGRA differs. With the C paths every POSIX
machine converts to identical bytes, one golden table serves the Mac and Linux, and nasm is not a
build dependency. The cost is 10% of a decode that runs 12.5x faster than real time.

**Pruning the source was tried and rejected.** configure reads `libavfilter/allfilters.c`, and the
top Makefile includes every `tests/*/Makefile` and `doc/examples/Makefile`. A pruned tree fails to
build, and would need rework at every bump. Keeping only the tarball and deleting the unpacked tree
after the build saves more.

### Licence and linking

- configure reports "License: LGPL version 2.1 or later" for this line: no `--enable-gpl`, no
  `--enable-nonfree`.
- Linked **statically** off Windows; Windows keeps its DLLs. LGPL 2.1 section 6 is met because the
  whole game is GPL-3 with its source published, so anyone can relink it against a modified FFmpeg.
  `COPYING.LGPLv2.1` is installed beside the libraries as `LICENSE.txt`, and the version and the
  exact configure line are in `Tools/ffmpeg-build-posix.sh`.
- **Packaging note (owner: whoever builds the .app / Linux package, E2 and M5):** a shipped binary
  must carry FFmpeg's licence text (`<build>/ffmpeg/LICENSE.txt`) and a pointer to the source
  (`https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz`, the configure line above, and this repository
  for relinking). Dylibs were rejected: rpath and bundling cost, no licence gain.

### The test: `test_binkvideo`

Needs `ZH_GAME_DATA`; read only (rule 9); skips with 77 without it.

- Every one of the 70 movies in `zerohour/`, through the Bink API: Width, Height, Frames and the
  frame rate against the file's own header. The demuxer reports `nb_frames=0`, so Frames comes from
  `BinkOpen`'s duration arithmetic, and this is what proves that arithmetic. Every frame decodes,
  exactly Frames of them and none past the end.
- A golden FNV-1a hash of each movie's **middle** frame (`Tests/binkvideo_golden.inc`, keyed to 8.1.2
  and this configure line; `ZH_BINK_PRINT_GOLDEN=1` regenerates it). Not the first frame: 27 of the 70
  first frames are the same black picture, so a first-frame hash would not see a decoder change.
- Armed controls: 32 against 32R is exactly an R/B swap, 24 is 32 without alpha, and every golden frame
  has colour in it.
- The engine's spelling (`data\MOVIES\gc_background.BIK`) opens; a missing movie answers NULL.
- `BinkWait` never releases a frame early. This is a lower bound only, which load cannot make flaky.
- On the null backend with the capture on, EA_LOGO's sound is in C4's mix: 2.74 s of the 3.2 s
  audible, 2.36 s with left and right apart (a summing mix would make them equal), silent at volume 0.
  The capture is waited on by its size, not the clock.
- Planted bugs, each caught: Frames off by one; 32R not swapped; a changed chroma conversion (the
  golden); the voice summed instead of passed through; the volume ignored; no audio queued; the path
  not resolved; `BinkWait` releasing early. One plant, bilinear to bicubic, was not caught, and it
  should not be: same-size conversion does no scaling, so it is an equivalent mutant.

### What this cannot see

- The picture on screen. `BinkVideoPlayer::frameRender` uploads into `W3DVideoBuffer`, which needs
  A3's draws.
- Windows' pixels against these. Windows' FFmpeg runs x86 SIMD in swscale, so the on-screen BGRA
  differs by rounding; the decoded YUV is the same decode.
- A person hearing it: the capture proves the mix, not the speakers.
- Linux: the script and the CMake are POSIX, not Mac-specific, but nothing has built them there yet
  (the disk hold on linux-check).
