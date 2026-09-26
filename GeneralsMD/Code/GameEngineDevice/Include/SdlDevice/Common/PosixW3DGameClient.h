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

// FILE: PosixW3DGameClient.h /////////////////////////////////////////////////////////////////////
// Desc:   W3DGameClient off Windows, with the one factory W3DDevice leaves to the platform.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* W3DGameClient is the game client on every platform (decision 8).  Its keyboard and mouse are its own
	 off Windows too (SDL's, C3), but its video player is Bink on FFmpeg, which is Windows-built today, so
	 off Windows W3DGameClient leaves createVideoPlayer to the platform.

	 Until V1 brings the movies, this is the engine's own VideoPlayer: every open and load answers NULL, so
	 each movie is skipped exactly as when its file is missing.  Not NULL itself: GameClient calls
	 TheVideoPlayer's reset and update unguarded, as do the movie windows. */

#pragma once

#ifndef __POSIXW3DGAMECLIENT_H_
#define __POSIXW3DGAMECLIENT_H_

#if defined(_WIN32)
#error PosixW3DGameClient is the POSIX platform's; Windows' W3DGameClient makes the Bink player itself
#endif

#include "GameClient/VideoPlayer.h"
#include "W3DDevice/GameClient/W3DGameClient.h"

class PosixW3DGameClient : public W3DGameClient
{
protected:
	virtual VideoPlayerInterface *createVideoPlayer( void ) { return NEW VideoPlayer; }	///< no movies until V1
};

#endif // __POSIXW3DGAMECLIENT_H_
