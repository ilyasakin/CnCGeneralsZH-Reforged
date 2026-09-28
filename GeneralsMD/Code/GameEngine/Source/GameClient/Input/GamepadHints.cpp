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

// GamepadHints.cpp: see GamepadHints.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GamepadFocus.h"
#include "GameClient/GamepadHints.h"
#include "GameClient/GamepadRadial.h"
#include "GameClient/GlobalLanguage.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/Mouse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace {

/// Each family's glyph for each button (GamepadMap.h's order, then the triggers), as Kenney names them.
/// Only buttons: no Guide, PS or Steam button - their glyphs are logos - and no Deck "..." button.
const char *const theGlyphNames[ GAMEPAD_GLYPHS_COUNT ][ GAMEPAD_GLYPH_COUNT ] =
{
	{ NULL },		// none
	{	// Xbox, and any pad whose labels are A B X Y
		"xbox_button_a", "xbox_button_b", "xbox_button_x", "xbox_button_y",
		"xbox_button_view", NULL, "xbox_button_menu",
		"xbox_stick_l_press", "xbox_stick_r_press", "xbox_lb", "xbox_rb",
		"xbox_dpad_up", "xbox_dpad_down", "xbox_dpad_left", "xbox_dpad_right",
		"xbox_button_share",
		"xbox_elite_paddle_top_right", "xbox_elite_paddle_top_left",
		"xbox_elite_paddle_bottom_right", "xbox_elite_paddle_bottom_left",
		NULL,
		"xbox_lt", "xbox_rt"
	},
	{	// PlayStation
		"playstation_button_cross", "playstation_button_circle", "playstation_button_square", "playstation_button_triangle",
		"playstation5_button_create", NULL, "playstation5_button_options",
		"playstation_button_l3", "playstation_button_r3", "playstation_trigger_l1", "playstation_trigger_r1",
		"playstation_dpad_up", "playstation_dpad_down", "playstation_dpad_left", "playstation_dpad_right",
		"playstation5_button_mute",
		"playstation5_elite_rb", "playstation5_elite_lb", "playstation5_elite_fn_r", "playstation5_elite_fn_l",
		"playstation5_touchpad",
		"playstation_trigger_l2", "playstation_trigger_r2"
	},
	{	// Nintendo: B is at the bottom, A on the right
		"switch_button_b", "switch_button_a", "switch_button_y", "switch_button_x",
		"switch_button_minus", NULL, "switch_button_plus",
		"switch_stick_l_press", "switch_stick_r_press", "switch_button_l", "switch_button_r",
		"switch_dpad_up", "switch_dpad_down", "switch_dpad_left", "switch_dpad_right",
		NULL,
		NULL, NULL, NULL, NULL,
		NULL,
		"switch_button_zl", "switch_button_zr"
	},
	{	// the Steam Deck
		"steamdeck_button_a", "steamdeck_button_b", "steamdeck_button_x", "steamdeck_button_y",
		"steamdeck_button_view", NULL, "steamdeck_button_options",
		"steamdeck_stick_l_press", "steamdeck_stick_r_press", "steamdeck_button_l1", "steamdeck_button_r1",
		"steamdeck_dpad_up", "steamdeck_dpad_down", "steamdeck_dpad_left", "steamdeck_dpad_right",
		NULL,
		"steamdeck_button_r4", "steamdeck_button_l4", "steamdeck_button_r5", "steamdeck_button_l5",
		NULL,
		"steamdeck_button_l2", "steamdeck_button_r2"
	}
};

/// Each family's font: the file (overlay-relative, the engine's spelling), its map, its family name
struct GlyphFont
{
	const char *file;
	const char *map;
	const char *family;
};

