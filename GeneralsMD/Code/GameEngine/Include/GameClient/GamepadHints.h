/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// GamepadHints.h: which gamepad button to press, drawn where the keyboard's letter is (G1, §7-§9).
//
// While a pad is the device in use (SdlGamepad says so, and which kind of pad it is), the GUI shows that
// pad's buttons instead of the keys:
//   - the command bar's corner letters: in command-bar mode South's glyph on the button under the
//     pointer, outside it North's glyph on the bar's first button, and no letters (keys a pad has not);
//   - a message box's answers: South's glyph beside OK or Yes, East's beside Cancel or No.
// Keyboard and mouse input hides them all again, and the letters come back.
//
// The glyphs are Kenney's "Input Prompts" 1.5A (CC0), the plain glyph fonts of four pad families, which
// vendor.sh fetches and stage-overlay.sh puts in Data/Fonts/Gamepad.  A font is one colour, so no brand
// colour shows; the tables in GamepadHints.cpp name only buttons, never a logo glyph (no Guide, no PS,
// no Steam button), and nothing here puts a platform's name on screen.  Without the fonts (not vendored)
// there are no hints, and the letters stay.
//
// Which glyph a button gets follows what is printed on it in each family: South is A on an Xbox pad and
// the Deck, Cross on a PlayStation pad, B on a Nintendo one - SDL_GetGamepadButtonLabel's answers,
// which test_sdl_gamepad checks the tables against.

#pragma once

#ifndef __GAMEPADHINTS_H
#define __GAMEPADHINTS_H

#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "GameClient/GamepadMap.h"

class GameFont;
class GameWindow;

enum GamepadGlyphSet
{
	GAMEPAD_GLYPHS_NONE = 0,			///< the keyboard and mouse are in use: no hints
	GAMEPAD_GLYPHS_XBOX,					///< also a pad SDL knows no family for: its labels are A B X Y
	GAMEPAD_GLYPHS_PLAYSTATION,
	GAMEPAD_GLYPHS_NINTENDO,
	GAMEPAD_GLYPHS_STEAM_DECK,

	GAMEPAD_GLYPHS_COUNT
};

/// The glyphs past the buttons: the triggers, which are axes
enum
{
	GAMEPAD_GLYPH_LEFT_TRIGGER = GAMEPAD_BUTTON_COUNT,
	GAMEPAD_GLYPH_RIGHT_TRIGGER,

	GAMEPAD_GLYPH_COUNT
};

class GamepadHints
{
public:
	/// What a push button shows: its own text, a glyph in its place, nothing, or a glyph beside it
	enum Hint { HINT_TEXT, HINT_INSTEAD, HINT_HIDE, HINT_BESIDE };

	/// The pad family whose glyphs show, or GAMEPAD_GLYPHS_NONE for none (SdlGamepad sets it)
	static void setShown( GamepadGlyphSet set );
	static GamepadGlyphSet getShown( void );

	static void setCommandBarMode( Bool on );
	static Bool isCommandBarMode( void );

	/** The font (at pointSize) and character that draw button's glyph (a GamepadButtonType or a
		* GAMEPAD_GLYPH_*) in set; FALSE when the family draws none for it or its font is not there */
	static Bool glyphFor( GamepadGlyphSet set, Int button, Int pointSize, GameFont *&font, UnicodeString &glyph );

	/// What window shows now, with the glyph's font and character for HINT_INSTEAD and HINT_BESIDE
	static Hint hintFor( GameWindow *window, Int pointSize, GameFont *&font, UnicodeString &glyph );

	/// A family's name for a button's glyph (Kenney's), or NULL: for the test
	static const char *glyphName( GamepadGlyphSet set, Int button );
};

#endif // __GAMEPADHINTS_H
