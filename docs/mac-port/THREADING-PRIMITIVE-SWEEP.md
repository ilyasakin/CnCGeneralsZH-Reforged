# Threading primitives: a sweep, so the next one is not found sideways

B8 (JobSystem) and B11 (CriticalSection) were both found by accident while doing something else,
and WWLib's separate critical-section hierarchy was found while doing B11. Three in a row is a
pattern, so this is the deliberate version: every Win32 threading primitive in the tree, listed
once, so the fourth is tasked rather than tripped over during B6.

Method: case-sensitive grep for the Win32 API names, which have fixed casing in the SDK, plus a
case-**insensitive** pass for the MSVC atomic intrinsics. The second pass matters —
`_interlockedbittestandset` is all-lowercase where every other `Interlocked*` name is capitalised,
and a case-sensitive grep for `Interlocked` misses it entirely. That cost an hour on B3.

`Tools/` is excluded throughout: the plan keeps WorldBuilder, the Launcher, mangler, matchbot and
the rest on Windows. For the record they hold 79 further sites, concentrated in `matchbot/wlib`
and `mangler/wlib`, which are a fourth and fifth copy of the same critical-section wrapper.

## The list

30 files. **Owner** is the task that should take it, or `unowned` where nothing does.

### Already done

| File | Primitives | Owner |
|:--|:--|:--|
| `GameEngine/Source/Common/System/JobSystem.cpp` | `CreateThread`, `CreateSemaphore`, `ReleaseSemaphore`, `CreateEvent`, `WaitForSingleObject`, `InterlockedDecrement`, `InterlockedExchangeAdd` | **B8, merged** |
| `GameEngine/Include/Common/CriticalSection.h` | `CRITICAL_SECTION` + the four `*CriticalSection` calls | **B11, in review** |

### In `GameEngine` / `GameEngineDevice`, and nothing owns them

| File | Primitives | Note |
|:--|:--|:--|
| **`GameEngine/Include/Common/AsciiString.h`** | `InterlockedIncrement` ×2, `InterlockedDecrement` ×2 (`:382`, `:394`, `:459`, `:466`) | **The one to care about.** This is the string refcount, in a header, on the hottest path in the engine. It is also why `TheAsciiStringCriticalSection` was dead enough for B11 to delete: the comment at `:377` says "don't need this if we're using InterlockedIncrement" — the lock was replaced by interlocked refcounting and the lock's corpse was left behind. Wants `std::atomic<int>`. **unowned** |
| `GameEngine/Include/Common/ScopedMutex.h` | `WaitForSingleObject`, `ReleaseMutex` | B5 named it: its only consumer is `MilesAudioManager.cpp`, so it is device code living in `GameEngine/Include`. Moves rather than ports. |
| `GameEngine/Source/Common/System/CopyProtection.cpp` | `CreateMutex`, `SetEvent` | **unowned** |
| `GameEngine/Source/GameClient/ChromaKeyboard.cpp` | `CreateThread`, `CRITICAL_SECTION`, `InterlockedCompareExchange`, `InterlockedExchange`, + 4 more | B5 bucket (c): excluded on macOS behind its three entry points. No port needed. |
| `GameEngine/Source/GameNetwork/GameSpy/MainMenuUtils.cpp` | `CreateThread` | B5 bucket (c): GameSpy, stub. |
| `GameEngine/Source/Common/Audio/simpleplayer.cpp` + `Include/Common/simpleplayer.h` | 24 sites, the largest single count in the tree | **Already `REMOVE_ITEM`'d from the CMake glob.** Ignore it; see the B5 survey. |
| `GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp` | `CreateMutex`, `InterlockedCompareExchange` | Device layer. C4 (Audio). |
| `GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` | `WaitForSingleObject` | Device layer. D-track. |
| `Main/WinMain.cpp` | `CreateMutex` (the one-copy-at-a-time mutex) | C2 (Entry point). |
| `Tests/miles_smoke.cpp` | `InterlockedCompareExchange`, `InterlockedExchange` | Follows C4. |

### WWVegas, which has **three** separate critical-section implementations of its own

This is the part nobody has costed. `GameEngine`'s `CriticalSection` is not the only one; WWLib
has two more, and they are unrelated class hierarchies.

