# C3 — Input

- **Milestone:** M4
- **Depends on:** C2, D4
- **Blocks:** nothing
- **Status:** not started
- **Size:** `Win32Mouse.cpp`, `Win32DIKeyboard.cpp` (`Win32DIMouse.cpp` is already out of the
  build)

## Why

DirectInput for the keyboard, Win32 window messages for the mouse. Neither exists on macOS. This
is M4 work because input needs a window, and the window arrives with the renderer.

## Scope

- `GameEngineDevice/Source/Win32Device/GameClient/Win32Mouse.cpp`
- `GameEngineDevice/Source/Win32Device/GameClient/Win32DIKeyboard.cpp`
- The `Keyboard` and `Mouse` interfaces in `GameEngine/Include/GameClient/` they implement
- New: `MacDevice/GameClient/` equivalents, next to C1's work

## Do

1. Cocoa `NSEvent` for both. `GameController.framework` is not needed; this game wants a keyboard
   and a mouse.
2. Key codes are the work. The engine carries DirectInput scan codes, the whole of
   `Data/` and the control bindings are written in them, and macOS gives you virtual key codes in
   a different space. You need a translation table, and the honest way to build it is from the
   existing binding data rather than from memory.
3. **Options > Controls has Modern and Legacy schemes** and both must work identically. Legacy
   holds Ctrl for force fire; Modern uses grid hotkeys for the command bar. Where Windows tests a
   modifier, decide deliberately whether the Mac equivalent is Command or Control — for an RTS
   ported from Windows, Control is usually right and Command is usually wrong, but say which and
   why rather than letting it happen by default.
4. Right-click orders units. A Mac trackpad's secondary click and a two-button mouse both have to
   produce it.

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
