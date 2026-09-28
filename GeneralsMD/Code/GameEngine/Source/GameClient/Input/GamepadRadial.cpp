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
// GamepadRadial.cpp: see GamepadRadial.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GamepadRadial.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"

#include <math.h>
#include <stdio.h>
#include <vector>

namespace {

const Real PICK_TILT = 0.5f;					///< the stick's tilt that picks a sector; less leaves the pick alone

Bool theOpen = FALSE;
std::vector<Int> thePlaces;						///< the ring's command grid places (ControlBar.h's CommandPlace), in reading order
Int thePick = -1;										///< the picked sector, an index into thePlaces, or -1

GameWindow *commandWindow( Int slot )
{
	if (TheWindowManager == NULL || TheNameKeyGenerator == NULL)
		return NULL;
	char name[ 64 ];
	snprintf( name, sizeof( name ), "ControlBar.wnd:ButtonCommand%02d", slot + 1 );
	return TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( name ) );
}

/// Shown: it and every window it sits in
Bool shown( GameWindow *window )
{
	if (window == NULL)
		return FALSE;
	for (; window != NULL; window = window->winGetParent())
		if (window->winIsHidden())
			return FALSE;
	return TRUE;
}

/// The command window standing at a grid place now, or NULL: an empty place, or one of the page's order keys
GameWindow *windowAt( Int place )
{
	Int where[ MAX_COMMANDS_PER_SET ];
	TheControlBar->getCommandPlaces( where );
	for (Int slot = 0; slot < MAX_COMMANDS_PER_SET; ++slot)
		if (where[ slot ] == place)
			return commandWindow( slot );
	return NULL;
}

/// An order key's name (the page's attack, hold position and move, which have no window and no label of their own)
UnicodeString orderName( Int place )
{
	const char *label = place == COMMAND_PLACE_ATTACK ? "GUI:GamepadForceFire"
		: place == COMMAND_PLACE_HOLD ? "GUI:GamepadHold" : place == COMMAND_PLACE_MOVE ? "GUI:GamepadMove" : NULL;
	return label != NULL && TheGameText != NULL ? TheGameText->fetch( label ) : UnicodeString::TheEmptyString;
}

Bool inMatch( void )
{
	return TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && TheControlBar != NULL;
}

/// The picked command's name, as the bar's tooltip names it, without its hotkey mark
UnicodeString pickName( GameWindow *window )
{
	const CommandButton *command = window != NULL ? (const CommandButton *)GadgetButtonGetData( window ) : NULL;
	if (command == NULL || command->getTextLabel().isEmpty() || TheGameText == NULL)
		return UnicodeString::TheEmptyString;
	// a label marks its hotkey with & ("&Barracks"), and && is an ampersand itself
	const UnicodeString label = TheGameText->fetch( command->getTextLabel() );
	UnicodeString name;
	for (const WideChar *c = label.str(); *c != 0; ++c)
	{
		if (*c == '&' && c[1] != '&')
			continue;
		if (*c == '&')
			++c;
		name.concat( *c );
	}
	return name;
}

}  // namespace

Bool GamepadRadial::open( void )
{
	close();
	if (!inMatch())
		return FALSE;
	// the grid's places that hold something, in place order, which is the way the grid reads: row by row
	IRegion2D rects[ COMMAND_PLACE_COUNT ];
	Int holds[ COMMAND_PLACE_COUNT ];
	if (InGameUI_commandPlaces( rects, holds ))
		for (Int place = 0; place < COMMAND_PLACE_COUNT; ++place)
			if (holds[ place ] == COMMAND_PLACE_HOLDS_ORDER || (holds[ place ] == COMMAND_PLACE_HOLDS_WINDOW && shown( windowAt( place ) )))
				thePlaces.push_back( place );
	theOpen = !thePlaces.empty();
	return theOpen;
}

void GamepadRadial::close( void )
{
	theOpen = FALSE;
	thePlaces.clear();
	thePick = -1;
}

Bool GamepadRadial::isOpen( void )
{
	return theOpen;
}

void GamepadRadial::aim( Real stickX, Real stickY )
{
	const Int sector = sectorFor( stickX, stickY, (Int)thePlaces.size() );
	if (theOpen && sector >= 0)
		thePick = sector;
}

Bool GamepadRadial::hasPick( void )
{
	return theOpen && thePick >= 0;
}

Bool GamepadRadial::activate( void )
{
	if (!hasPick() || !inMatch())
		return FALSE;
	// the place pressed as its key presses it (ControlBar::pressCommandButton): a command button as a click on it,
	// an order's place as its order's message
	const Int place = thePlaces[ thePick ];
	IRegion2D rects[ COMMAND_PLACE_COUNT ];
	Int holds[ COMMAND_PLACE_COUNT ];
	if (!InGameUI_commandPlaces( rects, holds ) || holds[ place ] == COMMAND_PLACE_HOLDS_NOTHING)
		return FALSE;		// the selection changed under the ring: its place is empty now
	GameWindow *window = holds[ place ] == COMMAND_PLACE_HOLDS_WINDOW ? windowAt( place ) : NULL;
	WinInstanceData *data = window != NULL ? window->winGetInstanceData() : NULL;		// the window pressed names itself
	DEBUG_LOG(( "GAMEPAD RADIAL: pressed %s, place %d, sector %d of %d, centre %d,%d\n",
		data != NULL ? data->m_decoratedNameString.str() : "an order key", place, thePick, (Int)thePlaces.size(),
		(rects[ place ].lo.x + rects[ place ].hi.x) / 2, (rects[ place ].lo.y + rects[ place ].hi.y) / 2 ));
	TheControlBar->pressCommandButton( place );
	return TRUE;
}

