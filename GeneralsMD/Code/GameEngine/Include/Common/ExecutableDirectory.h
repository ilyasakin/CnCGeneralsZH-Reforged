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

// FILE: ExecutableDirectory.h ////////////////////////////////////////////////////////////////////
// Desc:   The directory the running executable is in.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

#include <stddef.h>

/* The directory of the running executable, into buf: the executable's path cut at its last
	 separator.  With keepTrailingSeparator the separator stays ("C:\Game\"), without it the cut is on
	 the separator ("C:\Game").  A path with no separator in it is left whole.  The separator is the
	 platform's own: '\\' on Windows, '/' elsewhere.  Callers that append file names with '\\' still
	 do; those joins are C1's.  Symbolic links are resolved off Windows, so a link to the game names
	 the directory the game is really in.  A path that does not fit in buf gives "" off Windows;
	 GetModuleFileName truncates, as it always has.

	 Safe before main: nothing from the engine's allocator, no engine state.  MemoryInit calls it
	 before the memory manager exists, and Debug.cpp from a static constructor. */
void getExecutableDirectory( char *buf, size_t size, Bool keepTrailingSeparator );

#if !defined(_WIN32)
/* Where the debug logs go (P1 step 2): the executable's directory, as on Windows, except when the
	 executable is inside a macOS app bundle (".app/Contents/MacOS"): writing there would break the
	 bundle's code signature, so the logs go to the user data directory's Logs folder instead.  "" when
	 even that cannot be had, which leaves a log unwritten rather than written into the bundle.  The
	 same trailing-separator rule as getExecutableDirectory.  Safe before main. */
void getLogDirectory( char *buf, size_t size, Bool keepTrailingSeparator );
#endif
