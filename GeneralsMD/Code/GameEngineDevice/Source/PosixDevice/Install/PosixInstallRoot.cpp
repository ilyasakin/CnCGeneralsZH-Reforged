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

// PosixInstallRoot.cpp: see PosixInstallRoot.h.  No engine state and no SDL: its own small library
// (installroot), so test_install_root links it alone.

#include "PosixDevice/Common/PosixInstallRoot.h"

#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Common/EarlyOptions.h"	// findEarlyOptionValueIn: Registry.ini's reader
#include "zhio.h"

namespace {

// The archive each game's folder is known by (Win32BIGFileSystem's BASE_GAME_ARCHIVE is Textures.big).
const char ZERO_HOUR_ARCHIVE[] = "INIZH.big";
const char BASE_GAME_ARCHIVE[] = "Textures.big";

// Win32BIGFileSystem::init's places for the base game, relative to the Zero Hour folder.
const char *const BASE_GAME_FOLDERS[] = { "", "ZH_Generals", "../Command & Conquer Generals",
	"../Command & Conquer(tm) Generals" };
const char FIRST_DECADE_GENERALS_FOLDER[] = "Command & Conquer(tm) Generals";

// The Zero Hour folder's names on the installs players have (retail, First Decade, Steam, EA App).
const char *const ZERO_HOUR_FOLDER_NAMES[] = {
	"Command & Conquer Generals Zero Hour",
	"Command and Conquer Generals Zero Hour",
	"Command & Conquer Generals - Zero Hour",
	"Command & Conquer(tm) Generals Zero Hour",
	"Zero Hour" };

bool isFolder( const std::string &path )
{
	struct stat status;
	return !path.empty() && stat( path.c_str(), &status ) == 0 && S_ISDIR( status.st_mode );
}

std::string join( const std::string &folder, const std::string &name )
{
	if (name.empty())
		return folder;
	return folder.empty() || folder[folder.size() - 1] == '/' ? folder + name : folder + "/" + name;
}

// A file directly in `folder`, in any case: zh_access resolves each component without regard to case
bool holds( const std::string &folder, const char *name )
{
	return zh_access( join( folder, name ).c_str(), F_OK ) == 0;
}

std::string realOf( const std::string &path )
{
	char real[ PATH_MAX ];
	return realpath( path.c_str(), real ) != NULL ? std::string( real ) : std::string();
}

bool isInside( const std::string &path, const std::string &folder )
{
	return !folder.empty() && (path == folder || (path.size() > folder.size() && path.compare( 0, folder.size(), folder ) == 0
		&& path[folder.size()] == '/'));
}

bool readRegistryValue( const std::string &file, const char *key, std::string &value )
{
	if (file.empty())
		return false;
	FILE *fp = zh_fopen( file.c_str(), "r" );
	if (fp == NULL)
		return false;
	char buffer[ 1024 ];
	const bool found = findEarlyOptionValueIn( fp, key, buffer, sizeof( buffer ) );
	fclose( fp );
	if (found)
		value = buffer;
	return found && !value.empty();
}

bool holdsBaseGame( const std::string &zeroHour, const std::string &registryFile )
{
	for (size_t i = 0; i < sizeof( BASE_GAME_FOLDERS ) / sizeof( BASE_GAME_FOLDERS[0] ); ++i)
		if (holds( join( zeroHour, BASE_GAME_FOLDERS[i] ), BASE_GAME_ARCHIVE ))
			return true;
	// Registry.ini's Generals InstallPath, as GetStringFromGeneralsRegistry("", "InstallPath") reads it
	std::string generals;
	if (readRegistryValue( registryFile, "Generals\\InstallPath", generals ))
		return holds( generals, BASE_GAME_ARCHIVE ) || holds( join( generals, FIRST_DECADE_GENERALS_FOLDER ), BASE_GAME_ARCHIVE );
	return false;
}

std::string argumentValue( const std::vector<std::string> &arguments, const char *name )
{
	for (size_t i = 0; i + 1 < arguments.size(); ++i)
		if (strcasecmp( arguments[i].c_str(), name ) == 0)
			return arguments[i + 1];
	return std::string();
}

}  // namespace

PosixInstallCheck PosixCheckInstallFolder( const std::string &folder, const std::vector<std::string> &forbidden )
{
	return PosixCheckInstallFolderWith( folder, forbidden, std::string() );
}

PosixInstallCheck PosixCheckInstallFolderWith( const std::string &folder, const std::vector<std::string> &forbidden,
	const std::string &registryFile )
{
	const std::string real = realOf( folder );
	if (real.empty() || !isFolder( real ))
		return INSTALL_NOT_A_FOLDER;
	for (size_t i = 0; i < forbidden.size(); ++i)
	{
		const std::string inside = realOf( forbidden[i] );
		if (isInside( real, inside ))
			return INSTALL_INSIDE_THE_APP;
	}
	if (!holds( real, ZERO_HOUR_ARCHIVE ))
		return INSTALL_NO_ZERO_HOUR;
	if (!holdsBaseGame( real, registryFile ))
		return INSTALL_NO_BASE_GAME;
	return INSTALL_OK;
}

