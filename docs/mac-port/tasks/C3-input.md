# C3 — Input

- **Milestone:** M4
- **Depends on:** C2, D4
- **Blocks:** nothing
- **Status:** done: C3a and C3b merged (`a2083900`)
- **Size:** `Win32Mouse.cpp`, `Win32DIKeyboard.cpp` (`Win32DIMouse.cpp` is already out of the
  build)

> **Decision 3 (2026-09-25, `docs/mac-port/README.md`) applies here.** Keyboard and mouse come from SDL3 events on macOS and Linux, not `NSEvent`. The mapping from SDL scancodes to the game's DirectInput key codes is the piece of real work, and it lives in one table.

## Why

DirectInput for the keyboard, Win32 window messages for the mouse. Neither exists on macOS. This
is M4 work because input needs a window, and the window arrives with the renderer.

## Scope (decision 3: SDL3 events, not NSEvent; rewritten 2026-09-26, -47, design approved)

The seams are GameClient's factories, not SdlGameEngine's: `W3DGameClient.h` makes
`DirectInputKeyboard` and `W3DMouse`, and `W3DMouse` derives from `Win32Mouse`. Decision 8 builds
W3DGameClient off Windows, so that is where the platform choice goes.

- **C3a (now; builds and tests without the renderer):**
  - `SdlDevice/GameClient/`: `SdlKeyboard`, `SdlMouse`, the scancode table, the `.ANI` cursor loader, and
    `SdlInput_dispatch`. -18's `SdlGameEngine::serviceWindowsOS` calls it from its switch's default, and
    that is the whole of the edit to C2's pump.
  - `IMEManagerPosix.cpp`: a real `IMEManagerInterface`, platform-neutral, with device hooks. On Windows
    every typed character reaches a text field through IMEManager (`WM_CHAR` becomes `GWM_IME_CHAR`), so
    the old NULL manager meant no text input at all off Windows.
  - `Platform/DoubleClickTime.h`: macOS's own setting.
- **C3b (with A1, when W3DDevice compiles off Windows):**
  - `W3DGameClient.h`'s two factory lines go under `#if defined(_WIN32)`, and so does `W3DMouse`'s base.
  - Off Windows only, `typedef SdlMouse Win32Mouse;` keeps `W3DMouse.cpp`'s `Win32Mouse::` calls
    textually identical.
  - `SetCursor(NULL)` becomes `SDL_HideCursor` under `#if`.
  - `windows_view_diff.py` must say identical.

## Do

1. **Keyboard.**
   - One table maps SDL scancodes (USB HID positions, the same on every platform) to the engine's DIK codes.
     Each of the 107 codes is mapped or explicitly unreachable.
   - SDL's key-repeat events are dropped: DirectInput had none, and the engine makes its own
     (`Keyboard::checkKeyRepeat`).
   - Caps lock state comes from SDL's mod state.
   - Mac choices: Ctrl stays Ctrl, because the game's bindings are not redesigned. Command belongs to the
     system: Cmd+Q is SDL_EVENT_QUIT, the path Alt+F4 takes. Option is Alt.
   - `SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK` is set to "0", because Legacy force fire is Ctrl+left
     click.
   - Insert, Pause and a laptop numpad are absent on Macs, and none of them is bound: the bound keypad keys
     duplicate the arrows.
2. **Text.**
   - `attach` and `detatch` start and stop SDL text input at the field's rectangle, so the OS's input
     method is active only in text fields.
   - Committed UTF-8 goes through `WideCharFromUtf8` and is sent as `GWM_IME_CHAR` per UTF-16 unit of 32
     or more, which is IMEManager.cpp's rule. Return and keypad Enter send '\r', as `WM_CHAR` does.
   - Composition comes from `SDL_EVENT_TEXT_EDITING`. The OS draws the composition and the candidate
     window.
3. **Mouse.**
   - Mirrors `Win32Mouse`: its ring buffer, and positions scaled from window points to game pixels.
   - Double click is SDL's click count from the system (even is a double click, odd is a press).
   - The wheel is accumulated at 120 per notch. The user's natural-scrolling setting is honoured.
   - `isCursorInWindow` follows SDL's mouse focus.
   - The pointer is confined (mouse grab) under `Win32Mouse::update`'s ClipCursor condition.
   - SDL's auto-capture is kept, so a release outside the window arrives. Windows loses it, and keeping it
     is a stated improvement.
   - No relative mode.
4. **Cursors.** RM_WINDOWS, the default, uses the 52 `Data/Cursors/*.ANI` files. All 296 frames are 32 x
   32, 4-bit `.CUR` images. They are decoded to `SDL_CreateColorCursor` / `SDL_CreateAnimatedCursor`,
   with durations from the rate and sequence chunks.
5. **Tests.**
   - SDL's offscreen driver and synthetic events, through `SdlInput_dispatch`.
   - Every table entry, text with non-ASCII and surrogates, and the mouse, each with an armed control.
   - The `.ANI` decoder against an independent ICO/BMP parse of every install file (read-only, rule 9).

## C3a, 2026-09-26 (-47): what landed and what shows it

- **Files:**
  - `SdlDevice/GameClient/`: `SdlKeyTable`, `SdlKeyboard`, `SdlMouse`, `SdlInput` (dispatch, hints and the
    IME hooks) and `AniCursor`.
  - `GameClient/IMEManagerPosix.{h,cpp}`: the IME manager.
  - `Platform/DoubleClickTime.h`: the macOS body.
  - One line in -18's pump: `default: SdlInput_dispatch( event );`.
