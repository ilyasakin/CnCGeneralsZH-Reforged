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

/*
** The one place a path the engine spelled becomes a path the operating system can open (C1, decision
** D1 in docs/mac-port/tasks/C1-mac-game-engine.md).
**
** The engine spells paths the way Windows took them: "Data\INI\GameData.ini", in whatever case the
** code or the data happened to use, and it keeps them that way - INI load order, the INI CRC,
** MapCache.ini's keys and the portable save paths all depend on the spelling, so nothing upstream is
** normalised.  This translates only the string handed to the operating system:
**
**   - '\' and '/' both separate.  A leading '/' or '\' is absolute; anything else is relative to the
**     current directory, which is the install root.  A drive letter or a UNC path is refused.
**   - Each component is tried exactly first, and on a miss matched without regard to ASCII case
**     against the directory's listing.  On a case-insensitive volume the exact try always succeeds.
**   - Two entries differing only in case (possible on a case-sensitive volume, never on Windows): the
**     exact spelling wins; failing that, the first in byte order, logged once (decision D2).
**   - A component that does not exist keeps the engine's spelling when the intent allows creating
**     it, as Windows would have created it.
**
** Directory listings are cached, each against its directory's modification time, which POSIX changes
** whenever an entry is added or removed.  So a lookup that misses costs one stat when nothing has
** changed, which matters: the engine probes the local file system before the archives for every
** asset, and most of those probes miss.
**
** On a volume with coarse timestamps - exFAT, where the Steam installs on this project's machines
** sit, keeps them to two seconds - two changes inside one tick leave the time where it was.  That is
** harmless there and not a reason to change the cache: exFAT is case-insensitive, so the exact try
** always finds an existing file and the cache is never consulted.  The case-sensitive volumes the
** cache exists for (APFS case-sensitive, ext4, overlayfs) keep nanoseconds.
**
** POSIX only.  Windows opens the engine's spelling as it is.
*/

#ifndef POSIXPATH_H
#define POSIXPATH_H

#if defined(_WIN32)
#error posixpath.h is the POSIX side of path resolution; Windows opens the engine's spelling as it is
#endif

#include <string>

enum PosixPathIntent
{
	// Every component must exist.
	POSIX_PATH_EXISTING,
	// Every component but the last must exist; a missing last one keeps the engine's spelling.
	POSIX_PATH_CREATE_LEAF,
	// Any component may be missing; every missing one keeps the engine's spelling.  For walks that
	// create the directories along a path.
	POSIX_PATH_CREATE_PATH
};

// Resolves engine_path for intent into real_path.  False when a component the intent needs does not
// exist, or when the path is one no POSIX system can have (empty, a drive letter, a UNC path).
bool PosixPath_Resolve(const char * engine_path, PosixPathIntent intent, std::string & real_path);

// Drops the cached listing of one directory, by its real path.  The zh_* forwarders call it after
// they create, remove or rename, so the next lookup does not wait for the directory's time to move -
// a file system whose timestamps are coarser than two changes in a row would otherwise hide one.
void PosixPath_Forget_Directory(const char * real_directory);

// Drops every cached listing.  For tests, and for anything that changes many directories at once.
void PosixPath_Forget_All();

#endif
