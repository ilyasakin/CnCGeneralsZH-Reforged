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
// GamepadCycle.cpp: see GamepadCycle.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/KindOf.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/Drawable.h"
#include "GameClient/GamepadCycle.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include <algorithm>
#include <vector>

namespace {

Bool idBefore( const Object *a, const Object *b )
{
	return a->getID() < b->getID();
}

}  // namespace

Int GamepadCycle::stepFrom( Int current, Int count, Int step )
{
	if (count <= 0 || step == 0)
		return -1;
	if (current < 0 || current >= count)
		return step > 0 ? 0 : count - 1;
	return ((current + step) % count + count) % count;
}

Bool GamepadCycle::structure( Int step )
{
	if (TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || ThePlayerList == NULL
			|| TheInGameUI == NULL || TheMessageStream == NULL)
		return FALSE;
	const Player *local = ThePlayerList->getLocalPlayer();
	if (local == NULL)
		return FALSE;

	std::vector<Object *> buildings;
	for (Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject())
		if (obj->getControllingPlayer() == local && obj->isKindOf( KINDOF_STRUCTURE ) && obj->isSelectable()
				&& !obj->isEffectivelyDead() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
				&& obj->getProductionUpdateInterface() != NULL && obj->getDrawable() != NULL)
			buildings.push_back( obj );
	if (buildings.empty())
		return FALSE;
	std::sort( buildings.begin(), buildings.end(), idBefore );

	// where the one building selected is in the list, if it is one of them
	Int current = -1;
	Drawable *selected = TheInGameUI->getSelectCount() == 1 ? TheInGameUI->getFirstSelectedDrawable() : NULL;
	for (size_t i = 0; selected != NULL && i < buildings.size(); ++i)
		if (buildings[i] == selected->getObject())
			current = (Int)i;
	Object *next = buildings[ stepFrom( current, (Int)buildings.size(), step ) ];

	// as a click on it selects it: a new group of one
	TheInGameUI->deselectAllDrawables();
	GameMessage *group = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
	group->appendBooleanArgument( TRUE );
	group->appendObjectIDArgument( next->getID() );
	TheInGameUI->selectDrawable( next->getDrawable() );
	if (TheTacticalView != NULL)
		TheTacticalView->lookAt( next->getPosition() );
	DEBUG_LOG(( "GAMEPAD STRUCTURES: selected %s, id %d, %d of %d\n", next->getTemplate()->getName().str(), (Int)next->getID(),
		stepFrom( current, (Int)buildings.size(), step ) + 1, (Int)buildings.size() ));
	return TRUE;
}