const GlyphFont theFonts[ GAMEPAD_GLYPHS_COUNT ] =
{
	{ NULL, NULL, NULL },
	{ "Data\\Fonts\\Gamepad\\kenney_input_xbox_series.ttf", "Data\\Fonts\\Gamepad\\kenney_input_xbox_series_map.txt", "Kenney Input Xbox Series" },
	{ "Data\\Fonts\\Gamepad\\kenney_input_playstation_series.ttf", "Data\\Fonts\\Gamepad\\kenney_input_playstation_series_map.txt", "Kenney Input PlayStation Series" },
	{ "Data\\Fonts\\Gamepad\\kenney_input_nintendo_switch.ttf", "Data\\Fonts\\Gamepad\\kenney_input_nintendo_switch_map.txt", "Kenney Input Nintendo Switch" },
	{ "Data\\Fonts\\Gamepad\\kenney_input_steam_deck.ttf", "Data\\Fonts\\Gamepad\\kenney_input_steam_deck_map.txt", "Kenney Input Steam Deck" }
};

/// Where a glyph's ink is, in font units above the baseline
struct Ink
{
	Int yMin, yMax;
};

/// A family's font once it is installed, and its code points by glyph name
struct LoadedFont
{
	Bool tried;
	Bool ok;
	std::map<std::string, WideChar> codes;
	Int winAscent, winDescent;		///< OS/2's: the line the engine's rasteriser lays a glyph in (glyphrasteriser.cpp)
	std::map<WideChar, Ink> ink;		///< each code's ink, from the font's own outlines
};

LoadedFont theLoaded[ GAMEPAD_GLYPHS_COUNT ];
GamepadGlyphSet theShown = GAMEPAD_GLYPHS_NONE;
Bool theConfirmSwapped = FALSE;
Bool theCommandBarMode = FALSE;
Int theLayer = GAMEPAD_BUTTON_NONE;

/// Kenney's map: one "glyph_name: U+E004" a line
Bool readMap( const char *path, std::map<std::string, WideChar> &codes )
{
	if (TheFileSystem == NULL)
		return FALSE;
	File *file = TheFileSystem->openFile( path, File::READ | File::BINARY );
	if (file == NULL)
		return FALSE;
	const Int size = file->size();
	std::vector<char> text( size > 0 ? size + 1 : 1, 0 );
	const Bool read = size > 0 && file->read( &text[0], size ) == size;
	file->close();
	if (!read)
		return FALSE;
	for (char *line = strtok( &text[0], "\r\n" ); line != NULL; line = strtok( NULL, "\r\n" ))
	{
		char name[ 128 ];
		unsigned int code = 0;
		if (sscanf( line, " %127[^:]: U+%x", name, &code ) == 2 && code > 0 && code < 0x10000)
			codes[ name ] = (WideChar)code;
	}
	return !codes.empty();
}

UnsignedInt be16( const std::vector<unsigned char> &b, size_t at )
{
	return at + 2 <= b.size() ? (UnsignedInt)((b[at] << 8) | b[at + 1]) : 0;
}

Int be16s( const std::vector<unsigned char> &b, size_t at )
{
	return (Int)(short)be16( b, at );
}

UnsignedInt be32( const std::vector<unsigned char> &b, size_t at )
{
	return (be16( b, at ) << 16) | be16( b, at + 2 );
}

/** The ink of each glyph the family's map names, and the font's line, from the TrueType file itself (its head,
	* OS/2, cmap format 4, loca and glyf tables).  Kenney's glyphs sit on the baseline and are shorter than the line
	* the rasteriser gives them (a face button 983 units of a 1389-unit line, LB 655, View 410), so a glyph centred
	* by its line sits low, and a short one lower: the draw sites centre the ink instead (inkRows). */
