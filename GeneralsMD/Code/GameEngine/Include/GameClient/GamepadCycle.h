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
// GamepadCycle.h: a pad's way round the player's base.
//
// View held with RB or LB (GamepadReforged.ini) steps through the local player's production buildings - the
// command centre, barracks, war factory, airfield and the rest that build something - in a fixed order (by
// object id), selecting each and looking at it.  The selection is the message a click on that building sends
// (MSG_CREATE_SELECTED_GROUP, a new group of one), as InGameUI::selectNextIdleWorker sends for the idle-worker
// key; the look is the camera's, on the client.  Buildings still going up are left out.

#pragma once

#ifndef __GAMEPADCYCLE_H
#define __GAMEPADCYCLE_H

#include "Lib/BaseType.h"

class GamepadCycle
{
public:
	/// Selects the production building step places on (1 the next, -1 the previous) from the selected one; with none
	/// of them selected, the first (or the last).  FALSE outside a match or with none to select.
	static Bool structure( Int step );

	/// The index step places on from current among count, wrapping; current -1 (none) starts at the first or the last
	static Int stepFrom( Int current, Int count, Int step );
};

#endif // __GAMEPADCYCLE_H
