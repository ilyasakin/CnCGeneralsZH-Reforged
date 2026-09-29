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

// Win32GamepadOutput.cpp: see Win32GamepadOutput.h and SdlGamepadOutput.h.
//
// The pad's mouse is Win32Mouse, given the window messages a mouse would send (addWin32Event, as WndProc
// does); its keys are DirectInputKeyboard's, from a queue ahead of DirectInput's.  Windows' own pointer is
// never moved: the game draws the cursor while a pad drives (SdlGamepad.cpp's drawCursor), as off Windows.
// The time on each message is the message clock, GetTickCount's, as GetMessageTime gives WndProc's.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <windows.h>

#include "Common/GlobalData.h"
#include "GameClient/Keyboard.h"
#include "GameLogic/GameLogic.h"
#include "SdlDevice/GameClient/SdlGamepad.h"
#include "SdlDevice/GameClient/SdlGamepadOutput.h"
#include "SdlDevice/GameClient/SdlInputScript.h"
#include "SdlDevice/GameClient/SdlKeyTable.h"
#include "Win32Device/GameClient/Win32DIKeyboard.h"
#include "Win32Device/GameClient/Win32GamepadOutput.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "WinMain.h"

#include <SDL3/SDL.h>

#include <stdlib.h>

extern Win32Mouse *TheWin32Mouse;

namespace {

Bool theHandKnown = FALSE;
POINT theHand;		///< where the last hand's WM_MOUSEMOVE put Windows' pointer, on the screen

LPARAM clientPoint( Int x, Int y )
{
	return MAKELPARAM( (WORD)(SHORT)x, (WORD)(SHORT)y );
}

DirectInputKeyboard *keyboard( void )
{
	return dynamic_cast<DirectInputKeyboard *>( TheKeyboard );
}

void send( UINT message, WPARAM wParam, LPARAM lParam )
{
	if (TheWin32Mouse != NULL)
		TheWin32Mouse->addWin32Event( message, wParam, lParam, (DWORD)::GetTickCount() );
}

UINT buttonMessage( SdlGamepadOutputButton button, Bool down, Int clicks )
{
	static const UINT DOWN[ GAMEPAD_OUTPUT_BUTTON_COUNT ] = { WM_LBUTTONDOWN, WM_MBUTTONDOWN, WM_RBUTTONDOWN };
	static const UINT UP[ GAMEPAD_OUTPUT_BUTTON_COUNT ] = { WM_LBUTTONUP, WM_MBUTTONUP, WM_RBUTTONUP };
	static const UINT DOUBLE[ GAMEPAD_OUTPUT_BUTTON_COUNT ] = { WM_LBUTTONDBLCLK, WM_MBUTTONDBLCLK, WM_RBUTTONDBLCLK };
	if (!down)
		return UP[ button ];
	// Windows: down, up, double click, up, down, up...  SDL's count, as SdlMouse reads it
	return clicks >= 2 && clicks % 2 == 0 ? DOUBLE[ button ] : DOWN[ button ];
}

}  // namespace

// ---- SdlGamepadOutput.h: the pad's input ------------------------------------------------------------------

Bool SdlGamepadOutput_haveMouse( void )
{
	return TheWin32Mouse != NULL;
}

void SdlGamepadOutput_mouseMove( Int x, Int y, UnsignedInt /*time*/ )
{
	send( WM_MOUSEMOVE, 0, clientPoint( x, y ) );
}

void SdlGamepadOutput_mouseButton( SdlGamepadOutputButton button, Bool down, Int x, Int y, Int clicks, UnsignedInt /*time*/ )
{
	send( buttonMessage( button, down, clicks ), 0, clientPoint( x, y ) );
}