Bool readInk( const char *path, LoadedFont &font )
{
	if (TheFileSystem == NULL)
		return FALSE;
	File *file = TheFileSystem->openFile( path, File::READ | File::BINARY );
	if (file == NULL)
		return FALSE;
	const Int size = file->size();
	std::vector<unsigned char> b( size > 0 ? size : 0 );
	const Bool read = size > 0 && file->read( &b[0], size ) == size;
	file->close();
	if (!read)
		return FALSE;
	size_t head = 0, os2 = 0, cmap = 0, loca = 0, glyf = 0;
	for (UnsignedInt i = 0, tables = be16( b, 4 ); i < tables; ++i)
	{
		const size_t record = 12 + 16 * i;
		const UnsignedInt tag = be32( b, record ), offset = be32( b, record + 8 );
		if (tag == 0x68656164) head = offset;						// 'head'
		else if (tag == 0x4F532F32) os2 = offset;				// 'OS/2'
		else if (tag == 0x636D6170) cmap = offset;			// 'cmap'
		else if (tag == 0x6C6F6361) loca = offset;			// 'loca'
		else if (tag == 0x676C7966) glyf = offset;			// 'glyf'
	}
	if (head == 0 || os2 == 0 || cmap == 0 || loca == 0 || glyf == 0)
		return FALSE;
	const Bool longLoca = be16s( b, head + 50 ) != 0;
	font.winAscent = (Int)be16( b, os2 + 74 );
	font.winDescent = (Int)be16( b, os2 + 76 );
	// the Unicode BMP subtable, format 4
	size_t sub = 0;
	for (UnsignedInt i = 0, count = be16( b, cmap + 2 ); i < count && sub == 0; ++i)
	{
		const size_t record = cmap + 4 + 8 * i;
		const UnsignedInt platform = be16( b, record ), encoding = be16( b, record + 2 );
		const size_t at = cmap + be32( b, record + 4 );
		if (((platform == 3 && encoding == 1) || platform == 0) && be16( b, at ) == 4)
			sub = at;
	}
	if (sub == 0)
		return FALSE;
	const UnsignedInt segments = be16( b, sub + 6 ) / 2;
	const size_t ends = sub + 14, starts = ends + 2 * segments + 2, deltas = starts + 2 * segments, ranges = deltas + 2 * segments;
	for (std::map<std::string, WideChar>::const_iterator it = font.codes.begin(); it != font.codes.end(); ++it)
	{
		const UnsignedInt code = (UnsignedInt)it->second;
		UnsignedInt glyph = 0;
		for (UnsignedInt seg = 0; seg < segments; ++seg)
		{
			if (code > be16( b, ends + 2 * seg ) || code < be16( b, starts + 2 * seg ))
				continue;
			const UnsignedInt range = be16( b, ranges + 2 * seg );
			if (range == 0)
				glyph = (code + be16( b, deltas + 2 * seg )) & 0xFFFF;
			else
			{
				const UnsignedInt at = be16( b, ranges + 2 * seg + range + 2 * (code - be16( b, starts + 2 * seg )) );
				glyph = at != 0 ? (at + be16( b, deltas + 2 * seg )) & 0xFFFF : 0;
			}
			break;
		}
		if (glyph == 0)
			continue;
		const size_t from = longLoca ? be32( b, loca + 4 * glyph ) : 2 * be16( b, loca + 2 * glyph );
		const size_t to = longLoca ? be32( b, loca + 4 * glyph + 4 ) : 2 * be16( b, loca + 2 * glyph + 2 );
		if (to <= from)
			continue;		// an empty glyph: no ink
		Ink ink;
		ink.yMin = be16s( b, glyf + from + 4 );
		ink.yMax = be16s( b, glyf + from + 8 );
		font.ink[ it->second ] = ink;
	}
	return font.winAscent + font.winDescent > 0;
}

LoadedFont &loaded( GamepadGlyphSet set )
{
	LoadedFont &font = theLoaded[ set ];
	if (!font.tried)
	{
		font.tried = TRUE;
		font.ok = theFonts[ set ].file != NULL && readMap( theFonts[ set ].map, font.codes )
			&& GlobalLanguage_installFontFile( AsciiString( theFonts[ set ].file ) );
		if (font.ok && !readInk( theFonts[ set ].file, font ))
			DEBUG_LOG(( "GamepadHints: %s's outlines not read: its glyphs are centred by their line\n", theFonts[ set ].file ));
		if (!font.ok)
			DEBUG_LOG(( "GamepadHints: no %s (vendor.sh fetches the hint fonts): that pad's hints stay off\n", theFonts[ set ].file ));
	}
	return font;
}

NameKeyType keyFor( const char *name )
{
	return TheNameKeyGenerator != NULL ? TheNameKeyGenerator->nameToKey( AsciiString( name ) ) : NAMEKEY_INVALID;
}

