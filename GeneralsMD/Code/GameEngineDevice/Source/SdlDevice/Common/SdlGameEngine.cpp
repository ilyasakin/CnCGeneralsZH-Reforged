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
#include "SdlDevice/Common/SdlDisplays.h"
#include "SdlDevice/Common/SdlGameEngine.h"
#include "SdlDevice/Common/SdlMessageBox.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "PosixDevice/Common/PosixFileResolutionDump.h"
#include "MilesAudioDevice/MilesAudioManager.h"
#include "Common/GlobalData.h"		// -nodevice picks the radar, as on Windows
#include "Common/WindowMode.h"
#include "Win32Device/Common/HeadlessRadar.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DWindowHooks.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// WinMain's DEFAULT_XRESOLUTION and DEFAULT_YRESOLUTION: the window's size until W3DDisplay sizes it to
// the game's resolution when the device is made, as dx8wrapper does on Windows (W3DWindowHooks.h).
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

// WinMain.cpp's, defined in PosixMain.cpp: the window W3DDevice draws into, and whether it is borderless.
extern RenderWindow ApplicationHWnd;
extern Bool ApplicationIsBorderless;

/** W3DDisplay's applyWindowFrame, for SDL's window: WinMain's rules, a frame and a caption for a plain
	* window and none for the two that own the screen. */
static void dressWindow( Int mode )
{
	SDL_Window *window = s_titledWindow;
	if (window == NULL)
		return;
	if (mode == WINDOW_MODE_FULLSCREEN)
	{
		SDL_SetWindowFullscreen( window, true );
		return;
	}
	SDL_SetWindowFullscreen( window, false );
	SDL_SetWindowBordered( window, mode == WINDOW_MODE_WINDOWED );
}

/** W3DDisplay's sizeWindowToClient, for SDL's window: a client area of the resolution, a plain window
	* in the middle of the chosen monitor and a borderless one at its corner. */
static void sizeWindow( Int mode, Int width, Int height, const MonitorRect &screen )
{
	SDL_Window *window = s_titledWindow;
	if (window == NULL)
		return;
	SDL_SetWindowSize( window, width, height );
	int x = (int)screen.left, y = (int)screen.top;
	if (mode == WINDOW_MODE_WINDOWED)
	{
		x += ((int)(screen.right - screen.left) - width) / 2;
		y += ((int)(screen.bottom - screen.top) - height) / 2;
	}
	SDL_SetWindowPosition( window, x, y );

	int pixelWidth = 0, pixelHeight = 0;
	SDL_GetWindowSizeInPixels( window, &pixelWidth, &pixelHeight );
	DEBUG_LOG(( "SdlGameEngine: mode %d at %dx%d; the window is %dx%d pixels\n", mode, width, height,
		pixelWidth, pixelHeight ));
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

	// P1 step 3: "-dumpFileResolution <file>" writes where every path resolves, then ends the run
	// (test_packaging_resolution compares two layouts' dumps).  A test switch, never a player's.
	for (int i = 1; i + 1 < argc; ++i)
		if (strcasecmp( argv[i], "-dumpFileResolution" ) == 0)
		{
			const Bool written = PosixDumpFileResolution( argv[i + 1] );
			fprintf( stderr, "generals: file resolution %s %s\n", written ? "written to" : "NOT written to", argv[i + 1] );
			fflush( NULL );
			_exit( written ? 0 : 1 );
		}
}

