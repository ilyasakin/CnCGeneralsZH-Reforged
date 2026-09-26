/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// FILE: MapObjectRenderPosix.cpp /////////////////////////////////////////////////////////////////
// Desc:   MapObject's render half off Windows, until there is a renderer to hold references for.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* On Windows these three live beside the W3D terrain code in WorldHeightMap.cpp, and hold their
	 RenderObjClass with REF_PTR_SET.  Off Windows nothing creates a RenderObjClass yet - that is the D
	 track's - so the only value that can arrive is NULL, which the constructor and destructor pass to
	 clear the slots.  These keep the pointer and hold no reference; a non-null one would mean a
	 renderer had arrived without its half of MapObject, so it is reported rather than kept quietly. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MapObject.h"

void MapObject::setRenderObj(RenderObjClass *pObj)
{
	DEBUG_ASSERTCRASH(pObj == NULL, ("MapObject::setRenderObj: no renderer holds references off Windows yet"));
	m_renderObj = pObj;
}

void MapObject::setBridgeRenderObject( BridgeTowerType type, RenderObjClass* renderObj )
{
	DEBUG_ASSERTCRASH(renderObj == NULL, ("MapObject::setBridgeRenderObject: no renderer holds references off Windows yet"));
	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		m_bridgeTowers[ type ] = renderObj;
}

RenderObjClass* MapObject::getBridgeRenderObject( BridgeTowerType type )
{
	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		return m_bridgeTowers[ type ];
	return NULL;
}
