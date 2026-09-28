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
// GamepadAim.cpp: see GamepadAim.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GamepadAim.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include <math.h>
#include <vector>

namespace {

const Real EASE_SECONDS = 0.06f;		///< the ease's time constant: nine tenths of the way in about 0.14 s

Bool inMatch( void )
{
	return TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && TheGameClient != NULL
		&& TheTacticalView != NULL && ThePlayerList != NULL && ThePlayerList->getLocalPlayer() != NULL
		&& (TheGlobalData == NULL || TheGlobalData->m_gamepadAim);
}

/// Selectable, shown, and seen by the local player
Bool aimable( Drawable *draw, const Player *local )
{
	if (draw->isDrawableEffectivelyHidden() || !draw->isSelectable())
		return FALSE;
	const Object *obj = draw->getObject();
	if (obj == NULL)
		return FALSE;
	return obj->getControllingPlayer() == local || obj->getShroudedStatus( local->getPlayerIndex() ) < OBJECTSHROUD_FOGGED;
}

/// The aimable things' points on screen (their positions: a unit's feet, a building's footprint centre)
void aimPoints( std::vector<ICoord2D> &points )
{
	points.clear();
	const Player *local = ThePlayerList->getLocalPlayer();
	for (Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable())
	{
		ICoord2D screen;
		if (aimable( draw, local ) && TheTacticalView->worldToScreen( draw->getPosition(), &screen ))
			points.push_back( screen );
	}
}

/// Over one of the game's windows (the command bar, the radar...), where the pointer aims at buttons, not the world
Bool overWindow( Int x, Int y )
{
	return TheWindowManager != NULL && TheWindowManager->getWindowUnderCursor( x, y ) != NULL;
}

}  // namespace

Int GamepadAim::radiusFor( Int screenHeight )
{
	const Int radius = screenHeight * 3 / 100;		// 24 pixels at 800 lines, 32 at 1080
	return radius < 12 ? 12 : radius;
}

Bool GamepadAim::magnetTarget( Int x, Int y, Int radius, ICoord2D &target )
{
	if (!inMatch() || overWindow( x, y ))
		return FALSE;
	std::vector<ICoord2D> points;
	aimPoints( points );
	const Int best = points.empty() ? -1 : nearest( &points[0], (Int)points.size(), x, y, radius );
	if (best < 0)
		return FALSE;
	target = points[ best ];
	return TRUE;
}

Bool GamepadAim::nearTarget( Int x, Int y, Int radius )
{
	ICoord2D target;
	return magnetTarget( x, y, radius, target );
}

Int GamepadAim::nearest( const ICoord2D *points, Int count, Int x, Int y, Int radius )
{
	Int best = -1;
	Int bestDistance = radius * radius;
	for (Int i = 0; i < count; ++i)
	{
		const Int dx = points[i].x - x, dy = points[i].y - y;
		const Int distance = dx * dx + dy * dy;
		if (distance <= bestDistance && (best < 0 || distance < bestDistance))
		{
			best = i;
			bestDistance = distance;
		}
	}
	return best;
}

void GamepadAim::easeToward( Real &x, Real &y, const ICoord2D &target, Real seconds )
{
	const Real dx = target.x - x, dy = target.y - y;
	if (dx * dx + dy * dy <= 1.0f || seconds <= 0.0f)
	{
		if (seconds > 0.0f)
		{
			x = (Real)target.x;
			y = (Real)target.y;
		}
		return;
	}
	const Real keep = expf( -seconds / EASE_SECONDS );
	x = target.x - dx * keep;
	y = target.y - dy * keep;
}
