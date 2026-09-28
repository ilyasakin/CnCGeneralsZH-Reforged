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
// GamepadRadial.h: the radial command menu for a pad in a match (docs/mac-port/tasks/G1-gamepad-screens.md, "The
// radial command menu"), the way console RTS games put the command card under the thumb.
//
// Y held opens a ring of the command bar's shown buttons around the screen's centre: the selection's command card
// (production, abilities, upgrades, a dozer's structures), greyed where the bar has them disabled.  The left stick
// picks a sector, 0 at the top and on clockwise; letting Y go presses the pick, A presses it at once, B closes.
// A press is the command bar's own (ControlBar::pressCommandWindow, the GBM_SELECTED a click on that button sends),
// so the orders that follow are the ones a mouse on the bar gives.  A tap of Y that picks nothing is still the
// command-bar mode it was before (SdlGamepad.h).

#pragma once

#ifndef __GAMEPADRADIAL_H
#define __GAMEPADRADIAL_H

#include "Lib/BaseType.h"

class GamepadRadial
{
public:
	/// Opens the ring on the command bar's shown buttons; FALSE outside a match or with none shown
	static Bool open( void );
	static void close( void );
	static Bool isOpen( void );

	/// The left stick's tilt picks a sector; inside PICK_TILT the pick stays where it was
	static void aim( Real stickX, Real stickY );
	static Bool hasPick( void );

	/// Presses the picked sector's button as a click on it would; FALSE for no pick, or a button gone
	static Bool activate( void );

	/// The ring, drawn over the world and the GUI, under the cursor
	static void draw( void );

	/// The sector a stick at x,y points to among count, 0 at the top and on clockwise (a stick's up is negative
	/// y); -1 inside PICK_TILT or with none
	static Int sectorFor( Real x, Real y, Int count );
};

#endif // __GAMEPADRADIAL_H
