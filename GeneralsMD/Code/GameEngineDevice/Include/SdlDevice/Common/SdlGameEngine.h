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

// SdlGameEngine: the game engine off Windows with SDL3 underneath (C2).  PosixGameEngine (C1 (f)) has
// the file systems, the network and the browser; this adds what WinMain and its window procedure do
// on Windows:
//   - the window, which it owns: none at all under -headless, where SDL's video is never started,
//     so a headless run needs no display (E1's replay runs are on machines without one);
//   - the event pump (serviceWindowsOS), with a close request and the application's focus handled
//     as WM_CLOSE and WM_ACTIVATEAPP are;
//   - the window's title, through GameText's hook (GameClient/ApplicationWindowTitle.h).
// Nothing draws into the window yet: the renderer is D4's.
//
// The factories Windows answers with W3DDevice classes cannot be answered here yet, and each stops the
// game with a message that names what it waits for rather than hand back something that is not the
// game (PosixGameEngine.h says why for the logic and the module factory).  Audio is the exception:
// NullAudioManager, the silent device, until C4's upper half wires the real one.

#pragma once

#ifndef __SDLGAMEENGINE_H
#define __SDLGAMEENGINE_H

#include "PosixDevice/Common/PosixGameEngine.h"

struct SDL_Window;

class SdlGameEngine : public PosixGameEngine
{
public:
	/** How the window starts, as WinMain settles it before the engine exists: -headless, -win,
		* -fullscreen and -borderless over Options.ini's WindowMode. */
	struct WindowRequest
	{
		Bool headless;		///< no window, and SDL's video never started
		Bool windowed;
		Bool borderless;	///< windowed, with no frame
	};

	SdlGameEngine( const WindowRequest &request );
	virtual ~SdlGameEngine();

	using PosixGameEngine::init;
	virtual void init( int argc, char *argv[] );	///< the engine's real start (GameEngine::init( void ) does nothing)
	virtual void serviceWindowsOS( void );

protected:
	virtual GameLogic *createGameLogic( void );
	virtual GameClient *createGameClient( void );
	virtual ModuleFactory *createModuleFactory( void );
	virtual ThingFactory *createThingFactory( void );
	virtual FunctionLexicon *createFunctionLexicon( void );
	virtual Radar *createRadar( void );
	virtual ParticleSystemManager *createParticleSystemManager( void );
	virtual AudioManager *createAudioManager( void );

private:
	void createWindow( void );
	void destroyWindow( void );

	WindowRequest m_request;
	SDL_Window *m_window;
	Bool m_sdlVideoStarted;
};

#endif // __SDLGAMEENGINE_H
