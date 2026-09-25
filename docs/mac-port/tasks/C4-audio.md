# C4 — Audio

- **Milestone:** M5
- **Depends on:** C2
- **Blocks:** E2
- **Status:** in progress: lower half (the Miles API on miniaudio) in review, -a9; upper half open
- **Size:** `MilesAudioManager.cpp` is 3,637 lines; the XAudio2 implementation under it is
  `Libraries/Source/WWVegas/Miles6/xaudio2/miles_xaudio2.cpp`

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** Step 2 is decided: **miniaudio**, vendored through `Tools/vendor.sh` like litehtml and nanosvg. `AVAudioEngine` is rejected as macOS-only. The reasons are in the plan.

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

## Lower half: the Miles API on miniaudio (-a9, 2026-09-25)

`Libraries/Source/WWVegas/Miles6/miniaudio/miles_miniaudio.cpp` implements the same `AIL_*` surface
as `miles_xaudio2.cpp`, function by function and in the same order, on miniaudio 0.11.25. It builds
as `milesaudio` under `ZH_PLATFORM_POSIX` only, the same library name the Windows build uses.
`MilesAudioManager.cpp` is unchanged (`git diff` on it is empty). Nothing links it into the game
yet: `gameenginedevice` is still Windows-only, and that is the upper half.

### The contract

`MilesAudioManager.cpp` names 72 `AIL_` identifiers:

- **65 functions.** Every one of them is implemented in both backends.
- **6 speaker constants** (`AIL_3D_2_SPEAKER` ... `AIL_3D_71_SPEAKER`), which come from `MSS.h`.
- **`AIL_MSS_version`**, a macro in `MSS.h`. Off Windows it writes "Miles 6.5 surface on miniaudio".

`miles_xaudio2.cpp` implements 70 functions. The five that the manager does not call are
`AIL_lock`, `AIL_unlock`, `AIL_end_sample`, `AIL_end_3D_sample` and `AIL_set_3D_velocity_vector`.
Their only other callers are WWAudio and WPAudio, which are not built. They are implemented anyway,
so the two backends have the same 70.

