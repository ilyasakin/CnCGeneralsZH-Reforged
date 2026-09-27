# Upstream notes

Defects found in upstream libraries while porting, written so they can be reported upstream as they stand.
Filing any of them is the user's decision. Each note says what was measured and what is only reasoning.

## SDL

### An animated cursor on a video driver without native animated cursors crashes `SDL_QuitMouse`

- **Found:** 2026-09-27 by -a9, on thinkerer: Arch Linux, x86_64, and SDL's `offscreen` video driver
  with no display.
- **Versions:** SDL 3.4.16 (commit fa2c02bb6e21974a89ea9824bc53c9932abe5f9c, vendored here). SDL `main` as
  of 2026-09-27 (3.5.0) has the same code in both places named below; that is read, not run.

**What happens.** The process gets SIGSEGV at exit, in `SDL_QuitMouse` (`src/events/SDL_mouse.c`, the
`next = cursor->next;` line of the loop that destroys `mouse->cursors`), reached from
`SDL_QuitSubSystem(SDL_INIT_VIDEO)`. It needs these conditions:
- the video driver has a `CreateCursor` but no `CreateAnimatedCursor` (the offscreen driver on Linux; the
  dummy driver on macOS makes no cursors at all, and never gets this far);
- the application made an animated cursor with `SDL_CreateAnimatedCursor` and more than one frame;
- the application did not destroy that cursor itself before quitting video.

The game hit it with the offscreen driver. Its cursors are Windows `.ani` files, made with
`SDL_CreateAnimatedCursor` and never destroyed, because SDL frees what is left at quit.

**Why (reading the code).** Without a driver `CreateAnimatedCursor`, `SDL_CreateAnimatedCursor` calls
`SDL_CreateCursorAnimation`, which makes one cursor per frame with `SDL_CreateColorCursor`.
`SDL_CreateColorCursor` links each frame into `mouse->cursors`. Then the parent cursor is created and
linked in as well, at the head. So the list reads: the parent, its frames, and then everything older.
`SDL_QuitMouse` walks that list:

```c
cursor = mouse->cursors;
while (cursor) {
    next = cursor->next;          // the parent's next is its own last frame
    SDL_DestroyCursor(cursor);    // destroys the parent, and through SDL_DestroyCursorAnimation its frames
    cursor = next;                // a frame that has just been freed
}
```

Destroying the parent destroys its frames, which are still on the list, including the one `next` points
to. The loop then reads freed memory. `SDL_DestroyCursor` on the parent unlinks each frame properly when an
application calls it itself, so only the quit path is affected.

**A minimal reproduction (the shape of it; not run as a separate program).**
1. `SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen")`, then `SDL_Init(SDL_INIT_VIDEO)`.
2. Make two 32x32 ARGB8888 surfaces, and `SDL_CreateAnimatedCursor` with both frames (duration 100 each).
3. `SDL_QuitSubSystem(SDL_INIT_VIDEO)` without destroying the cursor.

Expected: a clean quit. Observed, in the game with these steps: SIGSEGV in `SDL_QuitMouse`.

**Our workaround** (feature/mac-port-l2, 0412e14b). `SdlMouse_releaseCursors` destroys every cursor the
game made before SDL's video quits. With that, the thinkerer run exits cleanly. No SDL patch is carried.

**Possible upstream fixes**, for whoever files it:
- keep animation frames off `mouse->cursors`, for example by creating them through the driver's
  `CreateCursor` directly;
- or have `SDL_QuitMouse` destroy the cursors that own animations first;
- or restart the walk from `mouse->cursors` after each destroy.
