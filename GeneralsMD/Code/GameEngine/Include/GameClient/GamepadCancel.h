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
// GamepadCancel.h: a pad's B in a match (docs/mac-port/tasks/G1-gamepad-screens.md, "The buttons in a match").
//
// Every console strategy game gives the right face button (the bottom one on a Nintendo pad) one meaning: take
// back.  Under Modern the mouse has no single button for that - a right click cancels what is armed and gives
// an order at the pointer as well, and the selection is cleared by a left click on empty ground - so the pad's
// Cancel does the taking back alone, the way those clicks do it:
//   - an ability or a special power waiting for its target goes (InGameUI::setGUICommand( NULL ), a right
//     button's release);
//   - a structure being placed goes, and the builder stays selected (placeBuildAvailable( NULL, NULL ));
//   - an armed attack, attack move or guard key goes (clearAttackMoveToMode);
//   - with none of those, the selection is cleared: deselectAllDrawables, which sends MSG_DESTROY_SELECTED_GROUP
//     as a click on empty ground does.
// Only the last reaches the game logic, and as the same message a hand's click sends.

#pragma once

#ifndef __GAMEPADCANCEL_H
#define __GAMEPADCANCEL_H

#include "Lib/BaseType.h"

class GamepadCancel
{
public:
	enum Result
	{
		NOTHING = 0,		///< outside a match, or nothing to take back
		COMMAND,				///< an ability or power waiting for its target
		PLACEMENT,			///< a structure being placed
		ORDER_KEY,			///< an attack, attack move or guard key
		SELECTION				///< the selection
	};

	/// Takes back the first of those there is, and says which
	static Result press( void );
};

#endif // __GAMEPADCANCEL_H