| Group | Functions (calls in the manager) | On miniaudio |
|:--|:--|:--|
| Startup | `AIL_startup` (1), `AIL_quick_startup` (2), `AIL_quick_handles` (1), `AIL_shutdown` (1), `AIL_set_redist_directory` (1), `AIL_get_timer_highest_delay` (1), `AIL_set_file_callbacks` (1), `AIL_lock`/`AIL_unlock` (0) | `ma_context` + `ma_device`: f32 stereo at the device's own rate, plus a `std::thread` service loop at 10 ms. The lock is a `std::recursive_mutex`, like the `CRITICAL_SECTION` it stands in for. The timer delay is 10, as on XAudio2. |
| Capture | `AIL_ex_start_capture` (1), `AIL_ex_stop_capture` (1) | The finished mix is written as 16-bit WAV from the device callback. The header sizes are refreshed every second. This is the same tap and format as XAudio2's mastering-voice tap. |
| Providers, filters | `AIL_enumerate_3D_providers` (1), `AIL_open_3D_provider` (3), `AIL_close_3D_provider` (1), `AIL_enumerate_filters` (1), `AIL_set_3D_speaker_type` (1), `AIL_set_sample_processor` (1), `AIL_set_filter_sample_preference` (3), `AIL_get_DirectSound_info` (2) | Identical to XAudio2: the same two provider names, no filters, NULL DirectSound, and no-ops. |
| 2D samples | `AIL_allocate_sample_handle` (1), `AIL_release_sample_handle` (1), `AIL_init_sample` (2), `AIL_set_sample_file` (1), `AIL_start_sample` (1), `AIL_stop_sample` (3), `AIL_resume_sample` (1), `AIL_end_sample` (0), `AIL_set_sample_volume_pan` (4), `AIL_sample_volume_pan` (2), `AIL_set_sample_playback_rate` (1), `AIL_sample_playback_rate` (1), `AIL_set_sample_user_data` (1), `AIL_sample_user_data` (1), `AIL_register_EOS_callback` (3) | A voice plays the caller's PCM image in place, through its own `ma_resampler`, and is mixed in the device callback. The rate ratio is clamped to [1/1024, 4], as XAudio2's voices are. Accepts PCM only, like XAudio2: the manager runs ADPCM through `AIL_decompress_ADPCM` first. |
| 3D samples | `AIL_allocate_3D_sample_handle` (1), `AIL_release_3D_sample_handle` (1), `AIL_set_3D_sample_file` (1), `AIL_start_3D_sample` (1), `AIL_stop_3D_sample` (3), `AIL_resume_3D_sample` (1), `AIL_end_3D_sample` (0), `AIL_set_3D_sample_volume` (4), `AIL_set_3D_sample_playback_rate` (1), `AIL_3D_sample_playback_rate` (1), `AIL_set_3D_sample_distances` (2), `AIL_set_3D_sample_occlusion` (1), `AIL_set_3D_user_data` (1), `AIL_3D_user_data` (1), `AIL_register_3D_EOS_callback` (5) | The same voice. Gain and pan come from XAudio2's own formula: linear falloff between the minimum and maximum distance, times (1 − occlusion), and a pan from the source's projection onto right = up × face. It is recomputed on every service tick. miniaudio's spatializer is not used. |
| Listener | `AIL_open_3D_listener` (1), `AIL_close_3D_listener` (1), `AIL_set_3D_position` (3), `AIL_set_3D_orientation` (1), `AIL_set_3D_velocity_vector` (0) | Plain state, as in XAudio2. There is no Doppler effect, in either backend. |
| Streams | `AIL_open_stream` (2), `AIL_close_stream` (2), `AIL_start_stream` (1), `AIL_pause_stream` (3), `AIL_set_stream_loop_count` (1), `AIL_stream_loop_count` (1), `AIL_set_stream_volume_pan` (5), `AIL_stream_volume_pan` (2), `AIL_stream_ms_position` (1), `AIL_register_stream_callback` (3) | `ma_decoder` over a VFS built from the game's file callbacks, decoded to s16 at the file's own rate with at most two channels. The service thread keeps four 200 ms chunks queued ahead. A loop count above 1 rewinds. The position counts frames played, and a loop does not reset it. Open happens outside the engine lock, as XAudio2 opens outside it. |
| Quick | `AIL_quick_load_and_play` (1), `AIL_quick_unload` (1), `AIL_quick_set_volume` (1) | A stream. A loop count of 0 means 1, as in XAudio2. |
| Formats | `AIL_WAV_info` (1), `AIL_decompress_ADPCM` (2), `AIL_mem_free_lock` (2) | XAudio2's RIFF reader and IMA ADPCM decoder, copied verbatim. |

Threading is the same as XAudio2's. End-of-sample and end-of-stream callbacks come from the service
thread, outside every lock. That is the contract `26bc0c45` (finish a sound on the main thread)
was written against: the manager queues the handle and deals with it in `update()`. The device
callback takes only a mix lock and never the engine lock, so a game holding `AIL_lock` cannot stall
the audio thread.

### Where miniaudio forced a choice

Each of these is also commented at its site in `miles_miniaudio.cpp`.

1. **Resampling is miniaudio's linear resampler with its default low-pass. It is not XAudio2's
   converter.** This is the one place where the output is knowingly different samples. Pitch is
   exact: `playback_rate_is_pitch` measures it. The difference is in how much aliasing gets through
   when a 22 kHz sound is played up to a 48 kHz device. miniaudio has no higher-order resampler,
   short of linking a third library. If ears on `miles_listen` disagree, this is the knob.
2. **MP3 is decoded by dr_mp3 (inside miniaudio), not FFmpeg.** This was measured on C_Chix01.mp3.
   FFmpeg 9.0.2 and miniaudio both give 4,593,024 frames, and the samples agree within 2 LSB from
   frame 0.
