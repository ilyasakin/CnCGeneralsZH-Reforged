# Zero Hour Reforged on macOS, Linux and Windows on ARM64

This branch ports the Zero Hour engine off Windows without changing what the game computes. A match
recorded on one platform plays back to the same world checksum on every other, and the Windows x64 build
keeps its own renderer and platform layer. This file covers what the port adds, how to build and run it,
how it works, how it is tested, and what is still open. Every change a Windows build sees is listed,
with its verification status, in [docs/porting/windows-impact.md](docs/porting/windows-impact.md).

## What the port adds

| Platform | Toolchain | Renderer | Status |
|:--|:--|:--|:--|
| macOS arm64 (Apple silicon) | AppleClang, macOS 13 or later | the port's D3D9-shaped device on SDL3 GPU, Metal underneath | playable; packaged as `Zero Hour Reforged.app` |
| macOS x86_64 | AppleClang (Intel Macs, or Rosetta) | the same, on Metal | builds and runs; a universal2 app can be made at release time |
| Linux x86_64, including the Steam Deck | GCC or Clang, glibc 2.31 or later for the portable package | the same device, Vulkan underneath | playable; engine-only AppImage, `.deb`, `.rpm`, Arch package and Flatpak, and a portable folder |
| Linux arm64 | GCC or Clang | Vulkan | builds and passes ctest; no packaging yet |
| Windows x64 | MSVC (Visual Studio 2022) | unchanged: Direct3D 9, or the Direct3D 11 twin | unchanged, plus the port's fixes |
| Windows ARM64 | MSVC | `-d3d12` by default; `-d3d9` and `-dx11` start without `d3dx9_43.dll` but have no file textures | builds, passes ctest and E1 |
| Windows x64 and ARM64, `-d3d12` | MSVC; `ZH_D3D12`, on by default for ARM64 | `-d3d12`: the same POSIX device on SDL3 GPU's Direct3D 12 backend, in `zh_d3d12.dll` | built and drawn by default on ARM64, opt-in on x64 |