Int GamepadRadial::sectorFor( Real x, Real y, Int count )
{
	if (count <= 0 || x * x + y * y < PICK_TILT * PICK_TILT)
		return -1;
	Real angle = atan2f( x, -y );		// 0 at the top, on clockwise
	if (angle < 0.0f)
		angle += TWO_PI;
	const Real width = TWO_PI / count;
	return ((Int)floorf( (angle + width / 2.0f) / width )) % count;
}

void GamepadRadial::draw( void )
{
	if (!theOpen || TheDisplay == NULL)
		return;
	const Int count = (Int)thePlaces.size();
	const Int width = TheDisplay->getWidth(), height = TheDisplay->getHeight();
	const Real ring = height * 0.22f;
	const Int iconHeight = height / 11;
	const Color frame = GameMakeColor( 255, 210, 60, 255 );
	UnicodeString pickText;
	GameFont *labelFont = TheFontLibrary != NULL ? TheFontLibrary->getFont( AsciiString( "Arial" ), height / 60 > 9 ? height / 60 : 9, TRUE ) : NULL;
	static DisplayString *orderLabels[ COMMAND_PLACE_COUNT ] = { NULL };
	for (Int i = 0; i < count; ++i)
	{
		const Int place = thePlaces[i];
		GameWindow *window = windowAt( place );
		const Bool orderKey = window == NULL || !shown( window );		// the page's attack, hold position or move
		Int w = 0, h = 0;
		if (!orderKey)
			window->winGetSize( &w, &h );
		const Int iconWidth = h > 0 ? iconHeight * w / h : iconHeight;
		const Real angle = i * TWO_PI / count;
		const Int x = width / 2 + (Int)(ring * sinf( angle )) - iconWidth / 2;
		const Int y = height / 2 - (Int)(ring * cosf( angle )) - iconHeight / 2;
		TheDisplay->drawFillRect( x - 3, y - 3, iconWidth + 6, iconHeight + 6, GameMakeColor( 0, 0, 0, 170 ) );
		if (orderKey)
		{
			// no picture of its own: its name, in the square
			DisplayString *&label = orderLabels[ place ];
			if (label == NULL && TheDisplayStringManager != NULL)
				label = TheDisplayStringManager->newDisplayString();
			const UnicodeString text = orderName( place );
			if (label != NULL && labelFont != NULL && !text.isEmpty())
			{
				if (label->getFont() != labelFont)
					label->setFont( labelFont );
				if (label->getText() != text)
					label->setText( text );
				label->setWordWrap( iconWidth - 4 );
				Int textWidth, textHeight;
				label->getSize( &textWidth, &textHeight );
				label->draw( x + iconWidth / 2 - textWidth / 2, y + iconHeight / 2 - textHeight / 2, GameMakeColor( 255, 255, 255, 255 ),
					GameMakeColor( 0, 0, 0, 255 ) );
			}
		}
		else
		{
			const Image *image = GadgetButtonGetEnabledImage( window );
			if (image != NULL)
				TheDisplay->drawImage( image, x, y, x + iconWidth, y + iconHeight );
			if (!BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ))
				TheDisplay->drawFillRect( x, y, iconWidth, iconHeight, GameMakeColor( 0, 0, 0, 150 ) );		// greyed, as the bar has it
		}
		if (i == thePick)
		{
			TheDisplay->drawOpenRect( x - 4, y - 4, iconWidth + 8, iconHeight + 8, 3.0f, frame );
			pickText = orderKey ? orderName( place ) : pickName( window );
		}
	}

	// the pick's name in the middle
	static DisplayString *name = NULL;
	const UnicodeString &text = pickText;
	if (text.isEmpty() || TheDisplayStringManager == NULL || TheFontLibrary == NULL)
		return;
	if (name == NULL)
		name = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheFontLibrary->getFont( AsciiString( "Arial" ), height / 40 > 11 ? height / 40 : 11, TRUE );
	if (name == NULL || font == NULL)
		return;
	if (name->getFont() != font)
		name->setFont( font );
	if (name->getText() != text)
		name->setText( text );
	Int textWidth, textHeight;
	name->getSize( &textWidth, &textHeight );
	TheDisplay->drawFillRect( width / 2 - textWidth / 2 - 6, height / 2 - textHeight / 2 - 3, textWidth + 12, textHeight + 6,
		GameMakeColor( 0, 0, 0, 190 ) );
	name->draw( width / 2 - textWidth / 2, height / 2 - textHeight / 2, GameMakeColor( 255, 255, 255, 255 ),
		GameMakeColor( 0, 0, 0, 255 ) );
}