3. **An MP3 has to be opened through a VFS with tell and size, not through `ma_decoder_init`'s
   read/seek pair.** Without tell, dr_mp3 cannot find the end of the file when it opens it, so it
   cannot see the 128-byte ID3v1 tag at the end of 54 of the 56 music tracks, and it loses the last
   frame (26 ms). Through read/seek C_Chix01 gives 4,591,872 frames; through the VFS it gives
   4,593,024, which is FFmpeg's count. `vfsTell` is the game's seek with a move of zero; `vfsInfo`
   seeks to the end and back. Both are the way FFmpeg's AVIO context used the same callbacks with
   `AVSEEK_SIZE`.
4. **A cut-off last frame is dropped, where FFmpeg decodes it.** USA_09.mp3's last frame is 436
   bytes short of its header's length (87 of 523 bytes are there). FFmpeg decodes it anyway:
   194.2467 s, 8,566,272 samples. dr_mp3 drops it: 194.220 s. That is 26 ms of the last frame's
   partial data, at the end of a music track. The other 55 tracks end on a whole frame and match
   exactly.
5. **A stream's length is exact and computed on first ask**
   (`ma_decoder_get_length_in_pcm_frames`, truncated to milliseconds). For MP3, FFmpeg's
   `AVStream.duration` could be an estimate. The manager reads the length once, at line 2842, to
   time music.
6. **The device is stereo f32 at the device's native rate.** XAudio2's mastering voice takes the
   device's own channel count and feeds only front left and right, which is what
   `applyPan` writes. Off Windows the OS receives stereo and places it on those two speakers. On a
   5.1 or 7.1 output the audible result should be the same. This has not been heard on one.
7. **A stereo source folds to mono before the pan, as XAudio2's matrix does.** It is kept on
   purpose. `applyPan` puts every source channel into both speakers at the same gain, so a stereo
   file at centre pan plays (L+R)·0.707 on each side. That is not stereo, but it is what the
   Windows build plays today. `a_stereo_source_is_summed_into_both_speakers` pins it. Changing it
   is a decision for both backends, not a port fix.
8. **The resampler's tail.** A voice counts as finished when its source is dry and the resampler has
   taken every input frame. The last fraction of a frame still inside the linear filter's history is
   not flushed. That is under one output frame, about 0.02 ms, and nothing can hear it.
9. **`ZH_AUDIO_BACKEND=null`** picks miniaudio's null backend. That is how the tests run the real
   mix, in real time, with no audio hardware. It is read once at `AIL_quick_startup`. Without it,
   miniaudio picks the platform's default: Core Audio on macOS, PulseAudio or ALSA on Linux.
10. **`S32`/`U32` stay `long` on LP64, so they are 64 bits there.** `MilesAudioManager.cpp:2842`
    passes a `long *` to `AIL_stream_ms_position`, and that file is not to change. This is safe
    because the API never leaves the process, and nothing that goes through it is a file or
    network format. The comment is in `MSS.h`.
11. **Shutting down without `AIL_shutdown`.** XAudio2's service thread is a `HANDLE`. Here it is a
    `std::thread`, which calls `std::terminate` if it is still joinable when it is destroyed. The
    engine's destructor stops the thread and releases the device. Without that, a process that exits
    without `AIL_shutdown` would abort. The tests caught this.

### Tests

The tests run on the null backend in ctest. Each one listens to the real mix through
`AIL_ex_start_capture` and measures what XAudio2's formulas predict.

- **`miles_smoke`**: the same file Windows runs. It is guarded so that off Windows it has local
  shims for the five Windows calls it makes. The Windows view is byte-identical; see the
  WINDOWS-DEBT row.
