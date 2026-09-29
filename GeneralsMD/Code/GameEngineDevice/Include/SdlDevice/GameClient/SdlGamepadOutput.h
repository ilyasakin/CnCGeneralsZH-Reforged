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

// SdlGamepadOutput.h: where a gamepad's input goes, the platform's own mouse and keyboard (G1, G1b).
//
// SdlGamepad reads the pads through SDL3 on every platform, and turns them into what a mouse and a keyboard
// produce (SdlGamepad.h).  What it hands them to is the platform's: off Windows SdlMouse and SdlKeyboard,
// the devices SDL's own mouse and key events fill (SdlGamepadOutput.cpp); on Windows Win32Mouse, as a window
// message would, and DirectInputKeyboard (Win32GamepadOutput.cpp).  Each side supplies these functions.
//
// The hand's half is here too, for G1's test 2 (SdlInputScript.h): a script's mouse and key lines arrive
// the way a real mouse's and keyboard's do on that platform, and so tell the pad that a hand has the game.

#pragma once

#ifndef __SDLGAMEPADOUTPUT_H
#define __SDLGAMEPADOUTPUT_H

#include "Lib/BaseType.h"

/// A mouse button, in SdlMouse::Button's order
enum SdlGamepadOutputButton
{
	GAMEPAD_OUTPUT_LEFT,
	GAMEPAD_OUTPUT_MIDDLE,
	GAMEPAD_OUTPUT_RIGHT,
	GAMEPAD_OUTPUT_BUTTON_COUNT
};

/// Is there a mouse to take the pad's pointer and clicks
Bool SdlGamepadOutput_haveMouse( void );
/// The pointer to a pixel of the game's screen, as the platform's own motion would arrive
void SdlGamepadOutput_mouseMove( Int x, Int y, UnsignedInt time );
/// A button down or up at a pixel; clicks is SDL's count, 2 for the second press of a double click
void SdlGamepadOutput_mouseButton( SdlGamepadOutputButton button, Bool down, Int x, Int y, Int clicks, UnsignedInt time );
/// The wheel at a pixel, 120 a notch
void SdlGamepadOutput_mouseWheel( Int x, Int y, Int delta, UnsignedInt time );
/// Where the mouse last put the pointer
void SdlGamepadOutput_mousePosition( Int &x, Int &y );
/// A key down or up (a DirectInput code)
void SdlGamepadOutput_key( UnsignedByte dik, Bool down );
/// The game's window in pixels, for the screen's size before there is a display; FALSE without one
Bool SdlGamepadOutput_windowSize( Int &width, Int &height );

/// The hand's input, for SdlInputScript: the pointer moved, a mouse button (x and y where it was pressed)
void SdlGamepadOutput_handMouseMove( Int x, Int y );
void SdlGamepadOutput_handMouseButton( SdlGamepadOutputButton button, Bool down, Int x, Int y );
/// A key by SDL's scancode name ("Left Ctrl"); FALSE for a name SDL or the engine does not know
Bool SdlGamepadOutput_handKey( const char *name, Bool down );

#endif // __SDLGAMEPADOUTPUT_H
