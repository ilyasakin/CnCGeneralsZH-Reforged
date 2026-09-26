# G1: Gamepad controls (Steam Deck and other controllers)

- **Milestone:** after M5's Linux pass; tested on the user's Steam Deck
- **Depends on:** the Linux port passing on thinkerer (W1 and the Linux checks); C3 (input)
- **Status:** not started (recorded 2026-09-27 at the user's request)
- **Owner:** unassigned

## Why

The user will connect a Steam Deck once the Linux port passes. The game has no controller support:
it's played with a mouse and keyboard.

## Two steps, in this order

### 1. No code: Steam Input

On the Deck, Steam Input maps a controller to mouse and keyboard for games without controller
support: the right trackpad as the cursor, the triggers as clicks, and buttons bound to keys.
Check first whether the game is playable that way. Write a recommended controller layout (control
groups, camera, cancel, the command bar) and record what's awkward. That list is the input to step 2.

### 2. Native gamepad support through SDL3's gamepad API

It works on every platform (Deck, Xbox, PlayStation, Switch Pro), not only the Deck.

- A stick-driven virtual cursor with acceleration, and optional snapping to units under it.
- A for select/confirm, B for command/cancel. Bumpers for control groups and unit cycling.
  Camera on the D-pad or the second stick, zoom on the triggers.
- Later: a radial menu for build and unit commands, and on-screen button glyphs.
- Bindings in an INI of our own, not in the retail files.

## Rules

- **The gamepad only produces the GameMessages the mouse and keyboard already produce.** No new
  message types, nothing the simulation reads differently. So lockstep, replays and mixed matches
  (a Deck player against a Windows player) are untouched. A test replays a gamepad-driven match and
  compares its CRC with the same commands issued by mouse.
- It's off unless a gamepad is connected, and the mouse and keyboard keep working alongside it.
- This is a feature, not a port fix. It doesn't break "no behaviour changes" (README, Not in scope),
  because it adds a way to issue the same commands rather than changing what any command does.
  It is recorded as a decision when the task starts.

## What it can't see until the Deck is here

Real Steam Input behaviour, the Deck's 1280x800 screen with the UI at that size, and Vulkan on its
AMD GPU. Those are checked on the device.
