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

// Win32GamepadOutput.h: the gamepad on Windows (G1b): SdlGamepadOutput.h's functions over Win32Mouse and
// DirectInputKeyboard (Win32GamepadOutput.cpp), and the window procedure's half of telling a hand from a pad.

#pragma once

#ifndef __WIN32GAMEPADOUTPUT_H
#define __WIN32GAMEPADOUTPUT_H

#include "Lib/BaseType.h"

void Win32GamepadOutput_stop( void );

/** Once an engine pass, after the window messages (Win32GameEngine::serviceWindowsOS): the first time the
	* command line has been read, SdlGamepad and, with ZH_INPUT_SCRIPT, the script's pad are started - a
	* -headless run starts neither unless it plays a script, as an -offscreen one off Windows (SdlGamepad.h);
	* then, every time, the script's next actions, SDL's gamepad events, and the pad's once-a-frame update. */
void Win32GamepadOutput_pump( void );

/** WndProc's WM_MOUSEMOVE, in client pixels: FALSE when it is to be dropped.  Windows repeats a move where the
	* pointer already is when a window appears, moves or changes under it; while the pad has the game's pointer
	* such a repeat is no hand's, and would pull the pointer back.  Any other move is a hand's
	* (SdlGamepad_noteMotion). */
Bool Win32GamepadOutput_handMotion( Int x, Int y );

#endif // __WIN32GAMEPADOUTPUT_H
