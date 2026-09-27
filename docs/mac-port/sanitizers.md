# Sanitizer builds: `ZH_SANITIZE`

A build for finding memory and threading bugs with AddressSanitizer, ThreadSanitizer or UBSan. It is never
used for playing, timing or E1 evidence. The PM approved it on 2026-09-27, after the whole game could not
run under a sanitizer at all.

## How to use it

```sh
cmake -S GeneralsMD/Code -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DZH_GAME_DATA=... -DZH_SANITIZE=address
ninja -C build-asan generals zh_overlay zh_overlay_dev
ASAN_OPTIONS=detect_leaks=0 ./build-asan/generals -root <rule-9 farm> -headless ... -maxframes 12000
```

- `ZH_SANITIZE` takes a `-fsanitize=` list: `address`, `thread`, `undefined`, or combinations such as
  `address,undefined`.
- It is read only in `CMakeLists.txt`'s Clang/GNU branch. It adds the flags everywhere, vendored libraries
  included, adds `-fno-omit-frame-pointer`, and defines `ZH_SANITIZER_BUILD`.
- Empty, the default, it does nothing: a normal build's command lines are what they were.
- It is not for MSVC. Windows compiles what it did before.

## What it changes, and why

**GameMemory's global operators.** GameMemory replaces the global `operator new` and `delete` with its own
allocator (TheDynamicMemoryAllocator). A sanitizer's runtime also supplies these operators, and mixing the
two breaks in three ways:
1. **Missing forms.** The sized deletes and the nothrow forms weren't replaced, so the runtime's were found.
   - TSan aborted before main: a sized delete freed a GameMemory block.
   - ASan failed in `freeBytes` on a `stable_sort` buffer from its own nothrow `new`.
   - Fixed separately: feature/mac-port-sized-delete, merged.
2. **System frameworks.** Even with every form replaced, macOS frameworks bind `new` and `delete`
   differently once a sanitizer runtime is loaded. Under ASan, Apple's Metal driver (AGXMetalG15X, inside
   `MTLCreateSystemDefaultDevice`) took a block from GameMemory's `new` and freed it through ASan's
   `delete`.
3. **The zero-fill contract.** GameMemory's `new` zero-fills every block (`allocateBytes`), and the engine
   relies on it: some members are set by nothing else. A first version of this option compiled GameMemory's
   operators out, leaving the sanitizer's own `new`, which doesn't zero. The game crashed loading its
   `.big` files (`ArchiveFile::attachFile`).

So under `ZH_SANITIZER_BUILD`, all fourteen forms (plain, array, sized, nothrow and the MFC-style
`new(file, line)`) take zeroed blocks from `calloc` and give them to `free`. The sanitizer intercepts
both, so:
- it sees every block;
- any runtime's `free` accepts one of ours, which covers the Metal driver;
- the engine keeps its zeroed memory.

The pools stay, since `MemoryPoolObject` classes have operators of their own, and so do the strings, which
allocate from TheDynamicMemoryAllocator by name. The link test in `initMemoryManager` is unchanged; the
counter it reads is atomic in a sanitizer build, because TSan reports the plain int's increments from
every thread.

**A normal build is unchanged**, measured on finer with clang:
- `GameMemory.cpp.o` is identical outside `__DWARF`, 8 of 8 sections. Only line numbers moved, and they
  reach the debug info and nothing else.
- The game binary differs only in `__text` and `__cstring`: the build fingerprint (`ZH_BUILD_FINGERPRINT`
  in GlobalData.cpp, N1), which any source edit changes.
- The suite passed, 86 of 86, at 496d7a14. The section comparison above is at 0ed8a896. The one later
  change (288787e4, the atomic counter) sits wholly under `ZH_SANITIZER_BUILD`.

## What it found (2026-09-27, finer)

- **`Player::init` wrote into a freed list node, after every match.** The game ran to its frame limit
  under ASan, headless mobstress at 12000 frames and the offscreen skirmish at 1800 on Metal. Both then
  stopped on this one heap-use-after-free at the reset.
  - It's EA's code. The shipped allocator made it harmless, so it's in the README's latent list.
  - It's fixed in this branch (feature/mac-port-player-uaf), with E1's CRCs unchanged.
  - With the fix, both ASan runs finish clean: 0 reports, exit 0.
- **`TheGameResultsQueue` is published to its thread without synchronisation (TSan; not fixed).**
  `GameEngine::init` (GameEngine.cpp:1285) stores the pointer after the interface's own init has started
  `GameResultsThreadClass`, which polls it (GameResultsThread.cpp:234). On x86's memory model that is
  benign in practice. On ARM the thread may see the pointer before the object behind it. It's reported to
  the PM, and it's the only race TSan found in the game besides the counter above.
- **The suite under ASan** (`ctest`, with `test_crash_reporting` disabled because it crashes on purpose;
  see CMakeLists.txt): with the Player fix, 81 of 85 pass. `mission_check` and `data_gone_check`, which
  failed before it, now pass. The four failures are three findings, each triaged for reach with the PM:
  - `LZHLCompressor::compress` (vendored Lz.cpp:247) reads 1 byte past its input, in `test_compression`
    and `compression_selfcheck`. The game never compresses with LZHL (the engine uses REFPACK), so it's
    latent.
  - `LCW_Uncomp` (WWLib lcw.cpp:142) writes 4 bytes past its output after a long run, in `test_wwlib`. Its
    loaders have no callers outside WWLib, so it's latent.
  - `WWMath::Is_Valid_Float` read its float through an `unsigned long`, 8 bytes on LP64, in `test_wwmath`.
    It's live in engine code, but the answer was right. Being fixed first
    (feature/mac-port-wwmath-lp64); `Is_Valid_Double` was wrong outright on LP64, though it has no callers.
- **TSan, with the Player fix and the atomic counter:** one report in each game run (headless mobstress,
  3000 frames; the offscreen skirmish, 900), the `TheGameResultsQueue` race above. There are no others,
  including the job threads, the renderer's threads and the Apple lock. The exit code is 134 because on
  macOS the sanitizers default to `abort_on_error=1` once anything was reported: with
  `TSAN_OPTIONS=abort_on_error=0` the same run exits 66, TSan's usual code for "warnings were reported".

## Not covered

- Linux: the option is written for GCC and clang alike, but it has been built and run only on macOS
  (finer) so far.
- Leak detection is off in these runs (`detect_leaks=0`). The engine frees much of its memory only at
  exit, or never.
- Every run here is a single-player or observer game. The network code's threads ran only in `net_check`,
  under the ASan suite.

## In the gate

The PM's direction: a sanitizer run belongs in the gate as an optional leg, not in every merge. The
proposal to -18 is for `ci-matrix --asan`: the ASan build, the headless mobstress and the offscreen
skirmish, pass only with exit 0 and no report.