void SdlGamepadOutput_mouseWheel( Int x, Int y, Int delta, UnsignedInt /*time*/ )
{
	POINT screen = { x, y };		// WM_MOUSEWHEEL's position is the screen's (Win32Mouse::translateEvent)
	::ClientToScreen( ApplicationHWnd, &screen );
	send( WM_MOUSEWHEEL, MAKEWPARAM( 0, (WORD)(SHORT)delta ), clientPoint( screen.x, screen.y ) );
}

void SdlGamepadOutput_mousePosition( Int &x, Int &y )
{
	x = y = 0;
	if (TheWin32Mouse != NULL)
		TheWin32Mouse->getPointerPosition( x, y );
}

void SdlGamepadOutput_key( UnsignedByte dik, Bool down )
{
	if (keyboard() != NULL)
		keyboard()->addKey( dik, down );
}

Bool SdlGamepadOutput_windowSize( Int &width, Int &height )
{
	RECT client;
	if (ApplicationHWnd == NULL || !::GetClientRect( ApplicationHWnd, &client ))
		return FALSE;
	width = client.right - client.left;
	height = client.bottom - client.top;
	return TRUE;
}

// ---- SdlGamepadOutput.h: the hand's input, for the script, as WndProc and DirectInput give a real one's ----

void SdlGamepadOutput_handMouseMove( Int x, Int y )
{
	SdlGamepad_noteMotion( x, y );
	send( WM_MOUSEMOVE, 0, clientPoint( x, y ) );
}

void SdlGamepadOutput_handMouseButton( SdlGamepadOutputButton button, Bool down, Int x, Int y )
{
	SdlGamepad_noteHand();
	send( buttonMessage( button, down, 1 ), 0, clientPoint( x, y ) );
}

Bool SdlGamepadOutput_handKey( const char *name, Bool down )
{
	const UnsignedByte dik = SdlKeyTable_dikFor( SDL_GetScancodeFromName( name ) );
	if (dik == 0 || keyboard() == NULL)
		return FALSE;
	SdlGamepad_noteHand();
	keyboard()->addKey( dik, down );
	return TRUE;
}

// ---- Win32GamepadOutput.h -----------------------------------------------------------------------------

namespace {

void start( Bool headless )
{
	const char *script = ::getenv( "ZH_INPUT_SCRIPT" );
	if (headless && (script == NULL || *script == 0))
		return;
	// where Windows' pointer is now: a move reported there later is a repeat, not a hand
	theHandKnown = ::GetCursorPos( &theHand ) != 0;
	if (SdlGamepad_start())
		SdlInputScript_start();		// G1's test 2: ZH_INPUT_SCRIPT, never set by a player (SdlInputScript.h)
}

}  // namespace

void Win32GamepadOutput_stop( void )
{
	SdlGamepad_stop();
}

void Win32GamepadOutput_pump( void )
{
	// the engine's init(argc, argv) reads the command line, and this is its first chance after that
	static Bool tried = FALSE;
	if (!tried && TheGlobalData != NULL)
	{
		tried = TRUE;
		start( TheGlobalData->m_headless );
	}
	if (!SdlGamepad_isStarted())
		return;
	if (TheGameLogic != NULL)
		SdlInputScript_play( TheGameLogic->getFrame() );		// nothing without ZH_INPUT_SCRIPT
	SDL_Event event;
	while (SDL_PollEvent( &event ))
		SdlGamepad_dispatch( event );		// only a gamepad's or a joystick's: SDL has no window here
	SdlGamepad_update( (UnsignedInt)SDL_GetTicks() );		// the triggers and the right stick, once a frame
}

Bool Win32GamepadOutput_handMotion( Int x, Int y )
{
	POINT screen = { x, y };
	::ClientToScreen( ApplicationHWnd, &screen );
	const Bool repeat = theHandKnown && screen.x == theHand.x && screen.y == theHand.y;
	theHandKnown = TRUE;
	theHand = screen;
	if (repeat)
	{
		Int padX, padY;
		return !SdlGamepad_pointer( padX, padY );
	}
	SdlGamepad_noteMotion( x, y );
	return TRUE;
}