/// A message box's answer: 1 for OK or Yes, -1 for Cancel or No, 0 for neither (MessageBox.cpp's names)
Int answerOf( Int id )
{
	static const char *const accept[] = { "MessageBox.wnd:ButtonOk", "MessageBox.wnd:ButtonYes",
		"QuitMessageBox.wnd:ButtonOk", "QuitMessageBox.wnd:ButtonYes" };
	static const char *const decline[] = { "MessageBox.wnd:ButtonCancel", "MessageBox.wnd:ButtonNo",
		"QuitMessageBox.wnd:ButtonCancel", "QuitMessageBox.wnd:ButtonNo" };
	for (Int i = 0; i < 4; ++i)
	{
		if (id == (Int)keyFor( accept[i] ))
			return 1;
		if (id == (Int)keyFor( decline[i] ))
			return -1;
	}
	return 0;
}

/// The command bar button slot a window is, or -1 (ControlBar.cpp's names)
Int commandSlotOf( Int id )
{
	static NameKeyType keys[ MAX_COMMANDS_PER_SET ];
	static Bool made = FALSE;
	if (!made && TheNameKeyGenerator != NULL)
	{
		made = TRUE;
		for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
		{
			char name[ 64 ];
			snprintf( name, sizeof( name ), "ControlBar.wnd:ButtonCommand%02d", i + 1 );
			keys[i] = keyFor( name );
		}
	}
	for (Int i = 0; made && i < MAX_COMMANDS_PER_SET; ++i)
		if (id == (Int)keys[i])
			return i;
	return -1;
}

Bool isShown( GameWindow *window )
{
	for (GameWindow *w = window; w != NULL; w = w->winGetParent())
		if (w->winIsHidden())
			return FALSE;
	return window != NULL;
}

/// The slot of the bar's top-left shown button, where the command bar's button's glyph goes; worked out once a frame
Int firstShownSlot( void )
{
	static UnsignedInt frame = 0xFFFFFFFF;
	static Int first = -1;
	const UnsignedInt now = TheGameClient != NULL ? TheGameClient->getFrame() : 0;
	if (now == frame)
		return first;
	frame = now;
	first = -1;
	Int firstX = 0, firstY = 0;
	commandSlotOf( 0 );		// the keys, made
	for (Int i = 0; TheWindowManager != NULL && i < MAX_COMMANDS_PER_SET; ++i)
	{
		char name[ 64 ];
		snprintf( name, sizeof( name ), "ControlBar.wnd:ButtonCommand%02d", i + 1 );
		GameWindow *button = TheWindowManager->winGetWindowFromId( NULL, keyFor( name ) );
		if (!isShown( button ))
			continue;
		Int x, y;
		button->winGetScreenPosition( &x, &y );
		if (first < 0 || y < firstY || (y == firstY && x < firstX))
		{
			first = i;
			firstX = x;
			firstY = y;
		}
	}
	return first;
}

}  // namespace

void GamepadHints::setShown( GamepadGlyphSet set )
{
	theShown = set;
}

void GamepadHints::setConfirmSwapped( Bool swapped )
{
	theConfirmSwapped = swapped;
}

Bool GamepadHints::isConfirmSwapped( void )
{
	return theConfirmSwapped;
}

Int GamepadHints::physicalFor( Int button )
{
	if (!theConfirmSwapped)
		return button;
	return button == GAMEPAD_BUTTON_SOUTH ? GAMEPAD_BUTTON_EAST : (button == GAMEPAD_BUTTON_EAST ? GAMEPAD_BUTTON_SOUTH : button);
}

GamepadGlyphSet GamepadHints::getShown( void )
{
	return theShown;
}

void GamepadHints::setCommandBarMode( Bool on )
{
	theCommandBarMode = on;
}

Bool GamepadHints::isCommandBarMode( void )
{
	return theCommandBarMode;
}

Bool GamepadHints::isGlyphFont( const char *family )
{
	for (Int set = GAMEPAD_GLYPHS_NONE + 1; family != NULL && set < GAMEPAD_GLYPHS_COUNT; ++set)
		if (strcmp( family, theFonts[ set ].family ) == 0)
			return TRUE;
	return FALSE;
}

