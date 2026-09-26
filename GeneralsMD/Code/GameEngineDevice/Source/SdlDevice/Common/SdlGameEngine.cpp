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

// SdlGameEngine.cpp: see SdlGameEngine.h.

#include "PreRTS.h"

#include "Common/MessageStream.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameClient/ApplicationWindowTitle.h"
#include "SdlDevice/Common/SdlGameEngine.h"
#include "NullAudioManager.h"

#include <SDL3/SDL.h>

#include <stdio.h>

// WinMain's DEFAULT_XRESOLUTION and DEFAULT_YRESOLUTION: the window's size until the renderer (D4)
// sizes it to the game's resolution, as dx8wrapper does on Windows.
static const int INITIAL_WINDOW_WIDTH = 800;
static const int INITIAL_WINDOW_HEIGHT = 600;

// The window GameText's title hook names.  One window per process, as on Windows (ApplicationHWnd).
static SDL_Window *s_titledWindow = NULL;

static void setTitleOfWindow( const char *utf8Title )
{
	if (s_titledWindow != NULL)
		SDL_SetWindowTitle( s_titledWindow, utf8Title );
}

/** WinMain.cpp's canPostQuitMessage: a GameMessage stamps itself with the local player, so none can be
	* made until the player list exists. */
static Bool canPostQuitMessage( void )
{
	return TheMessageStream != NULL && ThePlayerList != NULL && ThePlayerList->getLocalPlayer() != NULL;
}

/** A factory this platform cannot answer yet.  Stops the game and says what it waits for. */
static void notYetOffWindows( const char *factory, const char *waitsFor )
{
	char why[ 512 ];
	snprintf( why, sizeof( why ), "SdlGameEngine::%s is not implemented off Windows yet: %s", factory, waitsFor );
	DEBUG_LOG(( "%s\n", why ));
	RELEASE_CRASH( why );
}

SdlGameEngine::SdlGameEngine( const WindowRequest &request )
{
	m_request = request;
	m_window = NULL;
	m_sdlVideoStarted = FALSE;
}

SdlGameEngine::~SdlGameEngine()
{
	destroyWindow();
}

// The window exists before the engine starts, as WinMain creates it before GameMain: GameText names it
// during GameEngine::init.
// GameMain calls init( argc, argv ); the argument-less init is empty in GameEngine and nothing calls it.
void SdlGameEngine::init( int argc, char *argv[] )
{
	createWindow();
	GameEngine::init( argc, argv );
}

void SdlGameEngine::createWindow( void )
{
	if (m_request.headless)
		return;		// no SDL video at all: a headless run must work with no display

	if (!SDL_Init( SDL_INIT_VIDEO ))
	{
		char why[ 512 ];
		snprintf( why, sizeof( why ), "SDL could not start its video subsystem: %s", SDL_GetError() );
		RELEASE_CRASH( why );
		return;
	}
	m_sdlVideoStarted = TRUE;

	SDL_WindowFlags flags = 0;
	if (!m_request.windowed)
		flags |= SDL_WINDOW_FULLSCREEN;
	if (m_request.borderless)
		flags |= SDL_WINDOW_BORDERLESS;

	m_window = SDL_CreateWindow( "Command and Conquer Generals Zero Hour", INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, flags );
	if (m_window == NULL)
	{
		char why[ 512 ];
		snprintf( why, sizeof( why ), "SDL could not create the game's window: %s", SDL_GetError() );
		RELEASE_CRASH( why );
		return;
	}
	DEBUG_LOG(( "SdlGameEngine: window %dx%d, %s%s\n", INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT,
		m_request.windowed ? "windowed" : "fullscreen", m_request.borderless ? ", borderless" : "" ));

	s_titledWindow = m_window;
	TheApplicationWindowTitleHook = setTitleOfWindow;
}

void SdlGameEngine::destroyWindow( void )
{
	if (m_window != NULL)
	{
		if (s_titledWindow == m_window)
		{
			TheApplicationWindowTitleHook = NULL;
			s_titledWindow = NULL;
		}
		SDL_DestroyWindow( m_window );
		m_window = NULL;
	}
	if (m_sdlVideoStarted)
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		m_sdlVideoStarted = FALSE;
	}
}

/* WinMain's window procedure, for the messages that are not input (input is C3's):
	 - closing the window (WM_CLOSE) asks the game to quit the way its menus do, with
		 MSG_META_DEMO_INSTANT_QUIT, or, while it is still loading and nothing can carry a message, tells
		 the engine to stop;
	 - the application's focus (WM_ACTIVATEAPP) is the engine's isActive.
	 Headless there is no SDL and nothing to pump. */
void SdlGameEngine::serviceWindowsOS( void )
{
	if (!m_sdlVideoStarted)
		return;

	SDL_Event event;
	while (SDL_PollEvent( &event ))
	{
		switch (event.type)
		{
			case SDL_EVENT_QUIT:
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				if (!getQuitting())
				{
					if (canPostQuitMessage())
						TheMessageStream->appendMessage( GameMessage::MSG_META_DEMO_INSTANT_QUIT );
					else
						setQuitting( TRUE );
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				setIsActive( TRUE );
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				setIsActive( FALSE );
				break;

			default:
				break;
		}
	}
}

GameLogic *SdlGameEngine::createGameLogic( void )
{
	notYetOffWindows( "createGameLogic", "the simulation's terrain has to move out of W3DDevice first (task T1)" );
	return NULL;
}

GameClient *SdlGameEngine::createGameClient( void )
{
	notYetOffWindows( "createGameClient", "W3DGameClient comes with the renderer (D4), or a headless client after T1" );
	return NULL;
}

ModuleFactory *SdlGameEngine::createModuleFactory( void )
{
	notYetOffWindows( "createModuleFactory", "W3DModuleFactory's 19 draw modules come with the renderer (D4); see PosixGameEngine.h" );
	return NULL;
}

ThingFactory *SdlGameEngine::createThingFactory( void )
{
	notYetOffWindows( "createThingFactory", "W3DThingFactory comes with the renderer (D4)" );
	return NULL;
}

FunctionLexicon *SdlGameEngine::createFunctionLexicon( void )
{
	notYetOffWindows( "createFunctionLexicon", "W3DFunctionLexicon's window draw functions come with the renderer (D4)" );
	return NULL;
}

Radar *SdlGameEngine::createRadar( void )
{
	notYetOffWindows( "createRadar", "W3DRadar comes with the renderer (D4); HeadlessRadar is Win32Device's and not built here yet" );
	return NULL;
}

ParticleSystemManager *SdlGameEngine::createParticleSystemManager( void )
{
	notYetOffWindows( "createParticleSystemManager", "W3DParticleSystemManager comes with the renderer (D4)" );
	return NULL;
}

// The silent device, until C4's upper half wires MilesAudioManager over miniaudio.
AudioManager *SdlGameEngine::createAudioManager( void )
{
	return NEW NullAudioManager;
}