void SdlGameEngine::createWindow( void )
{
	if (m_request.headless)
		return;		// no SDL video at all: a headless run must work with no display

	if (m_request.offscreen)
	{
		startOffscreen();
		return;
	}

	// Fullscreen as the game has it on Windows: the display is the game's.  macOS would otherwise put the
	// window in a fullscreen Space, whose menu bar and Dock slide in when the pointer reaches the top or
	// bottom edge - where the game scrolls the view.  SDL reads this once, when its video starts.
	if (!m_request.windowed)
		SDL_SetHint( SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0" );

	if (!SDL_Init( SDL_INIT_VIDEO ))
	{
		char why[ 512 ];
		snprintf( why, sizeof( why ), "SDL could not start its video subsystem: %s", SDL_GetError() );
		RELEASE_CRASH( why );
		return;
	}
	m_sdlVideoStarted = TRUE;
	ThePlatformDisplays = &TheSdlDisplays;		// Monitors.h answers from SDL's displays from here on

	SDL_WindowFlags flags = 0;
	if (!m_request.windowed)
		flags |= SDL_WINDOW_FULLSCREEN;
	if (m_request.hidden)
		flags |= SDL_WINDOW_HIDDEN;		// -hiddenwindow: drawn, never shown (PosixMain.cpp)
	int width = INITIAL_WINDOW_WIDTH;
	int height = INITIAL_WINDOW_HEIGHT;
	SDL_Rect bounds;
	if (m_request.borderless && SDL_GetDisplayBounds( SDL_GetPrimaryDisplay(), &bounds ))
	{
		// Borderless is a frameless window covering the display, as WinMain makes it.
		flags |= SDL_WINDOW_BORDERLESS;
		width = bounds.w;
		height = bounds.h;
	}

	m_window = SDL_CreateWindow( "Command and Conquer Generals Zero Hour", width, height, flags );
	if (m_window == NULL)
	{
		char why[ 512 ];
		snprintf( why, sizeof( why ), "SDL could not create the game's window: %s", SDL_GetError() );
		RELEASE_CRASH( why );
		return;
	}
	DEBUG_LOG(( "SdlGameEngine: window %dx%d, %s%s%s\n", width, height,
		m_request.windowed ? "windowed" : "fullscreen", m_request.borderless ? ", borderless" : "",
		m_request.hidden ? ", hidden" : "" ));

	s_titledWindow = m_window;
	TheApplicationWindowTitleHook = setTitleOfWindow;
	setSdlMessageBoxOwner( m_window );

	// WinMain's ApplicationHWnd, for W3DDevice: the device's window, and the calls that dress and size it
	ApplicationHWnd = (RenderWindow)m_window;
	ApplicationIsBorderless = m_request.borderless;
	TheW3DWindowFrameHook = dressWindow;
	TheW3DWindowSizeHook = sizeWindow;
}

/* -offscreen (PosixMain.cpp): SDL's video runs, because SDL3 makes no GPU device without it, but no window
	 is made, and the device draws every frame into its own target.  The display's own video driver first;
	 where it has no display to add (no window server: a worker over ssh, CI), SDL's dummy driver, with
	 ZH_SDL_GPU_METAL_WINDOWLESS for the Metal backend, which otherwise wants a view the dummy driver cannot
	 make (Libraries/Source/sdl3-metal-windowless.patch).  The hint ZH_OFFSCREEN_FRAMES tells the device (a
	 hint, not ZH_OFFSCREEN itself: the device must not act on the variable when -headless starts no video).
	 Monitors.h keeps its no-display answers, as
	 -headless has them, so a run sizes itself from -xres/-yres and Options.ini alone, whatever the host. */
void SdlGameEngine::startOffscreen( void )
{
	SDL_SetHint( "ZH_OFFSCREEN_FRAMES", "1" );
	SDL_SetHint( "ZH_SDL_GPU_METAL_WINDOWLESS", "1" );
	const char *driver = "the display's";
	if (!SDL_Init( SDL_INIT_VIDEO ))
	{
		const AsciiString first = SDL_GetError();
		SDL_SetHint( SDL_HINT_VIDEO_DRIVER, "dummy" );
		driver = "dummy";
		if (!SDL_Init( SDL_INIT_VIDEO ))
		{
			char why[ 512 ];
			snprintf( why, sizeof( why ), "SDL could not start its video subsystem for -offscreen: %s (then, with the "
				"dummy driver: %s)", first.str(), SDL_GetError() );
			RELEASE_CRASH( why );
			return;
		}
	}
	m_sdlVideoStarted = TRUE;
	DEBUG_LOG(( "SdlGameEngine: offscreen, no window; SDL video driver %s (%s)\n", SDL_GetCurrentVideoDriver(), driver ));
}

void SdlGameEngine::destroyWindow( void )
{
	if (m_window != NULL)
	{
		if (s_titledWindow == m_window)
		{
			TheApplicationWindowTitleHook = NULL;
			TheW3DWindowFrameHook = NULL;
			TheW3DWindowSizeHook = NULL;
			s_titledWindow = NULL;
		}
		if (ApplicationHWnd == (RenderWindow)m_window)
			ApplicationHWnd = NULL;
		setSdlMessageBoxOwner( NULL );
		SDL_DestroyWindow( m_window );
		m_window = NULL;
	}
	if (m_sdlVideoStarted)
	{
		ThePlatformDisplays = NULL;
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		m_sdlVideoStarted = FALSE;
	}
}

/* WinMain's window procedure, for the messages that are not input (input is C3's):
	 - closing the window (WM_CLOSE) asks the game to quit the way its menus do, with
		 MSG_META_DEMO_INSTANT_QUIT, or, while it is still loading and nothing can carry a message, tells
		 the engine to stop;
	 - the application's focus (WM_ACTIVATEAPP) is the engine's isActive.
	 Everything else goes to SdlInput_dispatch, which is WndProc's input half.
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
				SdlInput_dispatch( event );		// keys, text and the mouse (C3): SdlInput.h
				break;
		}
	}
}

// Win32GameEngine's factories, the same W3D classes (decision 8); the radar too: W3DRadar, and
// HeadlessRadar only under -nodevice, where there is no device to hold W3DRadar's textures.  -headless
// makes the device with no window (decision 8, refined), so it keeps W3DRadar, as on Windows.
GameLogic *SdlGameEngine::createGameLogic( void ) { return NEW W3DGameLogic; }
GameClient *SdlGameEngine::createGameClient( void ) { return NEW W3DGameClient; }
ModuleFactory *SdlGameEngine::createModuleFactory( void ) { return NEW W3DModuleFactory; }
ThingFactory *SdlGameEngine::createThingFactory( void ) { return NEW W3DThingFactory; }
FunctionLexicon *SdlGameEngine::createFunctionLexicon( void ) { return NEW W3DFunctionLexicon; }
ParticleSystemManager *SdlGameEngine::createParticleSystemManager( void ) { return NEW W3DParticleSystemManager; }

Radar *SdlGameEngine::createRadar( void )
{
	if( TheGlobalData && TheGlobalData->m_noRenderDevice )
		return NEW HeadlessRadar;
	return NEW W3DRadar;
}

/* The rule: no sound and no windows in automated runs.  A hidden window (-hiddenwindow, or
	 ZH_HIDDEN_WINDOW) or no window (-offscreen, ZH_OFFSCREEN) is a harness or automated run, so it is
	 silent exactly as -noaudio makes it, and
	 the audio device is never opened.  ZH_ALLOW_AUDIO=1 keeps the sound for a deliberate audio check.
	 This is the place: after the command line is parsed, before TheAudio opens its device. */
AudioManager *SdlGameEngine::createAudioManager( void )
{
	const char *allow = getenv( "ZH_ALLOW_AUDIO" );
	const Bool allowed = allow != NULL && allow[0] != '\0' && strcmp( allow, "0" ) != 0;
	if ((m_request.hidden || m_request.offscreen) && !allowed && TheWritableGlobalData != NULL)
	{
		TheWritableGlobalData->m_audioOn = FALSE;
		TheWritableGlobalData->m_speechOn = FALSE;
		TheWritableGlobalData->m_soundsOn = FALSE;
		TheWritableGlobalData->m_musicOn = FALSE;
		DEBUG_LOG(( "Audio off: a hidden window or -offscreen is an automated or harness run (ZH_ALLOW_AUDIO=1 keeps it)\n" ));
	}
	return NEW MilesAudioManager;
}
