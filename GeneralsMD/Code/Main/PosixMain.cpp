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

// FILE: PosixMain.cpp ////////////////////////////////////////////////////////////////////////////
// Desc:   The game's entry point off Windows (C2): WinMain's start-up sequence without the Windows.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* One main for macOS and Linux, on SDL3 (decision 3).  It follows WinMain.cpp step for step, in its
	 order, and leaves out what only Windows has; each left-out step says where it goes:

	 - _set_FMA3_enable(0): the x64 C runtime's choice between two libm paths.  Whether this platform's
		 libm answers the logic's log() the same way is E1's question, not the entry point's.
	 - SetProcessDPIAware: the window's pixel density is the renderer's (D4).
	 - _set_se_translator / SetUnhandledExceptionFilter: their counterpart is C5's crash handler,
		 installCrashHandlers, first thing in main.
	 - "-DX" stack dumps: Windows symbol lookup (GetFunctionDetails).
	 - _CrtSetDbgFlag, the splash bitmap, OLE, copy protection and the Optimus exports: Windows only.
	 - The Windows window class and WndProc: the window is SdlGameEngine's, which the engine creates
		 before GameEngine::init, as WinMain creates it before GameMain.

	 The install root is the process's working directory, as on Windows, where WinMain sets it to the
	 executable's directory.  Here that is the default too, and "-root <dir>" overrides it.  It is set
	 once, before anything asks for a path, and nothing changes it after (C1's Roots paragraph).  Plan
	 rule 9 applies to how this is RUN: GameEngine::init deletes Data\INI\INIZH.big from the root, as it
	 does in a player's install, so a development run must be rooted at a copy of the game data. */

#include <SDL3/SDL_main.h>	// SDL3's main: on macOS and Linux an ordinary main

#include "PreRTS.h"

#include "Lib/BaseType.h"
#include "Common/CrashHandler.h"
#include "Common/CriticalSection.h"
#include "Common/Debug.h"
#include "Common/EarlyCommandLine.h"
#include "Common/EarlyOptions.h"
#include "Common/Errors.h"
#include "Common/ExecutableDirectory.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/version.h"
#include "Common/WindowMode.h"
#include "SdlDevice/Common/SdlGameEngine.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <unistd.h>

// GLOBALS ////////////////////////////////////////////////////////////////////
// gameengine names these three; WinMain.cpp defines them on Windows, with these values.
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char s_noAppPrefix[] = "";
char *gAppPrefix = s_noAppPrefix; /// So WB can have a different debug log file name.

static CriticalSection critSec2, critSec3, critSec4, critSec5;

// WinMain's pre-parse: how the window starts, settled before the engine exists.
static SdlGameEngine::WindowRequest s_windowRequest = { FALSE, FALSE, FALSE };

// WinMain's GENERALS_GUID, the name of its one-copy mutex; here the name of a lock file.
#define GENERALS_GUID "685EAFF2-3216-4265-B047-251C5F4B82F3"

/** The install root: "-root <dir>" if given, else the executable's directory.  Read from argv itself
	* rather than EarlyCommandLine.h, whose values end at a space, because a path may have one. */
static Bool chooseInstallRoot( int argc, char *argv[], char *out, size_t outSize )
{
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (strcasecmp( argv[i], "-root" ) == 0)
		{
			if (strlen( argv[i + 1] ) + 1 > outSize)
				return FALSE;
			strcpy( out, argv[i + 1] );
			return TRUE;
		}
	}
	getExecutableDirectory( out, outSize, FALSE );
	return out[0] != 0;
}

/** WinMain's one-copy guard: a named mutex there, an exclusive lock on a file in the user data directory
	* here, held for the process's life.  FALSE if another copy holds it. */
static Bool takeOneCopyLock( void )
{
	char path[ 4096 ];
	if (!findUserDataDirectory( path, sizeof( path ) ))
		return TRUE;	// nowhere to keep the lock: do not stop the game over it
	strncat( path, "Generals-" GENERALS_GUID ".lock", sizeof( path ) - strlen( path ) - 1 );
	const int fd = zh_open( path, O_CREAT | O_RDWR, 0644 );
	if (fd < 0)
		return TRUE;
	if (flock( fd, LOCK_EX | LOCK_NB ) != 0)
	{
		const Bool heldElsewhere = (errno == EWOULDBLOCK);
		close( fd );
		return heldElsewhere ? FALSE : TRUE;
	}
	return TRUE;	// the descriptor stays open, and the lock with it, until the process ends
}