- **Refinement to the approved design:** the engine draws the composition (`SDL_HINT_IME_IMPLEMENTED_UI`
  set to "composition"), because W3DTextEntry already draws `getCompositionString` inline, as on Windows.
  The platform draws only the candidate list.
- **Focus:** WndProc's `WM_ACTIVATEAPP` told `Win32Mouse` it had lost the focus and put the cursor back
  when it returned. SdlMouse reads the window's focus flag each frame instead, so -18's focus cases stay
  as they are.
- **Double-click time:** read from `NSEvent.doubleClickInterval` through `dlsym`'d ObjC runtime calls. So
  gameengine links nothing new. A process without AppKit gets 500.
- **`test_sdl_input`** (20 tests, 1,907 checks):
  - DIKeyCodes.h, read as text: 107 codes, 105 reached from 106 scancodes, 2 unreachable with reasons.
  - Every table entry goes through dispatch into the engine's key state, down and up.
  - Modifiers, repeats dropped, caps, and the full queue.
  - Text: "Abğüş€😀" arrives as its exact UTF-16 units; control characters are dropped. Enter and keypad
    Enter send '\r' (armed: SDL's text never carries one). Composition and its cursor are counted in
    code points and turned into UTF-16 units.
  - Mouse: scaling and clamping, the button states, the click-count double click (1..4), X1 ignored,
    the wheel with its carried fraction and natural scrolling left alone, the full ring, and the
    confinement truth table.
  - The double-click time against `defaults read -g com.apple.mouse.doubleClickThreshold`.
  - All 52 install `.ANI` files are byte-identical to the test's own parse (296 frames, 325 steps, no
    screen-inverting pixels), and all 52 became SDL cursors.
- **Red on six mutations:** repeats forwarded, no Enter, every click after the first a double, control
  characters kept, A mapped to S, and the wheel's fraction dropped.
- **Found on the way:** the Steam install's exFAT volume holds a macOS `._X.ani` AppleDouble file beside
  each cursor. The game opens cursors by name and never sees them; a directory listing does.
- **Cannot see:**
  - Real keyboards: JIS and ISO layouts, the Help key.
  - A real input method session.
  - macOS shortcuts taking keys first.
  - The double-click value itself varying: the machine's setting is 500 ms, the same as the fallback.
    The test shows only that NSEvent's class is present and asked.
  - Edge scrolling and the radar drag in play, which need the game on screen (M4).

## C3b, 2026-09-26 (-47): the game makes the SDL input, through W3DGameClient's own factories

- **`W3DGameClient.h`.** A1 had left the keyboard and mouse factories to a subclass off Windows. Both
  factories are W3DGameClient's own again.
  - createKeyboard is `DirectInputKeyboard` on Windows and `SdlKeyboard` elsewhere.
  - createMouse is **`W3DMouse` on both**, so the W3D cursor modes (RM_W3D, RM_POLYGON, RM_DX8) come
    along off Windows too.
  - `TheWin32Mouse` stays Windows-only; `SdlMouse::active()` plays its part.
  - -18's `PosixW3DGameClient` needs no input overrides, and any interim ones it gains are deleted
    here.
- **`W3DMouse.h`.** Off Windows the base is `SdlMouse`, through `typedef SdlMouse Win32Mouse;`, which is
  commented as an engine class name kept so that `W3DMouse.cpp`'s text is the same on both platforms.
- **`W3DMouse.cpp`,** back in A1's `w3ddevice`:
  - `SetCursor(NULL)` becomes `SDL_HideCursor()` (three sites).
  - RM_DX8's `GetCursorPos`/`ScreenToClient` becomes `SDL_GetMouseState` scaled to game pixels.
  - `extern HWND ApplicationHWnd` is Windows-only.
  - The in-class `MouseThreadClass::MouseThreadClass()` qualification, which MSVC accepts and C++ does
    not, has a plain `#else`.
  - `HRESULT` becomes `RenderResult`, A1's spelling, which is HRESULT on Windows.
- **`D3D9Posix.h`** gains `D3DCURSOR_IMMEDIATE_UPDATE` (1). `d3d9posix_check.py` agrees on all 157
  macros.
- **`sdlinput`.** The SDL input files are their own library, which both `w3ddevice` and `sdldevice`
  link, so that sdldevice can take w3ddevice for the engine's W3D factories without a cycle.
- **Headless.** Windows' `LoadCursorFromFile` needs no display, so a headless Windows run loads the
  cursors. SDL's need SDL's video, which -headless never starts, and each one would fail with a
  DEBUG_ASSERTCRASH in a debug build. `SdlMouse::initCursorResources` returns first without video.
  `test_sdl_input` shows it with a counting file system: no cursor file is opened without video, and
  the same call opens one with video. It is red with the guard removed.
- **Windows view** (`windows_view_diff.py` against 73e148ef): W3DGameClient.h and W3DMouse.h are
  IDENTICAL. W3DMouse.cpp differs in the one `HRESULT res` → `RenderResult res` line, the same type.
  SdlMouse.cpp, D3D9Posix.h and the test are not in the Windows build.
- **Cannot see:** W3DMouse's W3D cursor modes drawing (that needs the renderer drawing, D4), and the
  game's input in play (M4).

## Done when

A skirmish is playable end to end with keyboard and mouse: select, order, attack-move, force fire,
grid hotkeys, the radar drag, shift-queue and ctrl-queue on build buttons. Both control schemes.

## Do not

- Do not redesign the bindings for macOS convention. The game's controls are the game's controls
  and `README.md` documents them; a port that quietly moves a key is a bug report waiting to
  happen.

## Notes

The radar drag (hold on the radar, camera follows the cursor) and edge scrolling are the two things
most likely to behave subtly wrong under a different event model. Test them specifically.
