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

// SdlGamepadOutput.cpp: see SdlGamepadOutput.h.  Off Windows: SdlMouse and SdlKeyboard, and the hand's
// input as SDL's own events, which SdlInput_dispatch takes as it takes a real mouse's and keyboard's.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "SdlDevice/GameClient/SdlGamepadOutput.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"
#include "SdlDevice/GameClient/SdlMouse.h"

#include <SDL3/SDL.h>

static_assert( (Int)GAMEPAD_OUTPUT_LEFT == (Int)SdlMouse::BUTTON_LEFT, "SdlGamepadOutput.h follows SdlMouse::Button" );
static_assert( (Int)GAMEPAD_OUTPUT_MIDDLE == (Int)SdlMouse::BUTTON_MIDDLE, "SdlGamepadOutput.h follows SdlMouse::Button" );
static_assert( (Int)GAMEPAD_OUTPUT_RIGHT == (Int)SdlMouse::BUTTON_RIGHT, "SdlGamepadOutput.h follows SdlMouse::Button" );

namespace {

void pushMouse( Uint32 type, Int x, Int y, Uint8 button, bool down )
{
	SDL_Window *window = SdlInput_gameWindow();
	Real wx, wy;
	SdlInput_toWindowPoints( x, y, wx, wy );
	SDL_Event event;
	SDL_zero( event );
	event.type = type;
	if (type == SDL_EVENT_MOUSE_MOTION)
	{
		event.motion.windowID = window != NULL ? SDL_GetWindowID( window ) : 0;
		event.motion.x = wx;
		event.motion.y = wy;
	}
	else
	{
		event.button.windowID = window != NULL ? SDL_GetWindowID( window ) : 0;
		event.button.button = button;
		event.button.down = down;
		event.button.clicks = 1;
		event.button.x = wx;
		event.button.y = wy;
	}
	SDL_PushEvent( &event );
}

}  // namespace

Bool SdlGamepadOutput_haveMouse( void )
{
	return SdlMouse::active() != NULL;
}

void SdlGamepadOutput_mouseMove( Int x, Int y, UnsignedInt time )
{
	if (SdlMouse::active() != NULL)
		SdlMouse::active()->addEvent( SdlMouse::EVENT_MOVE, x, y, SdlMouse::BUTTON_LEFT, 0, 0, time );
}

void SdlGamepadOutput_mouseButton( SdlGamepadOutputButton button, Bool down, Int x, Int y, Int clicks, UnsignedInt time )
{
	if (SdlMouse::active() != NULL)
		SdlMouse::active()->addEvent( down ? SdlMouse::EVENT_BUTTON_DOWN : SdlMouse::EVENT_BUTTON_UP, x, y,
			(SdlMouse::Button)button, clicks, 0, time );
}

void SdlGamepadOutput_mouseWheel( Int x, Int y, Int delta, UnsignedInt time )
{
	if (SdlMouse::active() != NULL)
		SdlMouse::active()->addEvent( SdlMouse::EVENT_WHEEL, x, y, SdlMouse::BUTTON_LEFT, 0, delta, time );
}

void SdlGamepadOutput_mousePosition( Int &x, Int &y )
{
	x = y = 0;
	if (SdlMouse::active() != NULL)
		SdlMouse::active()->getPointerPosition( x, y );
}

void SdlGamepadOutput_key( UnsignedByte dik, Bool down )
{
	if (SdlKeyboard::active() != NULL)
		SdlKeyboard::active()->addKey( dik, down );
}

Bool SdlGamepadOutput_windowSize( Int &width, Int &height )
{
	SDL_Window *window = SdlInput_gameWindow();
	return window != NULL && SDL_GetWindowSize( window, &width, &height );
}

void SdlGamepadOutput_handMouseMove( Int x, Int y )
{
	pushMouse( SDL_EVENT_MOUSE_MOTION, x, y, 0, false );
}

void SdlGamepadOutput_handMouseButton( SdlGamepadOutputButton button, Bool down, Int x, Int y )
{
	const Uint8 sdlButton = button == GAMEPAD_OUTPUT_LEFT ? SDL_BUTTON_LEFT
		: (button == GAMEPAD_OUTPUT_MIDDLE ? SDL_BUTTON_MIDDLE : SDL_BUTTON_RIGHT);
	pushMouse( down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP, x, y, sdlButton, down );
}

Bool SdlGamepadOutput_handKey( const char *name, Bool down )
{
	const SDL_Scancode scancode = SDL_GetScancodeFromName( name );
	if (scancode == SDL_SCANCODE_UNKNOWN)
		return FALSE;
	SDL_Window *window = SdlInput_gameWindow();
	SDL_Event event;
	SDL_zero( event );
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = window != NULL ? SDL_GetWindowID( window ) : 0;
	event.key.scancode = scancode;
	event.key.down = down;
	SDL_PushEvent( &event );
	return TRUE;
}
