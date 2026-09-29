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
// GamepadAim.h: aim assist for a pad's pointer in a match (docs/mac-port/tasks/G1-gamepad-screens.md, "The cursor").
//
// A stick is a poor mouse for picking out one small unit, so the pointer is helped, on the client only:
//   - magnetism: while the stick rests, the pointer eases onto the nearest aimable thing within a small radius;
//   - friction: while the stick moves it, the pointer slows over one.
// Aimable is selectable, shown, and seen by the local player (their own, or neither fogged nor shrouded for
// them), so the pointer never finds what the player cannot see.  It only moves the pointer: the click that
// follows is the same click a mouse would make at that pixel, so the match's messages are a mouse's.
// Options > Controls > Controller Aim Assist (GlobalData::m_gamepadAim) turns both off.

#pragma once

#ifndef __GAMEPADAIM_H
#define __GAMEPADAIM_H

#include "Lib/BaseType.h"

class GamepadAim
{
public:
	/// The radius the pointer is drawn from, in pixels, for a screen this tall
	static Int radiusFor( Int screenHeight );

	/// Where a resting pointer at x,y is drawn to: the nearest aimable thing's point on screen within radius;
	/// FALSE for none, outside a match, or over the game's windows
	static Bool magnetTarget( Int x, Int y, Int radius, ICoord2D &target );

	/// TRUE when something aimable is within radius of x,y: the stick's pointer slows there
	static Bool nearTarget( Int x, Int y, Int radius );

	/// The nearest of count points to x,y within radius (inclusive), -1 for none; the first of equals
	static Int nearest( const ICoord2D *points, Int count, Int x, Int y, Int radius );

	/// One step of the ease toward target: the remaining distance shrinks by e^(-seconds / EASE_SECONDS), and
	/// the last pixel is closed outright, so it arrives and never overshoots
	static void easeToward( Real &x, Real &y, const ICoord2D &target, Real seconds );
};

#endif // __GAMEPADAIM_H
