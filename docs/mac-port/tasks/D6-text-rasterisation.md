# D6 — Text rasterisation off Windows

- **Milestone:** M4 (the game is visible; without this, it has no text)
- **Depends on:** nothing to start the seam and the rasteriser; D4 to see the result in the game
- **Blocks:** any screen with text on macOS and Linux
- **Status:** not started
- **Size:** `WW3D2/render2dsentence.cpp` (the GDI path), `GameClient/GlobalLanguage.cpp` (font
  installation), one vendored library

## Why

Decision 6 in `docs/mac-port/README.md`. Every glyph the game draws is rasterised by GDI:
`render2dsentence.cpp` creates a GDI font, draws the string into a DIB section with `ExtTextOut`,
and copies the bits into a texture. `GlobalLanguage.cpp` makes the language's own font files
available with `AddFontResource`/`RemoveFontResource`. None of that exists off Windows, and until
2026-09-26 no task owned it.

## Do

1. A glyph-rasteriser seam under render2dsentence: font creation by face name, point size and
   weight; per-glyph metrics (advance, ascent, descent, overhang); and a coverage bitmap per glyph.
   The Windows body is today's GDI code, unchanged: prove it with `Tools/windows_view_diff.py`.
2. A FreeType body, vendored through `Tools/vendor.sh` with a pinned commit, POSIX only.
3. Font lookup: the game's own font files (the ones `GlobalLanguage.cpp` installs) load from the
   game data by path. System faces go through ONE substitution table: Arial and Times New Roman
   come from the system on macOS. On Linux they come from Liberation Sans and Serif, found through
   fontconfig or a fixed path list (decide which, and say why). Do not bundle Microsoft's fonts.
4. Measure metric parity against GDI, since line wrapping depends on it. Without a Windows machine,
   compare advances for the game's strings against published Arial metrics, or the
   hmtx table of the same font file read directly. Report the worst case at the UI's sizes.
5. A ctest that rasterises a fixed string in each face the game uses at its common sizes and checks
   its advance and bounding box against committed values.

## Do not

- Do not change the Windows text path.
- Do not bundle Arial, Times New Roman or any Microsoft font file.
