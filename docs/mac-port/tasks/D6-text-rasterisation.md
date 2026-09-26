# D6 — Text rasterisation off Windows

- **Milestone:** M4 (the game is visible; without this, it has no text)
- **Depends on:** nothing to start the seam and the rasteriser; D4 to see the result in the game
- **Blocks:** any screen with text on macOS and Linux
- **Status:** D6a done on its branch (-47); D6b waits on A1
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

## Recon and design, 2026-09-26 (-47; approved)

**What GDI does, which is what parity means.** `FontCharsClass::Create_GDI_Font` calls
`CreateFont(-MulDiv(pt, 96, 72), w, ..., bold ? FW_BOLD : FW_NORMAL, ..., ANTIALIASED_QUALITY,
VARIABLE_PITCH, face)`. **"Generals" is not a font file:** it is Arial with `lfWidth` = 0.40 x the
pixel height, a horizontal squeeze. `Store_GDI_Char` draws each character with `ExtTextOutW` into a
24-bit DIB of (2 x pt) squared at (xOrigin, 0), `ETO_OPAQUE`, top-aligned. The width it takes is
`GetTextExtentPoint32W(ch).cx` + PixelOverlap + xOrigin, the rows are `tmHeight`, and the coverage is
the blue byte, which the fork turns into 4-bit alpha through a square root. The font's metrics are
`tmHeight`, `tmAscent` and `tmOverhang`.

**Census** (the shipped `WindowZH.big` .wnd files):
- Arial: 954 uses (10 pt 651, 8 pt 116, 14 pt 86, 12 pt bold 45, 10 pt bold 32).
- Times New Roman: 530 (14 pt 509).
- Generals: 155 (15 pt 128, 20 pt 25).
- Placard MT Condensed 21, Abadi MT Bold 8, Courier 1, Courier New 1.
- The fork's own `Data/Window`: Arial 304, Generals 48, Times New Roman 44.
- Language.ini adds Courier, Courier New, FixedSys (debug) and UnicodeFontName "Arial Unicode MS".
- English ships no LocalFontFile.

**Measured** (macOS Arial 5.01, Arial Bold, Times New Roman; the 7,512 lines of English `generals.csf`; 11 to
32 px; Homebrew FreeType at design time, and again on the vendored 2.14.3 in `test_glyph_rasteriser`):
1. **Advances.** At a size its hdmx table covers, GDI takes a TrueType glyph's width from hdmx.
   FreeType's TrueType interpreter **v35 reproduces hdmx exactly**: 191 of 191 Latin-1 glyphs at every hdmx
   size, and on the vendored build 10,696 advances at 56 face sizes. FreeType's default, v40, and
   unhinted rendering do not: 17 to 92 glyphs per size are off by 1 to 2 px, and whole strings move by
   11 to 230 px (Arial 13 px: 26 px on the worst string; Times New Roman 16 px: 140 px). That would move line
   wraps.
