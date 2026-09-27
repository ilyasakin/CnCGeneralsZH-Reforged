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
#include "GameClient/GamepadHints.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Mouse.h"

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

/// A family's font once it is installed, and its code points by glyph name
struct LoadedFont
{
	Bool tried;
	Bool ok;
	std::map<std::string, WideChar> codes;
};

LoadedFont theLoaded[ GAMEPAD_GLYPHS_COUNT ];
GamepadGlyphSet theShown = GAMEPAD_GLYPHS_NONE;
Bool theCommandBarMode = FALSE;

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

LoadedFont &loaded( GamepadGlyphSet set )
{
	LoadedFont &font = theLoaded[ set ];
	if (!font.tried)
	{
		font.tried = TRUE;
		font.ok = theFonts[ set ].file != NULL && readMap( theFonts[ set ].map, font.codes )
			&& GlobalLanguage_installFontFile( AsciiString( theFonts[ set ].file ) );
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

/// The slot of the bar's top-left shown button, where North's glyph goes; worked out once a frame
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

const char *GamepadHints::glyphName( GamepadGlyphSet set, Int button )
{
	if (set <= GAMEPAD_GLYPHS_NONE || set >= GAMEPAD_GLYPHS_COUNT || button < 0 || button >= GAMEPAD_GLYPH_COUNT)
		return NULL;
	return theGlyphNames[ set ][ button ];
}

Bool GamepadHints::glyphFor( GamepadGlyphSet set, Int button, Int pointSize, GameFont *&font, UnicodeString &glyph )
{
	const char *name = glyphName( set, button );
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

GamepadHints::Hint GamepadHints::hintFor( GameWindow *window, Int pointSize, GameFont *&font, UnicodeString &glyph )
{
	if (theShown == GAMEPAD_GLYPHS_NONE || window == NULL)
		return HINT_TEXT;
	const Int id = window->winGetWindowId();

	// a message box's answers: South's glyph beside OK or Yes, East's beside Cancel or No
	const Int answer = answerOf( id );
	if (answer != 0)
		return glyphFor( theShown, answer > 0 ? GAMEPAD_BUTTON_SOUTH : GAMEPAD_BUTTON_EAST, pointSize, font, glyph )
			? HINT_BESIDE : HINT_TEXT;

	// the command bar: its letters are keys the pad has not
	if (commandSlotOf( id ) < 0)
		return HINT_TEXT;
	if (theCommandBarMode)
	{
		const MouseIO *mouse = TheMouse != NULL ? TheMouse->getMouseStatus() : NULL;
		if (mouse != NULL && window->winPointInWindow( mouse->pos.x, mouse->pos.y )
				&& glyphFor( theShown, GAMEPAD_BUTTON_SOUTH, pointSize, font, glyph ))
			return HINT_INSTEAD;
		return HINT_HIDE;
	}
	if (commandSlotOf( id ) == firstShownSlot() && glyphFor( theShown, GAMEPAD_BUTTON_NORTH, pointSize, font, glyph ))
		return HINT_INSTEAD;
	return HINT_HIDE;
}

void GamepadHints::drawTooltipCorner( const IRegion2D &box )
{
	if (theShown == GAMEPAD_GLYPHS_NONE || !theCommandBarMode || TheDisplay == NULL || TheDisplayStringManager == NULL)
		return;
	GameFont *font = NULL;
	UnicodeString glyph;
	const Int pointSize = TheDisplay->getHeight() / 40 > 12 ? TheDisplay->getHeight() / 40 : 12;
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
	theCorner->draw( box.hi.x - width - 4, box.hi.y - height - 2, GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}
