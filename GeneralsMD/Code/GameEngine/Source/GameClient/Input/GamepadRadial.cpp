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
#include "GameLogic/GameLogic.h"

#include <math.h>
#include <stdio.h>
#include <vector>

namespace {

const Real PICK_TILT = 0.5f;					///< the stick's tilt that picks a sector; less leaves the pick alone

Bool theOpen = FALSE;
std::vector<Int> theSlots;						///< the ring's command windows, as the bar's slots (0 is ButtonCommand01)
Int thePick = -1;										///< the picked sector, an index into theSlots, or -1

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
		if (*c == L'&' && c[1] != L'&')
			continue;
		if (*c == L'&')
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
	for (Int slot = 0; slot < MAX_COMMANDS_PER_SET; ++slot)
		if (shown( commandWindow( slot ) ))
			theSlots.push_back( slot );
	theOpen = !theSlots.empty();
	return theOpen;
}

void GamepadRadial::close( void )
{
	theOpen = FALSE;
	theSlots.clear();
	thePick = -1;
}

Bool GamepadRadial::isOpen( void )
{
	return theOpen;
}

void GamepadRadial::aim( Real stickX, Real stickY )
{
	const Int sector = sectorFor( stickX, stickY, (Int)theSlots.size() );
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
	GameWindow *window = commandWindow( theSlots[ thePick ] );
	if (!shown( window ))
		return FALSE;		// the selection changed under the ring: its button is gone
	Int x, y, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );
	WinInstanceData *data = window->winGetInstanceData();		// the window pressed names itself
	DEBUG_LOG(( "GAMEPAD RADIAL: pressed %s, sector %d of %d, centre %d,%d\n", data != NULL ? data->m_decoratedNameString.str() : "?",
		thePick, (Int)theSlots.size(), x + w / 2, y + h / 2 ));
	TheControlBar->pressCommandWindow( window );
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
	const Int count = (Int)theSlots.size();
	const Int width = TheDisplay->getWidth(), height = TheDisplay->getHeight();
	const Real ring = height * 0.22f;
	const Int iconHeight = height / 11;
	const Color frame = GameMakeColor( 255, 210, 60, 255 );
	GameWindow *picked = NULL;
	for (Int i = 0; i < count; ++i)
	{
		GameWindow *window = commandWindow( theSlots[i] );
		if (!shown( window ))
			continue;
		Int w = 0, h = 0;
		window->winGetSize( &w, &h );
		const Int iconWidth = h > 0 ? iconHeight * w / h : iconHeight;
		const Real angle = i * TWO_PI / count;
		const Int x = width / 2 + (Int)(ring * sinf( angle )) - iconWidth / 2;
		const Int y = height / 2 - (Int)(ring * cosf( angle )) - iconHeight / 2;
		TheDisplay->drawFillRect( x - 3, y - 3, iconWidth + 6, iconHeight + 6, GameMakeColor( 0, 0, 0, 170 ) );
		const Image *image = GadgetButtonGetEnabledImage( window );
		if (image != NULL)
			TheDisplay->drawImage( image, x, y, x + iconWidth, y + iconHeight );
		if (!BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ))
			TheDisplay->drawFillRect( x, y, iconWidth, iconHeight, GameMakeColor( 0, 0, 0, 150 ) );		// greyed, as the bar has it
		if (i == thePick)
		{
			TheDisplay->drawOpenRect( x - 4, y - 4, iconWidth + 8, iconHeight + 8, 3.0f, frame );
			picked = window;
		}
	}

	// the pick's name in the middle
	static DisplayString *name = NULL;
	const UnicodeString text = pickName( picked );
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
