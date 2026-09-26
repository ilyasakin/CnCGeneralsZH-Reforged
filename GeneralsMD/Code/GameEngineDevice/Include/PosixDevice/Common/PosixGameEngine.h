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

// PosixGameEngine: the platform half of the game engine off Windows (C1 (f)), shared by macOS and
// Linux.  Win32GameEngine's counterpart for what is the platform's and not the renderer's:
//   - the file systems: PosixLocalFileSystem, and the BIG archive reader Windows uses (built here
//     from Win32Device's own source, so both platforms mount archives in the same order);
//   - the network, as NetworkInterface::createNetwork() on Windows;
//   - no embedded browser: nothing creates one on Windows either (WebBrowserPosix.cpp).
//
// Deliberately abstract.  The factories Windows answers with W3DDevice classes - game logic, game
// client, module factory, thing factory, function lexicon, particle system manager, radar - and the
// audio manager stay pure virtual here, for the subclass that has a renderer (C2's, over SDL3) to
// give.  None of them can be answered honestly yet:
//   - createGameLogic: W3DGameLogic's terrain logic takes the simulation's ground height from the
//     renderer's height map, so the base GameLogic would play on different ground.  Task T1 moves
//     that into gameengine; until it lands no subclass should return a GameLogic from here and
//     claim a simulation result from it.
//   - createModuleFactory: W3DModuleFactory registers 19 draw modules by name, in an order that fixes
//     their NameKeys, and every object INI names one; a headless factory needs all 19, with parsers
//     that accept each one's INI fields.
// serviceWindowsOS is empty: the event pump is C2's, in the subclass.  CreateGameEngine is C2's too.

#pragma once

#ifndef __POSIXGAMEENGINE_H
#define __POSIXGAMEENGINE_H

#include "Common/GameEngine.h"

class PosixGameEngine : public GameEngine
{
public:
	PosixGameEngine();
	virtual ~PosixGameEngine();

	virtual void reset( void );
	virtual void update( void );
	virtual void serviceWindowsOS( void );		///< nothing here; the subclass with a window pumps its events

protected:
	virtual LocalFileSystem *createLocalFileSystem( void );
	virtual ArchiveFileSystem *createArchiveFileSystem( void );
	virtual NetworkInterface *createNetwork( void );
	virtual WebBrowser *createWebBrowser( void );
};

#endif // __POSIXGAMEENGINE_H