- **`test_miles_miniaudio`**: 13 tests, 47 checks:
  - **Level and pan:** constant-power pan (centre, hard left and right, the square-root law at a
    quarter pan), linear volume, and the stereo fold (item 7). Each is within 3% of a 440 Hz
    reference tone's RMS.
  - **Rate:** the playback rate as pitch, measured by zero crossings.
  - **3D:** linear falloff at the midpoint of the distances, occlusion, full gain inside the
    minimum distance, and silence beyond the maximum while the sample still ends and calls back.
  - **Streams:** a loop count of 3 plays three times, the position keeps counting through the loops
    (1200 ms ±5%), and the loop count reads back as 1 at the end.
  - **Every WAV in the game's archives**, read from `ZH_GAME_DATA` (read only): 5,346 PCM and 3,236
    IMA ADPCM files across 8 archives. `AIL_WAV_info` against dr_wav's header gives 0 header
    mismatches. Sample-for-sample, `AIL_decompress_ADPCM` against dr_wav's decode of 549,618,610
    ADPCM frames gives 0 mismatches. That validates the decoder the Windows backend shares.
  - **Every music MP3 (56)**: the stream length matches the file's own frame headers to the
    millisecond. The check covers MPEG-1 and MPEG-2 layer III, the Xing/LAME delay and padding,
    and whole frames only.
  - **Speech:** an ADPCM WAV (`dxxoc001.wav`) streams through the game's file callbacks. Its length
    equals its header's (17,124 ms). After 400 ms of wall time its position is at least 200 ms; it
    measures 394 to 406 ms.

  Without `ZH_GAME_DATA` the data tests print "skip" and pass, as `bink_smoke` does.
- **`miles_listen`** (not in ctest) plays one sound from a `.big` file, or a loose file, on the real
  device at a chosen volume and pan, through the same API. It is for putting the port next to
  Windows by ear. **Nobody has listened to it yet.** It has been run on Core Audio (macOS 27, the
  default output) at volume 0, so the device ran silently:
  - `addnwi1a.wav`, a sample of 72,443 frames at 22,050 Hz (3.29 s), reached its EOS callback.
    The whole run took 4.15 s.
  - `End_Chif.mp3`, a stream of 32,600 ms, reached its end-of-stream callback. The whole run took
    33.07 s.

Results:

| Platform | Compiler | Audio targets | `miles_smoke` | `test_miles_miniaudio` |
|:--|:--|:--|:--|:--|
| macOS 27.0 arm64 | Apple clang 21.0.0 | 0 warnings | pass | 13/13, 47 checks |
| Linux arm64, ubuntu:24.04 | GCC 13.3.0 | 1 warning (below) | pass | 13/13, 47 checks |
| Linux arm64, ubuntu:24.04 | Clang 18.1.3 | 0 warnings | pass | 13/13, 47 checks |
| Linux amd64 (Rosetta), ubuntu:24.04 | GCC 13.3.0 | 1 warning (below) | pass | 13/13, 47 checks |

Every row read the game data and printed the same numbers:

- 5,346 PCM and 3,236 ADPCM WAVs, with 0 header, length or sample mismatches.
- 56 of 56 MP3 streams match to the millisecond.
- The speech stream is 17,124 ms long.

Each row passed in ctest as well: `ctest -R miles`, 2 of 2. The Linux runs were also run directly,
so that their output could be read.

The full macOS ctest is 22 of 22, with `test_gameengine` Disabled and `d3dx_oracle` Skipped as
before.

On Linux the rest of the tree does not build yet on this base: 85 failed edges on arm64/clang, in
wwdebug, wwutil and wwmath. -47's `feature/mac-port-linux` fixes those and has not merged, so its
`linux-check.sh` was not used. The rows above come from ubuntu:24.04 containers built with its
package list. They build from a case-sensitive copy of git's file list, with the game data mounted
read only.

GCC's one warning is `-Wformat-truncation` at `miles_smoke.cpp:210`. It is the shared `snprintf` of
the temporary path, which is bounded and truncates safely. It was left alone because Windows
compiles that line.

### What the lower half does not cover

- **Linking into the game.** `gameenginedevice` off Windows, and the Done-when skirmish.
- **Video sound.** `bink_ffmpeg.cpp` links FFmpeg and XAudio2 together, and on POSIX that belongs
  with the video task, together with step 3's FFmpeg question.
- **Real hardware.** Every ctest result comes from the null backend. Core Audio has been reached by
  the two silent `miles_listen` runs above. PulseAudio and ALSA have never been reached: the
  containers have no sound device.
- **Listening.** Nobody has compared the port's output with Windows by ear. That is what
  `miles_listen` is for, and deviation 1 is where a difference would show.
