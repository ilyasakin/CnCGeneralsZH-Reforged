# C3 — Input

- **Milestone:** M4
- **Depends on:** C2, D4
- **Blocks:** nothing
- **Status:** C3a in progress (-47); C3b with A1
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