2. **Heights.** GDI's `tmAscent` and `tmDescent` are the file's VDMX table's. FreeType does not read VDMX;
   its hhea heights and the rounded usWin metrics are both wrong by up to 2 px (Arial 11 px: 12 against
   VDMX's 14).
3. **Anti-aliasing.** `ANTIALIASED_QUALITY` obeys the gasp table. macOS's Arial 5.01 says no grey from 9 to
   17 ppem (Arial Bold: 9 to 10), so for this file the game's 8, 10 and 12 pt text is bilevel under GDI.

**Design.**
- **The seam** is `FontCharsClass`'s three GDI members. On Windows they stay byte for byte, inside
  `#if defined(_WIN32)`. POSIX twins call `GlyphRasteriserClass`.
- Rejected: a GDI-shaped shim, which needs Win32 types (decision 7's rule), and a seam through the
  Windows body, which would change the Windows view.
- **Fonts.** A registered file (LocalFontFile) is found first, by its family name. Otherwise one
  substitution table: fixed files in `/System/Library/Fonts/Supplemental` on macOS, and Liberation
  Sans/Serif/Mono through fontconfig on Linux. Linux distros put Liberation in different places and users
  install fonts under `~/.local/share/fonts`, so a fixed path list would miss them. No Microsoft font is
  bundled.
- **Phases.** D6a now; D6b with or after A1.

## D6a, 2026-09-26: the rasteriser, FreeType and the test

- **FreeType 2.14.3**, released 2026-03-22, is the newest stable tag on 2026-09-26. `vendor.sh` pins
  commit `0a0221a1`. It is POSIX only and `EXCLUDE_FROM_ALL`, with zlib, bzip2, PNG, HarfBuzz and
  Brotli off. `vendor.ps1` says it skips it.
- **`GlyphRasteriserClass`** (`WW3D2/glyphrasteriser.{h,cpp}`, its own `glyphrasteriser` library,
  outside ww3d2's list):
  - `Create_Font(face, pixel height, lfWidth, bold, box)` answers the TEXTMETRIC questions.
    `Get_Advance` answers `GetTextExtentPoint32W` for one character, and `Draw_Char` does `ExtTextOutW`
    into an 8-bit box.
  - It uses interpreter v35, VDMX heights (rounded usWin metrics without VDMX), and gasp for grey or
    bilevel.
  - An lfWidth scales x by lfWidth / (OS/2 xAvgCharWidth at this height), rounded to whole ppem, and
    skips hdmx. FreeType would otherwise take the hdmx record for the x ppem; GDI takes none for a
    scaled font.
  - Bold without a bold file is synthesised: the outline is emboldened and the advance widened by 1 px.
- **`test_glyph_rasteriser`** (ctest) reads cmap, hdmx, VDMX, gasp and the version with its own parser,
  not FreeType's:
  - All **10,696 advances equal hdmx** (Arial, Arial Bold, Times New Roman and Times New Roman Bold, 14
    sizes each).
  - **45 face sizes' heights equal VDMX**.
  - Bilevel exactly where gasp says. The switch changes pixels and never advances or heights.
  - "Generals" 15 pt: "GENERALS" is 97 px against Arial's 106 px, with the same heights. 20 pt: 125 px
    against 147 px.
  - The pen is on the baseline from the origin, and the box is cleared per character.
  - A registered file is found before the table.
  - A regression golden (string advance, ink box, coverage hash) is keyed to each font file's version
    string. A changed font file fails as "regenerate", not as eleven wrong numbers.
  - Red three ways: interpreter v40 (41 advances wrong), VDMX ignored (90 checks), gasp ignored (9).
- **The vendored FreeType agrees with Homebrew's** on every design measurement that was repeated
  (hdmx, VDMX, gasp).

**What D6a cannot see.**
- GDI itself: there is no Windows machine, and Wine's gdi32 is FreeType.
- Windows' own Arial (7.x) against macOS's 5.01. Its hdmx, VDMX and gasp may differ; each platform's
  file decides, with the same algorithm.
- GDI's grey shading against FreeType's.
- How GDI rounds a width-scaled font. The census sizes' x ppem (18, 23) have no hdmx record, so the
  scaled-hdmx rule is not exercised by them.
- GDI's simulated bold, taken as 1 px wider. No macOS face in the table needs it.
- The fontconfig branch was syntax-checked against Homebrew's fontconfig headers, not run. Linux is
  tested at its milestone.

## For M4's visual review (recorded, not decided)

Following gasp with macOS's Arial 5.01 makes the game's 8, 10 and 12 pt text **bilevel on the Mac**.
Windows 10 and 11 ship Arial 7.x, whose gasp probably says grey at those sizes. So a Mac player may see
aliased text where a Windows player sees smooth text. Layout is unaffected: advances and heights do not
depend on anti-aliasing. The default is faithful to GDI with the platform's own file.
`GlyphRasteriserClass::Set_Antialias_Mode(ANTIALIAS_ALWAYS_GRAY)` is the one switch. **At M4, with text
on screen, the user judges which reads better.** The switch is not in the game's options.

## D6b (with or after A1)

- `render2dsentence.cpp` gets its POSIX twins of `Create_GDI_Font`, `Store_GDI_Char` and
  `Free_GDI_Font`. The Windows bodies go under `#if defined(_WIN32)`, unchanged, and
  `windows_view_diff.py` must say identical.
- `GlobalLanguage`'s POSIX `installLocalFont` calls `Register_Font_File`.
- Both compile only once ww3d2 builds off Windows.