// main =======================================================================
/** Application entry point */
//=============================================================================
int main( int argc, char *argv[] )
{
	// Before anything else, and before another thread exists: a crash from here on leaves
	// ReleaseCrashInfo.txt, as WinMain's _set_se_translator and SetUnhandledExceptionFilter make it on Windows.
	installCrashHandlers();

	// The one locale category the game may set (plan rule; C2's task file): dates in the replay and save
	// lists in the user's format.  LC_NUMERIC would change how the INI parser reads decimals.
	setlocale( LC_TIME, "" );

	try {

		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		char root[ 4096 ];
		if (!chooseInstallRoot( argc, argv, root, sizeof( root ) ) || chdir( root ) != 0)
		{
			fprintf( stderr, "generals: cannot use '%s' as the install root: %s\n", root, strerror( errno ) );
			return 1;
		}

		// The window mode, as WinMain settles it: Options.ini's saved mode, then the command line over it.
		{
			const int savedMode = getEarlyOptionInt( "WindowMode", WINDOW_MODE_FULLSCREEN, 0, WINDOW_MODE_COUNT - 1 );
			s_windowRequest.borderless = (savedMode == WINDOW_MODE_BORDERLESS);
			s_windowRequest.windowed = (savedMode != WINDOW_MODE_FULLSCREEN);
		}
		for (int i = 1; i < argc; ++i)
		{
			if (strcasecmp( argv[i], "-win" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.borderless = FALSE;	// an explicit -win beats a borderless Options.ini
			}
			if (strcasecmp( argv[i], "-fullscreen" ) == 0)
			{
				s_windowRequest.windowed = FALSE;
				s_windowRequest.borderless = FALSE;
			}
			if (strcasecmp( argv[i], "-borderless" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.borderless = TRUE;
			}
			if (strcasecmp( argv[i], "-headless" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.headless = TRUE;
			}
		}

		// start the log
		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);
		initMemoryManager();

		// Set up version info
		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

		/* -multiInstance lets a second copy start, as on Windows: one copy at a time is right for a
			 player and wrong for a test, where a network game needs two processes on one machine. */
		const Bool oneCopyIsEnough = (findEarlyCommandLineOption( L"-multiInstance" ) == NULL);
		if (oneCopyIsEnough && !takeOneCopyLock())
		{
			DEBUG_LOG(("Generals is already running...Bail!\n"));
			delete TheVersion;
			TheVersion = NULL;
			shutdownMemoryManager();
			DEBUG_SHUTDOWN();
			return 0;
		}
		DEBUG_LOG(("Create GeneralsMutex okay.\n"));

		DEBUG_LOG(("CRC message is %d\n", GameMessage::MSG_LOGIC_CRC));

		// run the game main loop
		GameMain(argc, argv);

		delete TheVersion;
		TheVersion = NULL;

	#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
	#endif
	#if defined(_DEBUG) || defined(_INTERNAL)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
	#endif

		// close the log
		shutdownMemoryManager();
		DEBUG_SHUTDOWN();
	}
	// As WinMain: name what escaped the engine, and leave through ReleaseCrash so no destructor runs.
	catch (INIException e)
	{
		RELEASE_CRASH((e.mFailureMessage ? e.mFailureMessage : "Uncaught INI exception in main"));
	}
	catch (ErrorCode ec)
	{
		char why[ 64 ];
		snprintf( why, sizeof(why), "Uncaught ErrorCode 0x%08x in main", (UnsignedInt)ec );
		RELEASE_CRASH((why));
	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in main"));
	}

	TheUnicodeStringCriticalSection = NULL;
	TheDmaCriticalSection = NULL;
	TheMemoryPoolCriticalSection = NULL;

	return 0;

}  // end main

// CreateGameEngine ===========================================================
/** Create the game engine we're going to use: SDL3's, over C1's POSIX one */
//=============================================================================
GameEngine *CreateGameEngine( void )
{
	return NEW SdlGameEngine( s_windowRequest );
}