| File | Primitives | Note |
|:--|:--|:--|
| `WWLib/critsection.cpp` + `critsection.h` | `CRITICAL_SECTION`, the four calls | Implementation #2. **unowned** |
| `WWLib/mutex.cpp` | `CRITICAL_SECTION`, `CreateMutex`, `ReleaseMutex`, `WaitForSingleObject`, the four calls | Implementation #3, `CriticalSectionClass` + `MutexClass`. **unowned** |
| `WWLib/mutex.h:135` | `_interlockedbittestandset` — `FastCriticalSectionClass`, a spinlock | B3 has guarded the `<intrin.h>` include on `_MSC_VER`; the intrinsic itself still needs `std::atomic_flag`. **unowned** |
| `WWLib/thread.cpp` | `_beginthread`, `CreateEvent`, `WaitForSingleObject` | **unowned** |
| `WWLib/wwmouse.h:235-236` + `wwmouse.cpp` | `InterlockedIncrement`/`Decrement` | **unowned** |
| `WWDebug/wwmemlog.cpp:317` | `_interlockedbittestandset`, plus `CRITICAL_SECTION`, `CreateMutex`, `WaitForSingleObject` | B3 guarded the include; intrinsic outstanding. **unowned** |
| `WWDebug/wwdebug.cpp` | `SetEvent`, `WaitForSingleObject` | **unowned** |
| `wwshade/shdhwshader.cpp` | `WaitForSingleObject` | **unowned** |
| `WWAudio/Threads.cpp` | `_beginthread`, `CreateEvent`, `SetEvent`, `ReleaseMutex`, `WaitForSingleObject` | WWAudio. Follows C4. |
| `WWAudio/WWAudio.cpp`, `Utils.{h,cpp}`, `SoundSceneObj.cpp` | `CRITICAL_SECTION`, `CreateEvent`, `CreateMutex`, the four calls | WWAudio. Follows C4. |

### The other two libraries

| File | Primitives | Note |
|:--|:--|:--|
| **`Libraries/Source/profile/internal.h:54`** | `_interlockedbittestandset`, `WaitForSingleObject` | **A fourth copy of the same spinlock**, and B3's `_MSC_VER` guards did not reach it — B3 guarded `WWLib/mutex.h` and `WWDebug/wwmemlog.cpp`. This one is byte-for-byte the same `while (_interlockedbittestandset((volatile long *)&nFlag, 0))` loop. **unowned** |
| `Libraries/Source/WPAudio/AUD_Windows.cpp` | `CreateThread`, `CRITICAL_SECTION`, the four calls | Its name says what it is. Follows C4. |
| `Libraries/Source/debug/debug_debug.cpp:35` | `<intrin.h>` | Include only, no threading call. |

## What this changes

1. **`AsciiString.h` is the find.** Four live `Interlocked*` calls on the refcount of the engine's
   most-used class, in a header, and nothing owns it. It is a small change — `std::atomic<int>`,
   the same shape B8 used — but it touches everything that includes `AsciiString.h`, which is most
   of the engine. It should be its own task and it should not be discovered during B6.

   It is also a gap in my own B5 survey: that survey grepped for `HWND|HRESULT|DWORD|windows.h`
   and `Interlocked*` is none of those, so `AsciiString.h` showed up there only for its stray
   `#include "windows.h"`. The survey said that include was a delete with nothing behind it. That
   was wrong — there are four intrinsics behind it.

2. **The `_interlockedbittestandset` spinlock exists four times**: `WWLib/mutex.h:135`,
   `WWDebug/wwmemlog.cpp:317`, `profile/internal.h:54`, and once more in each of `Tools/matchbot`
   and `Tools/mangler` outside the build. B3 has guarded two. One in-scope copy is left.

3. **WWVegas has three critical-section implementations**, none owned, plus `_beginthread` in two
   places. This is the "B6 trips over it" risk, and it is larger than `GameEngine`'s was.

4. **The `Interlocked*` family is where the sweeps keep failing**, because it does not look like
   Windows. `CreateThread` and `CRITICAL_SECTION` announce themselves; `InterlockedIncrement` in
   a one-line inline accessor does not, and neither does `_interlockedbittestandset` if the grep
   was case-sensitive.
