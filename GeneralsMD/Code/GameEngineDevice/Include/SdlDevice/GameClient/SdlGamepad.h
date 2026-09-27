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

// SdlGamepad.h: SDL3's gamepads, turned into the mouse's and the keyboard's own input (G1).
//
// G1's rule (docs/mac-port/tasks/G1-gamepad.md): a gamepad only produces what the mouse and keyboard
// already produce.  So this has exactly two outputs, the calls SDL's own mouse and key events make:
// SdlMouse::addEvent and SdlKeyboard::addKey.  Everything after them - Mouse and Keyboard turning them
// into MSG_RAW_* messages, the command maps, the translators - is the code a mouse and keyboard run.
//
//   - Buttons follow GamepadMap (Data\INI\GamepadReforged.ini): a mouse button pressed where the pointer
//     is, a key with its modifiers, or the key the player's command map binds a command to.  A second
//     press within the system's double-click time and 4 pixels counts as a double click, as SDL counts
//     a mouse's.  A chord (a binding With a held button) lets that button's own keys go while it lasts.
//   - The triggers are the wheel, in proportion to how far they are pulled; the right stick holds the
//     arrow keys, with some hysteresis.  The left stick's pointer is G1's second part.
//   - A key or mouse button held by two things at once (two pads, a shoulder and a chord) goes down once
//     and up when the last lets go.  A pad pulled out, or the window losing the focus, lets go of all
//     it holds, so nothing is left stuck down.
//
// Only a game with a window starts it (SdlGameEngine), so -headless and -offscreen runs never do.

#pragma once

#ifndef __SDLGAMEPAD_H
#define __SDLGAMEPAD_H

#include "Lib/BaseType.h"

union SDL_Event;

/// SDL's gamepad subsystem; FALSE when it cannot start (the game runs on without gamepads)
Bool SdlGamepad_start( void );
void SdlGamepad_stop( void );

/// One event; TRUE when it was a gamepad's or a joystick's and was taken
Bool SdlGamepad_dispatch( const SDL_Event &event );

/// Once a frame after the events: the triggers' wheel and the right stick's keys, at nowMs
void SdlGamepad_update( UnsignedInt nowMs );

/// Lets go of every key and mouse button a gamepad holds (the focus lost; a pad pulled out)
void SdlGamepad_releaseAll( void );

/// The pads open now
Int SdlGamepad_count( void );

#endif // __SDLGAMEPAD_H