const char *GamepadHints::glyphName( GamepadGlyphSet set, Int button )
{
	if (set <= GAMEPAD_GLYPHS_NONE || set >= GAMEPAD_GLYPHS_COUNT || button < 0 || button >= GAMEPAD_GLYPH_COUNT)
		return NULL;
	return theGlyphNames[ set ][ button ];
}

Bool GamepadHints::glyphFor( GamepadGlyphSet set, Int button, Int pointSize, GameFont *&font, UnicodeString &glyph )
{
	const char *name = glyphName( set, physicalFor( button ) );		// the one that does it
	if (name == NULL || TheFontLibrary == NULL)
		return FALSE;
	LoadedFont &fontData = loaded( set );
	if (!fontData.ok)
		return FALSE;
	std::map<std::string, WideChar>::const_iterator code = fontData.codes.find( name );
	if (code == fontData.codes.end())
		return FALSE;
	font = TheFontLibrary->getFont( AsciiString( theFonts[ set ].family ), pointSize, FALSE );
	if (font == NULL)
		return FALSE;
	glyph.clear();
	glyph.concat( code->second );
	return TRUE;
}

void GamepadHints::inkRows( GamepadGlyphSet set, Int button, Int cellHeight, Int &top, Int &bottom )
{
	top = 0;
	bottom = cellHeight;
	const char *name = glyphName( set, physicalFor( button ) );
	if (name == NULL || set <= GAMEPAD_GLYPHS_NONE || set >= GAMEPAD_GLYPHS_COUNT || cellHeight <= 0)
		return;
	const LoadedFont &font = loaded( set );
	std::map<std::string, WideChar>::const_iterator code = font.codes.find( name );
	if (!font.ok || code == font.codes.end() || font.winAscent + font.winDescent <= 0)
		return;
	std::map<WideChar, Ink>::const_iterator ink = font.ink.find( code->second );
	if (ink == font.ink.end())
		return;
	// the rasteriser's line is the font's win ascent over its win descent, each rounded: the baseline sits at the
	// ascent's share of the cell, and the ink rises from there
	const Real scale = (Real)cellHeight / (Real)(font.winAscent + font.winDescent);
	const Int baseline = (Int)floorf( font.winAscent * scale + 0.5f );
	top = baseline - (Int)floorf( ink->second.yMax * scale + 0.5f );
	bottom = baseline - (Int)floorf( ink->second.yMin * scale + 0.5f );
	if (top < 0) top = 0;
	if (bottom > cellHeight) bottom = cellHeight;
	if (bottom <= top) { top = 0; bottom = cellHeight; }
}

Int GamepadHints::glyphTop( GamepadGlyphSet set, Int button, Int cellHeight, Int centreY )
{
	Int top, bottom;
	inkRows( set, button, cellHeight, top, bottom );
	return centreY - (top + bottom) / 2;
}

GamepadHints::Hint GamepadHints::hintFor( GameWindow *window, Int pointSize, GameFont *&font, UnicodeString &glyph, Int *button )
{
	Int unused;
	Int &which = button != NULL ? *button : unused;
	which = GAMEPAD_BUTTON_NONE;
	if (theShown == GAMEPAD_GLYPHS_NONE || window == NULL)
		return HINT_TEXT;
	const Int id = window->winGetWindowId();

	// a message box's answers: South's glyph beside OK or Yes, East's beside Cancel or No
	const Int answer = answerOf( id );
	if (answer != 0)
	{
		which = answer > 0 ? GAMEPAD_BUTTON_SOUTH : GAMEPAD_BUTTON_EAST;
		return glyphFor( theShown, which, pointSize, font, glyph ) ? HINT_BESIDE : HINT_TEXT;
	}

	// the command bar: its letters are keys the pad has not
	if (commandSlotOf( id ) < 0)
		return HINT_TEXT;
	if (theCommandBarMode)
	{
		const MouseIO *mouse = TheMouse != NULL ? TheMouse->getMouseStatus() : NULL;
		which = GAMEPAD_BUTTON_SOUTH;
		if (mouse != NULL && window->winPointInWindow( mouse->pos.x, mouse->pos.y )
				&& glyphFor( theShown, which, pointSize, font, glyph ))
			return HINT_INSTEAD;
		return HINT_HIDE;
	}
	const GamepadButtonType bar = TheGamepadMap != NULL ? TheGamepadMap->buttonFor( GAMEPAD_ACTION_COMMAND_BAR ) : GAMEPAD_BUTTON_NONE;
	which = bar;
	if (commandSlotOf( id ) == firstShownSlot() && bar != GAMEPAD_BUTTON_NONE && glyphFor( theShown, bar, pointSize, font, glyph ))
		return HINT_INSTEAD;
	return HINT_HIDE;
}