std::string PosixInstallCheckMessage( PosixInstallCheck check, const std::string &folder )
{
	switch (check)
	{
		case INSTALL_OK:
			return std::string();
		case INSTALL_NOT_A_FOLDER:
			return "\"" + folder + "\" is not a folder.";
		case INSTALL_NO_ZERO_HOUR:
			return "\"" + folder + "\" is not a Command & Conquer Generals Zero Hour folder: it has no INIZH.big.\n\n"
				"Choose the folder Zero Hour is installed in, the one with INIZH.big in it.";
		case INSTALL_NO_BASE_GAME:
			return "\"" + folder + "\" holds Zero Hour, but not the original Command & Conquer Generals it needs: "
				"its Textures.big is not there, in a ZH_Generals folder inside it, or in a \"Command & Conquer "
				"Generals\" folder beside it.\n\n"
				"Copy Generals' .big files into a folder named ZH_Generals inside the Zero Hour folder, then choose "
				"the Zero Hour folder again.";
		case INSTALL_INSIDE_THE_APP:
			return "\"" + folder + "\" is inside the game's own app. Choose the folder your copy of Zero Hour is "
				"installed in.";
	}
	return std::string();
}

std::vector<std::string> PosixKnownInstallPlaces( const std::string &home )
{
	std::vector<std::string> places;
	const size_t names = sizeof( ZERO_HOUR_FOLDER_NAMES ) / sizeof( ZERO_HOUR_FOLDER_NAMES[0] );
	for (size_t i = 0; i < names; ++i)
		places.push_back( join( join( home, "Games" ), ZERO_HOUR_FOLDER_NAMES[i] ) );
	for (size_t i = 0; i < names; ++i)
		places.push_back( join( "/Applications", ZERO_HOUR_FOLDER_NAMES[i] ) );

	// Wine prefixes the players on Macs use: CrossOver's and Whisky's bottles, each a Windows drive
	static const char *const bottles[] = {
		"Library/Application Support/CrossOver/Bottles/*/drive_c",
		"Library/Containers/com.isaacmarovitz.Whisky/Bottles/*/drive_c" };
	static const char *const inside[] = {
		"Program Files*/EA Games",
		"Program Files*/Origin Games",
		"Program Files*/Steam/steamapps/common",
		"Program Files*/Command & Conquer The First Decade",
		"Program Files*" };
	for (size_t b = 0; b < sizeof( bottles ) / sizeof( bottles[0] ); ++b)
		for (size_t f = 0; f < sizeof( inside ) / sizeof( inside[0] ); ++f)
			for (size_t n = 0; n < names; ++n)
			{
				const std::string pattern = join( join( join( home, bottles[b] ), inside[f] ), ZERO_HOUR_FOLDER_NAMES[n] );
				glob_t found;
				if (glob( pattern.c_str(), 0, NULL, &found ) == 0)
					for (size_t k = 0; k < found.gl_pathc; ++k)
						places.push_back( found.gl_pathv[k] );
				globfree( &found );
			}
	return places;
}

bool PosixChooseInstallRoot( const PosixInstallRequest &request, PosixInstallChoice &choice )
{
	choice = PosixInstallChoice();
	choice.writeInstallPath = false;

	// 1. -root: as given, as harnesses and development runs have always used it
	const std::string argument = argumentValue( request.arguments, "-root" );
	if (!argument.empty())
	{
		choice.root = argument;
		choice.source = ROOT_FROM_ARGUMENT;
		return true;
	}

	// 2. Registry.ini's InstallPath, while it still holds the game
	std::string registered;
	if (readRegistryValue( request.registryFile, "InstallPath", registered )
		&& PosixCheckInstallFolderWith( registered, request.forbidden, request.registryFile ) == INSTALL_OK)
	{
		choice.root = realOf( registered );
		choice.source = ROOT_FROM_REGISTRY;
		return true;
	}

	if (!request.insideAppBundle)
	{
		// 5. an unpacked build: the executable's directory, as always
		choice.root = request.executableDirectory;
		choice.source = ROOT_FROM_EXECUTABLE;
		return !choice.root.empty();
	}

	// 3. the known places, silently: nothing is written for a folder the player did not choose
	const std::vector<std::string> places = PosixKnownInstallPlaces( request.home );
	for (size_t i = 0; i < places.size(); ++i)
		if (PosixCheckInstallFolderWith( places[i], request.forbidden, request.registryFile ) == INSTALL_OK)
		{
			choice.root = realOf( places[i] );
			choice.source = ROOT_FROM_KNOWN_PLACE;
			return true;
		}

	// 4. the player's choice, asked again with the reason until it validates or they cancel
	if (request.chooser == NULL)
	{
		choice.problem = "The Zero Hour folder is not known, and there is no way to ask for it here: start the game with "
			"-root <folder>.";
		return false;
	}
	std::string why;
	for (;;)
	{
		std::string chosen;
		if (!request.chooser( why, chosen, request.chooserContext ))
		{
			choice.problem = "No Zero Hour folder was chosen.";
			return false;
		}
		const PosixInstallCheck check = PosixCheckInstallFolderWith( chosen, request.forbidden, request.registryFile );
		if (check == INSTALL_OK)
		{
			choice.root = realOf( chosen );
			choice.source = ROOT_FROM_CHOOSER;
			choice.writeInstallPath = true;
			return true;
		}
		why = PosixInstallCheckMessage( check, chosen );
	}
}
