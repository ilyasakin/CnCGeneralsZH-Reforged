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
// GamepadCancel.cpp: see GamepadCancel.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GamepadCancel.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"

GamepadCancel::Result GamepadCancel::press( void )
{
	if (TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || TheInGameUI == NULL)
		return NOTHING;
	Result result = NOTHING;
	if (TheInGameUI->getGUICommand() != NULL)
	{
		TheInGameUI->setGUICommand( NULL );
		result = COMMAND;
	}
	else if (TheInGameUI->getPendingPlaceType() != NULL)
	{
		TheInGameUI->placeBuildAvailable( NULL, NULL );		// the builder stays selected, as after a right click
		result = PLACEMENT;
	}
	else if (TheInGameUI->isOrderKeyArmed())
	{
		TheInGameUI->clearAttackMoveToMode();
		result = ORDER_KEY;
	}
	else if (TheInGameUI->getSelectCount() > 0)
	{
		TheInGameUI->deselectAllDrawables();
		result = SELECTION;
	}
	DEBUG_LOG(( "GAMEPAD CANCEL: %s\n", result == COMMAND ? "the armed command" : result == PLACEMENT ? "the placement"
		: result == ORDER_KEY ? "the order key" : result == SELECTION ? "the selection" : "nothing" ));
	return result;
}
