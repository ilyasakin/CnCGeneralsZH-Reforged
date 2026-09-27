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

// SdlInputScript.h: timed input for G1's test 2, from ZH_INPUT_SCRIPT (a test's switch, never a player's).
//
// The same match driven twice, once by a pad and once by hand, must end on the same CRC: the gamepad
// only produces what a mouse and keyboard produce (G1-gamepad.md, §5).  This plays a script of input
// keyed to the logic frame, in the first render pass after that frame, before SDL's events are polled:
//   - a pad's buttons and axes on SDL's virtual joystick, which SdlGamepad then reads as any pad;
//   - a hand's mouse and keys as SDL's own events, pushed as a platform would send them.
// Either way it enters where the platform's input enters, and everything after is the game's own code.
//
//   # a comment
//   <frame> pad <South|East|...|DPadUp|...> down|up         GamepadMap's button names
//   <frame> pad axis <LeftX|LeftY|RightX|RightY|LeftTrigger|RightTrigger> <-32768..32767>
//   <frame> mouse move <x> <y>                              the game's pixels
//   <frame> mouse <left|middle|right> down|up <x> <y>
//   <frame> key <SDL's scancode name, _ for a space: Left_Ctrl> down|up
//
// Each action goes to the debug log as it is played ("INPUT SCRIPT: frame F: ..."), so two runs show they
// played the same script on the same frames.

#pragma once

#ifndef __SDLINPUTSCRIPT_H
#define __SDLINPUTSCRIPT_H

#include "Lib/BaseType.h"

/// Reads ZH_INPUT_SCRIPT, and attaches the virtual pad if the script has pad lines; FALSE without one
Bool SdlInputScript_start( void );

/// Plays every action due by logicFrame, in order
void SdlInputScript_play( UnsignedInt logicFrame );

#endif // __SDLINPUTSCRIPT_H