Also added: sound through miniaudio, text through FreeType, video through FFmpeg on every platform, LAN
play between any of the platforms, a crash reporter off Windows, gamepad support on every platform (see
[Gamepad](#gamepad)), an optional smooth-motion mode that draws units between logic ticks, and about 80 new
ctest tests.

## Building

Every platform first fetches the third-party sources EA stripped and the repository does not carry:
`GeneralsMD/Code/Tools/vendor.sh` (POSIX) or `vendor.ps1` (Windows). They are pinned; each is fetched once
and a second run does nothing. The build scripts call them for you.

### macOS

Needs the Xcode command line tools (`xcode-select --install`) and CMake 3.20 or later; Ninja is used when
it is installed.

```sh
./build.sh                    # vendor, configure and build Release into build-mac/
./build.sh Release test       # the same, then ctest
./build.sh Debug              # another configuration (Release, RelWithDebInfo, Debug)
./build.sh clean              # delete build-mac/ and start again
```

A machine-specific override (another CMake, generator or build folder) goes in `build.local.sh` beside
`build.sh`, which is git-ignored. The app bundle is a separate target, outside the default build:

```sh
ninja -C build-mac macos_app  # build-mac/Zero Hour Reforged.app, ad-hoc signed and verified
```

The app carries no upscaled art: like the Linux packages, it fetches it on its first start (see [Linux
packages](#linux-packages)). `ZH_APP_BUNDLE_ID` sets the bundle identifier, `ZH_ART_URL` the release the art is
fetched from, and
`ZH_X86_64_GENERALS=<an x86_64 build's generals>` makes the app universal2. The deployment target defaults
to macOS 13.0, and an API newer than the target is a build error.

### Linux

There is no build script for Linux yet; the steps are the same as the Mac's. On Ubuntu 24.04:

```sh
sudo apt-get install cmake ninja-build g++ python3 git pkg-config libx11-dev libxext-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
  wayland-protocols libegl-dev libdrm-dev libgbm-dev libvulkan-dev spirv-tools
GeneralsMD/Code/Tools/vendor.sh
cmake -S GeneralsMD/Code -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && (cd build && ctest --output-on-failure)
```

That package list is the one `GeneralsMD/Code/Tools/linux-check.sh` uses; that script builds and tests the
tree in containers ({gcc, clang} x {arm64, amd64}) from a Mac or a Linux host with Docker.

**The Steam Deck and other distributions.** `GeneralsMD/Code/Tools/linux-portable.sh --build <dir> --out
<dir> --cmake <Linux CMake 3.29+>` builds inside Valve's Steam Runtime 3 "sniper" SDK container and makes one
folder that runs on SteamOS and any desktop Linux of the last five years with nothing installed
(`--appimage` also makes an AppImage). It refuses a binary that needs a newer glibc, any shared libstdc++,
or a library whose licence is not listed. The folder's `README.txt` tells players how to add it to Steam.

#### Linux packages

`GeneralsMD/Code/Tools/linux-packages.sh` makes the Linux release files from one command, each step in docker:
an AppImage, a `.deb`, an `.rpm`, an Arch `.pkg.tar.zst` and a single-file `.flatpak`. It also makes the
portable folder as a `.tar.zst`, and `SHA256SUMS`. They are meant to be attached to a GitHub release; no
repository (Flathub, AUR, a PPA or COPR) is involved.

```
GeneralsMD/Code/Tools/linux-packages.sh --build <build folder> --out <out folder> \
  --cmake <Kitware's CMake 3.29+ for Linux>/bin/cmake --appimagetool <appimagetool> --runtime <type-2 runtime>
```

- **One binary in all five.** Each wraps the folder `linux-portable.sh --no-art` builds in Valve's Steam
  Runtime "sniper" SDK and checks. The checks: glibc 2.31 at most; libstdc++, SDL3, FFmpeg's LGPL-only build
  and the rest static; the licences checked against the link line. The `.deb` and `.rpm` depend only on glibc
  and fontconfig. What SDL and miniaudio load at run time (Vulkan, X11 or Wayland, ALSA or PulseAudio, D-Bus)
  is recommended, not required.
- **Layout.** The `.deb`, `.rpm` and Arch package install the folder unchanged in `/usr/lib/zero-hour-reforged`,
  with `/usr/bin/zero-hour-reforged`, a menu entry, icons and the licences. The Flatpak
  (`io.github.olcayseygan.ZeroHourReforged`, on org.freedesktop.Platform 25.08) has no filesystem permission.
- **No game files.** The first start finds the player's Zero Hour or asks for it. In the Flatpak it asks through
  the desktop portal's folder chooser, which shares only the folder chosen. A Zero Hour whose Generals is not
  inside or beside it is followed by a second question, for the Generals folder. That covers CD and First
  Decade installs, and the Flatpak's sandbox.
- **No art in the packages or the macOS app**, as on Windows. The launcher (the app's start, on macOS) fetches
  the upscaled `Reforged*.big` in the background into `ReforgedArt/` in the user data folder, from this
  repository's art-latest release, or from the release `--art-url` (`ZH_ART_URL` for the app) named at build
  time; `ZHR_ART_URL` overrides either at run time. Each file is used only once its sha256 matches art.json. The
  first start plays at the original textures; the next one has the art. When no art source can be reached it
  says so once, in `Logs/art-fetch.log`, and tries again on the next start. `ZHR_NO_ART_FETCH=1` turns it off.

`Tools/linux-packages-check.sh` proves the files on clean containers:
- **E1 at the three pins**, through the installed `/usr/bin/zero-hour-reforged`, on an install with only the
  required dependencies and no network;
- **one software-rendered frame** (Xvfb and Mesa's lavapipe) on a default install.

It passes on Debian 12, Ubuntu 24.04, Fedora, openSUSE Tumbleweed and Arch Linux, and for the AppImage. The
Flatpak's E1 passes on a Steam Deck, as a user installation run through `flatpak run`.

### Windows

Unchanged: double-click `build.bat`, or see [README.md](README.md). An ARM64 machine builds ARM64 into
`build-arm64`. `windows-ci.ps1` runs the whole Windows check in one command: the build, ctest, the GPU
tests in the desktop session, and E1's replay CRCs (`-DataDir`, `-ExpectCrc`), its runs side by side, each on its
own farm with its own user data folder.

**The `-d3d12` renderer.** A plain ARM64 build includes it: `ZH_D3D12` is on by default when the compiler
targets ARM64, and `build.bat` then runs `vendor.ps1 -D3D12`, which also fetches glslang, SPIRV-Cross and
SDL_shadercross. The build makes `zh_d3d12.dll` beside the exe, and the game draws through it unless `-d3d9` or
`-dx11` is given. On x64 (including an x64 build on an ARM64 machine) it is off: run
`GeneralsMD\Code\Tools\vendor.ps1 -D3D12`, configure with `-DZH_D3D12=ON`, and `-d3d12` on the command line
selects it. An explicit `-DZH_D3D12=ON` or `OFF` wins on either, and configure prints the value it chose. If
the DLL does not load, the old renderer draws and the log says why. It needs `d3dcompiler_47.dll` at run time.
A `ZH_D3D12` build, so every ARM64 build, also puts `d3d12shaders.shipped` beside the exe: 85 Direct3D 12 programs
recorded with Microsoft's compiler, which the device reads before compiling anything, so a first start
compiles almost nothing.

### Build options

| Option | Default | What it does |
|:--|:--|:--|
| `ZH_GAME_DATA` | empty | a folder holding a Zero Hour install's `zerohour/` (and `generals/`), for the tests that read game data; without it they skip |
| `ZH_D3D12` | `ON` for an ARM64 target, else `OFF` | Windows only: build the `-d3d12` renderer |
| `ZH_SANITIZE` | empty | Clang/GCC only: an `-fsanitize=` list; see [docs/porting/sanitizers.md](docs/porting/sanitizers.md) |
| `ZH_APP_BUNDLE_ID`, `ZH_ART_URL`, `ZH_X86_64_GENERALS` | see above | the macOS app |
| `ZH_PORTABLE_CMAKE` | empty | Linux: the CMake `linux_portable_check` runs inside the container |

## Running

The port does not include any game data. It needs an installed Zero Hour (Steam, EA App, CD or The First
Decade) with the original Generals it builds on. The engine finds the install in this order:

1. `-root <dir>` on the command line, used as given;
2. `InstallPath` in `Registry.ini` (the port's stand-in for the Windows registry, in the user data folder),
   if it still holds the game;
3. packaged builds only: the usual places (`~/Games`, `/Applications`, every Steam library, CrossOver and
   Whisky bottles), the first that holds the game;
4. packaged builds only: a folder chooser (SDL's native dialog) on the first launch. A folder without
   `INIZH.big` and the base game's archives is refused with the reason, and the choice is saved to
   `Registry.ini`, so the chooser appears once. A Zero Hour whose original Generals is not inside or beside
   it is followed by a second question, for the Generals folder, saved as `Registry.ini`'s Generals
   `InstallPath`. In the Flatpak the chooser is the desktop portal's, which shares only the folder chosen.
   The Steam Deck's Game Mode has no dialog, so the game asks for `-root` in Steam's launch options instead;
5. unpackaged builds: the executable's directory, as on Windows.

The fork's own data (its INIs, strings, windows and the upscaled art) is an **overlay** searched before the
install: inside the app bundle, beside the executable in the Linux package, or `-overlay <dir>` for a
development build (the `zh_overlay` target stages it into `<build>/overlay`). The install itself is never
written to: every root is read-only to the engine, and a write into it is refused and logged.

Settings, saves, replays and logs go to the user data folder: `~/Library/Application Support/Command and
Conquer Generals Zero Hour Data` on macOS, `$XDG_DATA_HOME` or `~/.local/share/Command and Conquer Generals
Zero Hour Data` on Linux. `ZH_USER_DATA_DIR` overrides it.

New switches, all platforms unless noted: `-hiddenwindow` (windowed, never shown), `-offscreen` (POSIX: no
window at all), `-mission <map> [easy|normal|hard]`, `-d3d12` (Windows), `-writableRoot` (POSIX, a test
harness's control, never a player's switch), and `-noDynamicLOD` and `-noaudio`, which are now accepted in
Release. `-nologo` and `-novideo` are honoured in Release only when `ZH_UNATTENDED` is set, so a player's
Release run still shows the EA logo. A hidden or offscreen run is silent unless `ZH_ALLOW_AUDIO=1`.
`ZH_UNATTENDED=1` (or `-headless`) makes the message boxes that could wait for a person print to stderr and exit
instead.

The corner readout (the render rate, the clock, the renderer and the frame; upstream's `ShowHudOverlay`) is off
by default in Release builds and on in Debug and `_INTERNAL` ones. `ShowHudOverlay = Yes` in GameData.ini turns it
on, and `-showHudOverlay` turns it on for one run whatever the INI says; the harnesses whose screenshots are
evidence pass it. Nothing shipped may force it on: the staged overlay and the packages refuse a
`ShowHudOverlay = Yes`.

## Gamepad

On macOS, Linux (including the Steam Deck) and Windows, the game can be played with a gamepad through SDL3's
gamepad API. Pads can be plugged in and out while the game runs; the last one used sets the button glyphs, and touching
the mouse or keyboard brings back the system cursor and the keyboard letters. The Options screen's Controls
page has **Controller** (on by default; off ignores every pad), **Controller Aim Assist** (on) and **Swap
Confirm and Cancel** (off).

- **Menus** are navigated console-style, without a pointer: the D-pad or the left stick moves the focus to the
  nearest control in that direction, confirm presses it, cancel presses the screen's Back or Cancel, Start
  presses the screen's main button, and the shoulder buttons switch tabs. A focus frame and a hint bar show
  where the focus is and what each button does.
- **Drop-down lists** work as on a console: confirm opens the list, the D-pad moves a highlight, confirm picks
  and cancel closes the list with nothing changed.
- **In a match**, the left stick moves the game's own cursor, with an aim assist that eases a resting cursor
  onto a nearby visible unit or building; the right stick scrolls the camera. A tap of the right trigger puts
  the command bar's 6x3 grid under the D-pad; holding it opens a radial command menu under the left stick.
  Cancel takes back a pending target, a building being placed or an armed attack move, and otherwise clears
  the selection. The bindings, including control groups, are in `Data/INI/GamepadReforged.ini`.
- **Confirm and cancel** follow each controller family's own layout: the bottom button confirms and the right
  one cancels on Xbox, PlayStation and Steam Deck controls, and on Nintendo pads A (right) confirms and B
  (below) cancels. Swap Confirm and Cancel reverses them.
- **Glyphs** come from Kenney's "Input Prompts" (CC0), with their own sets for Xbox, PlayStation, Nintendo and
  the Steam Deck; any other pad shows the Xbox set. `Tools/vendor.sh` and `vendor.ps1` download them and check the
  archive's hash and its CC0 licence; without them the hints show the keyboard letters.
- **A pad plays the same game as a mouse and keyboard.** `test_sdl_gamepad` checks that pad input produces the
  same mouse and key events as the hand input; `gamepad_crc_check` and `gamepad_combo_check` play a skirmish,
  and set one up, by pad and by hand, and require the same world CRC. On Windows `gamepad-check.ps1` runs the
  same checks in Git for Windows' bash, and they give the same CRCs as on macOS and Linux.
- **On Windows** the pads are SDL3's too, and what they press goes to Win32Device's own mouse and keyboard
  (`Win32GamepadOutput.cpp`); the window, the mouse, the keyboard and the message loop stay Win32's.

## How it works

**Platform layer.** Windows keeps `Win32Device`, DirectInput and Miles. Its gamepad is SDL3's joystick and
gamepad subsystems, feeding Win32Device's mouse and keyboard. Off Windows the window, events,
timers and entry point are SDL3 (`GameEngineDevice/*/SdlDevice`, `Main/PosixMain.cpp`); the file systems,
install discovery and crash handler are in `PosixDevice`; sound is the Miles API reimplemented on miniaudio
(`WWVegas/Miles6/miniaudio`), so `MilesAudioManager` compiles unchanged; text is rasterised by FreeType
behind the same seam GDI uses on Windows, with GDI's line metrics for the substitute fonts. CMake knows
`ZH_PLATFORM_WINDOWS` and `ZH_PLATFORM_POSIX`, with macOS and Linux only as refinements of POSIX.

**Renderer.** WW3D2 and W3DDevice are written in Direct3D 9: hundreds of its names and thousands of uses
outside `DX8Wrapper`. Rather than rewrite them, the port gives them a D3D9-shaped device of its own
(`GameEngineDevice/Source/PosixDevice/Render`, declared by `Libraries/Include/Platform/D3D9Posix.h`).
Resources live in CPU memory; draws resolve at draw time into cached SDL3 GPU pipelines, which are Metal on
macOS, Vulkan on Linux and Direct3D 12 under `-d3d12`. `-headless` makes the same device with no window and
no GPU, as Windows' `-headless` keeps a real device and skips only the frame. D3DX's arithmetic is the port's
own (`d3dx9portable.cpp`), checked bit for bit against Microsoft's `d3dx9_43.dll` by an oracle test.

**Scaled displays.** The game's sizes are pixels, as on Windows, where it is DPI-aware. The window asks SDL for
a high-density drawable, and a windowed resolution is divided by the monitor's scale to get SDL's points. So on
Wayland at 150 % or 200 %, or on a Retina Mac, a 1280x720 window is 1280x720 pixels, drawn one to one rather
than stretched. Checked on headless sway (x1.5) and weston (x2), with Xvfb as the control; clicks land on the
same game pixels.

**Shaders.** The fixed-function and engine shader generators (`ffvertex.cpp`, `ffshader.cpp`,
`engineshader.cpp`) still emit HLSL, one language for every target. Off Windows that text goes through
glslang's HLSL front end to SPIR-V for Vulkan, then SPIRV-Cross (through SDL_shadercross) to MSL for Metal
or to HLSL/DXBC for Direct3D 12. Programs are compiled ahead of need and cached. On Windows the generated
HLSL for Direct3D 9 and 11 is byte-identical to what it was, apart from the generator fixes listed below.

**Wide characters.** `WideChar` was `wchar_t`: 2 bytes on MSVC, 4 on Clang and GCC, which would change string
CRCs, network messages and replays. It is now `char16_t` on every platform (`Lib/WideChar.h`), with the
engine's own string functions in `Lib/WideCharFns.h` instead of the `wcs*` family. Non-ASCII in a literal is
written as a `\u` escape (`widechar_check` enforces it).

**LP64.** Windows is LLP64 and macOS and Linux are LP64, so `long` is 8 bytes off Windows. Wire and file
structures use fixed-width types, and `static_assert`s pin the layout of every packed network and W3D
structure, so a disagreement stops the build instead of desynchronising a game.

**Determinism.** The simulation must compute the same bits everywhere:
- `-ffp-contract=off`, `-fsigned-char` and `-fno-strict-aliasing` on every Clang and GCC build, keyed on the
  compiler (no FMA contraction, which MSVC never does).
- `Platform/MsvcFloatCasts.h`: a float converted to an integer out of its range is undefined in C++. MSVC x64
  lowers it to `cvttss2si`, and the engine relies on that answer; these helpers compute MSVC's result on every
  platform. `Lib/DetRound.h` does the same for the rounding conversions.
- D3DX's vector maths on the simulation path goes through the portable implementation on Windows too, because
  `d3dx9_43.dll` picks a different summation order on Intel and AMD CPUs (port defect 7).
- The multiplayer compatibility CRC hashes a **build fingerprint**, a CRC-32 over the tracked sources listed in
  `GeneralsMD/Code/BuildFingerprint.manifest`, instead of the executable's bytes, so identical sources agree
  across compilers and platforms.

**E1**, the replay check (`Tools/replay-check.sh`, `replay-check.ps1` on Windows), plays a headless skirmish,
plays its replay back and plays the seed again, and compares the three runs' world CRCs. The pinned results
are seed 0 at 1200 frames `0x43105931`, seed 0 at 12000 `0x3E744B9E` and seed 1 at 12000 `0x6CC0BDAD`, since
upstream bef14036 (v2.3.1) was merged; they are identical on macOS arm64, macOS x86_64 under Rosetta,
Linux x86_64 (GCC), Windows x64 and Windows ARM64 (MSVC; ARM64 from a Windows ARM64 CI run on the integrated
tree). Replays recorded on one platform play back on the others, and a two-host LAN match between macOS and
Linux keeps one world. Upstream gameplay data changes move the pins.

### Design decisions

1. Windows routes the simulation's D3DX maths through the portable implementation (port defect 7).
2. A CPU or GPU the engine cannot measure (Apple silicon, any Vulkan device) counts as meeting the top
   preset's requirement when the default detail level is chosen. POSIX only.
3. Linux is a target alongside macOS: SDL3 for the platform layer, miniaudio for sound, SDL3's GPU API for the
   renderer. Windows keeps its own, except SDL3 for the gamepad and the `-d3d12` renderer.
4. Shaders stay HLSL; SPIR-V and MSL are derived (glslang, SPIRV-Cross). Measured on all 49 generated
   programs. glslang has deprecated its HLSL front end (removal not before about 2027); the fallback is DXC
   through the same call.
5. The compatibility CRC hashes the build fingerprint instead of the executable.
6. Text is rasterised with FreeType off Windows, with metric-compatible fonts on Linux.
7. The renderer reaches POSIX through a D3D9-shaped device; Windows keeps Direct3D 9 and 11.
8. The POSIX engine uses the W3D factories, as Windows' own headless mode does.
9. The fork's data is an overlay the game mounts, never written into the player's install.
10. One vendored library is patched: SDL3 may make a Metal device with no window
    (`Libraries/Source/sdl3-metal-windowless.patch`, behind a hint), for machines with no window server.
11. Smooth motion: units are drawn between logic ticks, one tick behind. It changes the picture, never the
    game, and is on by default everywhere except Windows.

### Port rules

The comments cite these by number. Rule 2: platform differences go behind a named header with the reason in
its comment, not `#ifdef _WIN32` scattered through game code. Rule 3: nothing the simulation touches changes
behaviour; a change that moves a replay checksum is a bug. Rule 8: non-ASCII in a literal is a `\u` escape.
Rule 9: never start the engine with a real install as its root, because `GameEngine::init` deletes
`Data\INI\INIZH.big` from its root; tests run on a read-only farm of symbolic links (`Tools/install-guard.sh`
checks the install did not change). **Never define `_UNIX`**: it switches on Westwood's abandoned UNIX port
(`MSVCCompat.h` stops the build, `unix_define_check` checks the command lines). The only locale category the
game sets is `LC_TIME`.

Work items are named in comments by short labels: A (the POSIX device: A1 compiles it, A2 resources, A3
draws), B1 to B19 (portability: B1 wide characters, B2 clocks, B5 Win32 types, B7 W3D layout), C1 to C5
(POSIX engine, entry point, input, audio, crash reporting), D1 to D6 (renderer funnel, shaders, text), E1
(replay check), E3 (the x86_64/arm64 differential harness), L1/L2 (LAN, Vulkan), N1 (build fingerprint), P1 to
P3 (macOS app, macOS reach, Linux package), R1 (smooth motion), T1 (simulation terrain), V1 (video), W1/W2
(Linux, the first MSVC build), W-ARM64, X1 (`-d3d12`), X2 (Direct3D 11 measurements). M1 to M5 are the milestones: a headless build, a
headless game, the renderer funnel, a game that draws, and a game that ships.

## Testing

`ctest` in the build folder runs every suite: the engine's own (`test_gameengine`), the libraries', and the
port's (the POSIX device, the shader generators, the file systems, input, audio, LAN, packaging). With
`-DZH_GAME_DATA=<folder>` the data tests run too: `replay_check` (E1, including two 12000-frame matches),
`locomotor_check`, `mission_check`, `net_check` (two copies of the game in one LAN match), the install and
overlay checks and more. Automated runs never make a sound or show a window.

The renderer is checked against **FFReference** (`Tests/ffreference`), Direct3D 9's fixed-function pipeline
computed in double precision from Microsoft's documentation, written independently of the device. It judges
the game's own captured draws (`ffref_capture_selfcheck`, `ZH_GPU_CAPTURE`) and the device's draws on the
machine's GPU (`ffref_gpu_selfcheck`). Where the documentation was ambiguous, Windows' own Direct3D 9 (WARP
and the reference rasteriser) settled it (`knownprobe_windows.cpp`, `f7probe_windows.cpp`).

Other checks: `arch_differential` (one source built for arm64 and x86_64 and run under Rosetta, the
architecture half of determinism), `d3dx_oracle` (against Microsoft's DLL), `test_msvc_float_casts` (against
MSVC's own casts on Windows), `license-headers.py --check`, `fingerprint-manifest.sh --check`,
`include_case_check` and `widechar_check`.

## Known issues and limitations

- **Metal's 27 pixel differences.** Metal on Apple silicon forms its 2x2 derivative quads across an edge that
  two triangles of one indexed draw share, so a pixel beside that edge can take its neighbour's mip level.
  All 27 of Metal's disagreements with FFReference on the captured draw sets (243 pixels, worst 51/255) are
  this, and every one agrees when drawn a triangle per call. Measured on an M3 Pro only. Not fixed: the fix
  would draw every mesh unindexed.
- **`-d3d12` is the default on Windows ARM64 and opt-in on Windows x64.** On ARM64 it is the only renderer that
  draws the game correctly: there is no `d3dx9_43.dll` for ARM64, so the native Direct3D 9 path cannot. It runs
  the port's device over SDL3's GPU API on Direct3D 12 (`zh_d3d12.dll`, which a plain ARM64 build includes), with a
  vendored patch to SDL (`Libraries/Source/sdl3-d3d12-descriptors.patch`) that fixes a descriptor-heap overflow
  in SDL's Direct3D 12 backend. `-d3d9` and `-dx11` opt out of the default.
- **On Windows ARM64, `-d3d9` and `-dx11` draw incompletely**: there is no `d3dx9_43.dll` for ARM64, so they
  start but load no file textures.
- **A first `-d3d12` start can still compile a few programs** that no recording reached (a key seen in only one
  recording is left out of the shipped set). Each is compiled once and kept in the player's `d3d12shaders.cache`.
- **The upscaled art comes from the art-latest release, which is not published at the moment.** Until it is, the
  Linux packages and the macOS app play at the original textures and try again on each start. Once it is, the
  first start downloads the art in the background and the next start uses it.
- **The Flatpak in the Steam Deck's Game Mode**: there is no folder dialog there, and `-root` names a folder the
  sandbox cannot see. Start it once in Desktop Mode to choose the folder, or grant it:
  `flatpak override --user --filesystem=<folder> io.github.olcayseygan.ZeroHourReforged`. The app ID is a default,
  open to the maintainers' choice, as the macOS bundle identifier is.
- **The portal's folder dialog itself is untested**: it needs a person to click it. The chooser logic behind it
  is tested headless, the Generals question included.
- **An `_INTERNAL` build has not been built.** A Windows x64 Debug build compiles and passes ctest and E1 (seed 0
  at 1200 frames, Release's pin) since 2026-09-29; before that it did not compile, and did not before the port
  either, for a different reason.
- **Texture stage 2 is invisible to the Direct3D 11 backend** (suspected, by reading: `shader.cpp` sets it
  through raw `DX8CALL`). Upstream's code, not the port's; not fixed.
- **`TheGameResultsQueue` is published to its thread without synchronisation** (ThreadSanitizer; benign on
  x86, not on ARM in principle). Not fixed.
- **Windows verification is partial.** Of the rows in docs/porting/windows-impact.md, 155 are verified on
  Windows, 62 partly and 101 open (as of 2026-09-30). What is left needs checks run in a Debug build, someone at
  a Windows screen (menus, text, input), a network game between two Windows machines, GameSpy, an AMD machine,
  or old replays and saves.
- Retail replays (32-bit `time_t` in the header) are probably misread by both 64-bit builds; unverified.
- On a case-sensitive file system, a user data folder with a capital letter in its path may break the random
  map's path, which the engine lowercases whole.
- The app icon comes from `Main/Generals.ico`'s 48 px image, which is soft on a Retina display.
- The macOS app is ad-hoc signed, not notarised.

## Defects found while porting

Found by compiling with a second compiler, sanitizers and tests. "Fork" means introduced by this repository's
own changes, "upstream" by a specific upstream commit; the rest are in EA's release.

| # | Defect | Status |
|--:|:--|:--|
| 1 | Debug and `_INTERNAL` builds did not compile on x64 (live `__asm` in `PerfTimer.h`) | the `__asm` replaced; Debug still stops elsewhere |
| 2 | Double free in `AsciiString`'s reference count | fixed |
| 3 | `WWSaveLoad`'s pointer remapping never worked on x64 | fixed |
| 4 | An animation picks the wrong frame from frame 33 up | recorded |
| 5 | A non-ASCII player name could make an auto-saved replay's file name invalid | fixed by the wide-character work |
| 6 | Translated strings used as printf formats | recorded |
| 7 | Intel and AMD Windows machines compute a shell's flight path differently (`d3dx9_43.dll`) | fixed (decision 1) |
| 8 | A malformed font string in a map script reads past a stack buffer | kept, as Windows compiles it |
| 9 | Replay error boxes overstated their buffer to `FormatMessageW` | fixed |
| 10 | The score screen's "add buddy" always asks for profile 0 | recorded (GameSpy is gone) |
| 11 | A find handle opened and never closed | fixed |
| 12 | Writing a file whose last name has no `.` spins until `AsciiString` throws | fixed off Windows |
| 13 | A staging-room stats message sent a string's address | fixed |
| 14 | A 3D turn toward a goal exactly behind does not turn | recorded |
| 15 | A truncated replay header reads as 1023 U+FFFF characters | kept |
| 16 | With the save folder missing, the save list read the game folder and cleanup deleted its `.map` files | fixed |
| 17 | Fork: the logic catch-up let the water grid the simulation reads fall behind | fixed |
| 18 | A screenshot reads past its image when the width is not a multiple of 8 | fixed |
| 19 | Fork: a random map smaller than the generator's sizes can put two starts together | recorded |
| 20 | Fork: saving nudged every object's heading, so checkpointed replays diverged | fixed |
| 21 | Fork: under Direct3D 11 a scrolling texture does not scroll | fixed |
| 22 | Fork: generated vertex programs ignored `D3DRS_LOCALVIEWER` | fixed |
| 23 | Fork: under Direct3D 11 a light's own ambient colour is dropped | fixed |
| 24 | Fork: a generated `DOTPRODUCT3` stage did not write alpha | fixed |
| 25 | Fork: generated pixel programs never added the specular colour (Direct3D 9 and 11) | fixed |
| 26 | Fork: under Direct3D 11 a mesh's second vertex colour is ignored | fixed |
| 27 | Fork: a stage that generates coordinates or reads another stage's set sampled the wrong ones | fixed |
| 28 | A player's rank walks off its table on remote stats data | fixed |
| 29 | Fork: a POSIX LAN lobby hears no broadcasts | fixed |
| 30 | A map's water-track file, which a host can send, indexes past the wave table | fixed |
| 31 | The port: a cloned particle emitter `strdup`s NULL and the fog of war crashes off Windows | fixed |
| 32 | A dangling iterator in `LocomotorStore::reset` crashes or hangs a map with its own locomotor | fixed |
| 33 | Upstream (fbe8dc6f, 179e1f65): a `ReplaceModule` of an AI module dropped units' locomotors | fixed upstream too |
| 34 | Upstream (fbe8dc6f): a module added to a reskin erased its copied modules | fixed |

Unnumbered, all fixed: a failed `UDP::Bind` leaked its socket; a challenge with no load movie read through
NULL; the game crashed when the drive holding its data went away (now it says so and exits); `Player::init`
wrote into a freed list node after every match; the debug library's crash box waited forever under
`-headless`; `d3d9.dll` was unloaded while textures still used it, which crashes on exit under Proton.

## Licence

See [NOTICE.md](NOTICE.md) and [LICENSE.md](LICENSE.md). Files the port added carry `Copyright 2026 İlyas
Akın` and GPL-3.0-or-later. Files EA released that the port changed keep EA's header exactly, with a
`Modified 2026 by İlyas Akın for the macOS/Linux port` line under it. `GeneralsMD/Code/Tools/license-headers.py
--check` verifies every file against these rules. Third-party libraries are fetched at pinned versions and keep
their own licences; the port's patches to them are the `Libraries/Source/*.patch` files.
