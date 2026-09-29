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
// already produce.  So this has exactly two outputs, the platform's mouse and keyboard, reached through
// SdlGamepadOutput.h: off Windows the calls SDL's own mouse and key events make (SdlMouse::addEvent and
// SdlKeyboard::addKey), on Windows what a window message and DirectInput give Win32Mouse and
// DirectInputKeyboard.  Everything after them - Mouse and Keyboard turning them into MSG_RAW_* messages,
// the command maps, the translators - is the code a mouse and keyboard run.
//
//   - Buttons follow GamepadMap (Data\INI\GamepadReforged.ini): a mouse button pressed where the pointer
//     is, a key with its modifiers, or a command: the key the command map binds it to, or where the
//     command grid took that key, what the key's place gives (GamepadMap.h).  A second
//     press within the system's double-click time and 4 pixels counts as a double click, as SDL counts
//     a mouse's.  A chord (a binding With a held button) lets that button's own keys go while it lasts.
//   - The triggers are buttons, pressed past half their pull: RT is the command bar (a tap) and the radial
//     command menu (held), LT a modifier.  The right stick holds the arrow keys, with some hysteresis;
//     with LB held (its camera layer) it zooms, as the wheel does, and turns the camera instead.
//   - The left stick moves the game's own pointer, and while a pad is in use the game draws the cursor itself
//     (Mouse.ini's polygon images, RM_POLYGON), never the platform's: a warp of the platform's pointer does
//     not come back on gamescope's Xwayland (the Steam Deck's Game Mode, measured).  A radial dead zone, a
//     squared response, full tilt crossing the screen's width in 1.2 s of real time.  A real mouse or
//     trackpad gives the platform's cursor back.  A pointer parked in the edge-scrolling band (the gamescope
//     finding: (0, 0)) is put at the screen's centre when the pad is first used.
//   - Command-bar mode (GamepadMap's CommandBar, RT's tap): the pointer goes to the command grid's
//     first place that holds something, the D-pad moves it along the grid's rows and columns (the page's
//     order keys included), South clicks, and East leaves.  The pointer is really over the place, so its
//     hover and its tooltip show as for a mouse.
//   - A message box up: South moves the pointer onto its OK or Yes and clicks, East onto its Cancel or
//     No.  A click, not a message to the box: the mouse's own way to answer it.
//   - A key or mouse button held by two things at once (two pads, a shoulder and a chord) goes down once
//     and up when the last lets go.  A pad pulled out, or the window losing the focus, lets go of all
//     it holds, so nothing is left stuck down.
//
// Only a game with a window starts it (SdlGameEngine; on Windows Win32GameEngine), and an -offscreen one
// only for a test's virtual pad (ZH_INPUT_SCRIPT); a -headless run never does, unless it plays such a pad.

#pragma once

#ifndef __SDLGAMEPAD_H
#define __SDLGAMEPAD_H

#include "Lib/BaseType.h"

union SDL_Event;

/// SDL's gamepad subsystem; FALSE when it cannot start (the game runs on without gamepads)
Bool SdlGamepad_start( void );
void SdlGamepad_stop( void );
/// Did SdlGamepad_start start it: Windows pumps SDL's events only then (Win32GameEngine)
Bool SdlGamepad_isStarted( void );

/// One event; TRUE when it was a gamepad's or a joystick's and was taken
Bool SdlGamepad_dispatch( const SDL_Event &event );

/// Once a frame after the events: the triggers' presses, and the right stick's keys or, with the camera
/// layer held, its wheel and turn, at nowMs
void SdlGamepad_update( UnsignedInt nowMs );

/// Lets go of every key and mouse button a gamepad holds (the focus lost; a pad pulled out)
void SdlGamepad_releaseAll( void );

/// The pads open now
Int SdlGamepad_count( void );

/** SdlInput's motion, in the game's pixels, before SdlMouse takes it: a hand on the mouse or a trackpad (the
	* pad never moves the platform's pointer), which then has the pointer and the platform's cursor */
void SdlGamepad_noteMotion( Int x, Int y );

/// SdlInput's key, mouse button or wheel: a hand is on the keyboard or the mouse
void SdlGamepad_noteHand( void );

/// TRUE while a pad, not the keyboard or mouse, was the last thing used (the button hints show then)
Bool SdlGamepad_isLastUsed( void );

/// The pointer the pad moves, in the game's pixels; FALSE while the mouse has it
Bool SdlGamepad_pointer( Int &x, Int &y );

/// TRUE while command-bar mode has the D-pad
Bool SdlGamepad_inCommandBar( void );

/** Command-bar mode's step over points: of count centres, the one from (x, y) in the direction (dx, dy)
	* (one of them 0, the other 1 or -1) by GamepadFocus::pickNeighbour's rule (one in line first, else the
	* nearest in the cone); -1 when none */
Int SdlGamepad_pickNeighbour( const ICoord2D *centres, Int count, Int x, Int y, Int dx, Int dy );

#endif // __SDLGAMEPAD_H