void GamepadHints::setLayer( Int button )
{
	theLayer = button;
}

Int GamepadHints::getLayer( void )
{
	return theLayer;
}

void GamepadHints::drawMatchStrip( void )
{
	if (theShown == GAMEPAD_GLYPHS_NONE || TheDisplay == NULL || TheDisplayStringManager == NULL || TheFontLibrary == NULL
			|| TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame()
			|| GamepadFocus::isActive() || GamepadRadial::isOpen())
		return;
	struct Prompt { Int button; const char *label; };
	static const Prompt plain[] = { { GAMEPAD_BUTTON_SOUTH, "GUI:GamepadSelect" }, { GAMEPAD_BUTTON_WEST, "GUI:GamepadOrder" },
		{ GAMEPAD_BUTTON_EAST, "GUI:GamepadBack" }, { GAMEPAD_BUTTON_NORTH, "GUI:GamepadAttackMove" },
		{ GAMEPAD_BUTTON_RIGHT_TRIGGER, "GUI:GamepadCommands" }, { GAMEPAD_BUTTON_LEFT_SHOULDER, "GUI:GamepadIdleWorker" } };
	static const Prompt queue[] = { { GAMEPAD_BUTTON_SOUTH, "GUI:GamepadAdd" }, { GAMEPAD_BUTTON_WEST, "GUI:GamepadQueue" },
		{ GAMEPAD_BUTTON_DPAD_UP, "GUI:GamepadGroupsHigh" } };
	static const Prompt ctrl[] = { { GAMEPAD_BUTTON_SOUTH, "GUI:GamepadForceFire" }, { GAMEPAD_BUTTON_DPAD_UP, "GUI:GamepadMakeGroup" },
		{ GAMEPAD_BUTTON_EAST, "GUI:GamepadScatter" }, { GAMEPAD_BUTTON_LEFT_SHOULDER, "GUI:GamepadStop" } };
	static const Prompt camera[] = { { GAMEPAD_BUTTON_RIGHT_STICK, "GUI:GamepadZoomTurn" }, { GAMEPAD_BUTTON_NORTH, "GUI:GamepadArmy" },
		{ GAMEPAD_BUTTON_RIGHT_SHOULDER, "GUI:GamepadStop" } };
	const Prompt *items = plain;
	Int count = sizeof( plain ) / sizeof( plain[0] );
	if (theLayer == GAMEPAD_BUTTON_LEFT_TRIGGER) { items = queue; count = sizeof( queue ) / sizeof( queue[0] ); }
	else if (theLayer == GAMEPAD_BUTTON_RIGHT_SHOULDER) { items = ctrl; count = sizeof( ctrl ) / sizeof( ctrl[0] ); }
	else if (theLayer == GAMEPAD_BUTTON_LEFT_SHOULDER) { items = camera; count = sizeof( camera ) / sizeof( camera[0] ); }

	// one glyph string and one word string a slot, handed new text only when it changes (a display string keeps the
	// texture its text was built into)
	enum { SLOTS = 6 };
	static DisplayString *glyphs[ SLOTS ] = { NULL }, *words[ SLOTS ] = { NULL };
	const Int points = TheDisplay->getHeight() / 55 > 11 ? TheDisplay->getHeight() / 55 : 11;
	GameFont *wordFont = TheFontLibrary->getFont( AsciiString( "Arial" ), points, TRUE );
	if (wordFont == NULL)
		return;
	Int widths[ SLOTS ], total = 0, rowHeight = 0;
	Bool shown[ SLOTS ];
	for (Int i = 0; i < count && i < SLOTS; ++i)
	{
		GameFont *glyphFont = NULL;
		UnicodeString glyph;
		shown[i] = FALSE;
		if (!glyphFor( theShown, items[i].button, points * 2, glyphFont, glyph ))
			continue;
		if (glyphs[i] == NULL)
			glyphs[i] = TheDisplayStringManager->newDisplayString();
		if (words[i] == NULL)
			words[i] = TheDisplayStringManager->newDisplayString();
		if (glyphs[i] == NULL || words[i] == NULL)
			continue;
		const UnicodeString word = TheGameText != NULL ? TheGameText->fetch( items[i].label ) : UnicodeString::TheEmptyString;
		if (glyphs[i]->getFont() != glyphFont)
			glyphs[i]->setFont( glyphFont );
		if (glyphs[i]->getText() != glyph)
			glyphs[i]->setText( glyph );
		if (words[i]->getFont() != wordFont)
			words[i]->setFont( wordFont );
		if (words[i]->getText() != word)
			words[i]->setText( word );
		Int gw, gh, ww, wh;
		glyphs[i]->getSize( &gw, &gh );
		words[i]->getSize( &ww, &wh );
		Int inkTop, inkBottom;
		inkRows( theShown, items[i].button, gh, inkTop, inkBottom );
		rowHeight = inkBottom - inkTop > rowHeight ? inkBottom - inkTop : rowHeight;
		rowHeight = wh > rowHeight ? wh : rowHeight;
		widths[i] = gw + 4 + ww;
		total += widths[i] + (total > 0 ? points : 0);
		shown[i] = TRUE;
	}
	if (total == 0)
		return;
	// along the top from the left, on a plate: right of the menu button in the corner (it grows with the screen: 24
	// pixels at 600 lines, 40 at 1080), and clear of the diagnostic text on the right
	const Int pad = points / 2, centreY = pad + rowHeight / 2 + 2;
	const Int menuEdge = (Int)TheDisplay->getHeight() / 24 + 6;
	Int x = (menuEdge > 34 ? menuEdge : 34) + pad;
	TheDisplay->drawFillRect( x - pad, 2, total + 2 * pad, rowHeight + 2 * pad, GameMakeColor( 0, 0, 0, 150 ) );
	for (Int i = 0; i < count && i < SLOTS; ++i)
	{
		if (!shown[i])
			continue;
		Int gw, gh, ww, wh;
		glyphs[i]->getSize( &gw, &gh );
		words[i]->getSize( &ww, &wh );
		glyphs[i]->draw( x, glyphTop( theShown, items[i].button, gh, centreY ), GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		words[i]->draw( x + gw + 4, centreY - wh / 2, GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		x += widths[i] + points;
	}
}

void GamepadHints::drawTooltipCorner( const IRegion2D &box )
{
	if (theShown == GAMEPAD_GLYPHS_NONE || !theCommandBarMode || TheDisplay == NULL || TheDisplayStringManager == NULL)
		return;
	GameFont *font = NULL;
	UnicodeString glyph;
	const Int pointSize = TheDisplay->getHeight() / 30 > 16 ? TheDisplay->getHeight() / 30 : 16;
	if (!glyphFor( theShown, GAMEPAD_BUTTON_SOUTH, pointSize, font, glyph ))
		return;
	// one string, handed a new glyph only when the pad's family changes: a display string keeps the texture
	// its text was built into (W3DPushButton.cpp's badgeString says why that matters)
	static DisplayString *theCorner = NULL;
	if (theCorner == NULL)
		theCorner = TheDisplayStringManager->newDisplayString();
	if (theCorner == NULL)
		return;
	if (theCorner->getFont() != font)
		theCorner->setFont( font );
	if (theCorner->getText() != glyph)
		theCorner->setText( glyph );
	Int width = 0, height = 0;
	theCorner->getSize( &width, &height );
	Int inkTop, inkBottom;
	inkRows( theShown, GAMEPAD_BUTTON_SOUTH, height, inkTop, inkBottom );
	theCorner->draw( box.hi.x - width - 4, box.hi.y - 4 - inkBottom, GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}
