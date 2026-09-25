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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Registry.cpp
// Simple interface for storing/retreiving registry values
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Registry.h"
#if !defined(_WIN32)
#include "Common/EarlyOptions.h"	// findUserDataDirectory, findEarlyOptionValueIn
#include <stdio.h>
#include <stdlib.h>
#endif

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#if defined(_WIN32)
/*
	A 64-bit process reads HKLM\SOFTWARE natively, and the game's installers are 32-bit: what they
	wrote is under HKLM\SOFTWARE\WOW6432Node, which the native view does not show.  So every read
	asks for the native view first and the 32-bit one after it, which is where an install path
	written by Steam's copy of the game actually is.  On Win32 the second open is the same key as
	the first and costs nothing.
*/
static int openForRead(HKEY root, const char *path, HKEY *handle)
{
	int returnValue = RegOpenKeyEx( root, path, 0, KEY_READ, handle );
	if (returnValue != ERROR_SUCCESS)
	{
		returnValue = RegOpenKeyEx( root, path, 0, KEY_READ | KEY_WOW64_32KEY, handle );
	}
	return returnValue;
}

Bool  getStringFromRegistry(HKEY root, AsciiString path, AsciiString key, AsciiString& val)
{
	HKEY handle;
	unsigned char buffer[256];
	unsigned long size = 256;
	unsigned long type;
	int returnValue;

	if ((returnValue = openForRead( root, path.str(), &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), NULL, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = (char *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool getUnsignedIntFromRegistry(HKEY root, AsciiString path, AsciiString key, UnsignedInt& val)
{
	HKEY handle;
	unsigned char buffer[4];
	unsigned long size = 4;
	unsigned long type;
	int returnValue;

	if ((returnValue = openForRead( root, path.str(), &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), NULL, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = *(UnsignedInt *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool setStringInRegistry( HKEY root, AsciiString path, AsciiString key, AsciiString val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, "REG_NONE", REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &handle, NULL )) == ERROR_SUCCESS)
	{
		type = REG_SZ;
		size = val.getLength()+1;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)val.str(), size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool setUnsignedIntInRegistry( HKEY root, AsciiString path, AsciiString key, UnsignedInt val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, "REG_NONE", REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &handle, NULL )) == ERROR_SUCCESS)
	{
		type = REG_DWORD;
		size = 4;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)&val, size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";

	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s\n", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";

	fullPath.concat(path);
	// HKCU first, HKLM second. The machine-wide hive needs administrator rights to write, so a
	// per-user or Steam install writes only HKCU - and a stale HKLM entry left by some other copy of
	// the game used to win, sending this one at another install's language or data folder.
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s\n", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";

	fullPath.concat(path);
	DEBUG_LOG(("GetUnsignedIntFromRegistry - looking in %s for key %s\n", fullPath.str(), key.str()));
	if (getUnsignedIntFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getUnsignedIntFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

#else
/* Off Windows there is no registry.  The same three reads come from one file, Registry.ini in the user
	 data directory, in Options.ini's "key = value" form and read by the same parser, EarlyOptions'
	 findEarlyOptionValueIn: case-insensitive keys, the last one wins.  A Zero Hour value's key is its
	 registry path below the game's key, then its name - "Language", or "ergc\\<name>" for the \\ergc
	 subkey - and an original-Generals value's is the same under "Generals\\".  Windows' two hives, per
	 user and per machine, are one file here.

	 Nothing in the engine writes the file: on Windows the installer writes the registry, and here that
	 is the future launcher's or installer's job (C1's task file says so).  Until C1 places the user data
	 directory, findUserDataDirectory has none, no file is read, and every caller keeps its compiled-in
	 default - GetRegistryLanguage's "english", GetRegistryVersion's 65536. */

/** The value of one key in a Registry.ini file already chosen; FALSE if the file or the key is not there. */
static Bool readRegistryFileAt( const char *file, const AsciiString &name, AsciiString &val )
{
	FILE *fp = fopen( file, "r" );
	if (fp == NULL)
		return FALSE;
	char value[ 256 ];
	const bool found = findEarlyOptionValueIn( fp, name.str(), value, sizeof( value ) );
	fclose( fp );
	if (found)
		val = value;
	return found ? TRUE : FALSE;
}

static Bool readRegistryFile( const AsciiString &name, AsciiString &val )
{
	char directory[ 1024 ];
	if (!findUserDataDirectory( directory, sizeof( directory ) ))
		return FALSE;
	AsciiString file;
	file.format( "%sRegistry.ini", directory );		// the directory ends in its separator, as on Windows
	return readRegistryFileAt( file.str(), name, val );
}

/** "Generals\\" or "", then the path below the game's key without its leading backslashes, then the name. */
static AsciiString registryFileKey( const char *tree, const AsciiString &path, const AsciiString &key )
{
	AsciiString name = tree;
	const char *below = path.str();
	while (*below == '\\')
		++below;
	if (*below != 0)
	{
		name.concat( below );
		name.concat( '\\' );
	}
	name.concat( key );
	return name;
}

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	return readRegistryFile( registryFileKey( "Generals\\", path, key ), val );
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	return readRegistryFile( registryFileKey( "", path, key ), val );
}

/** A REG_DWORD on Windows; here decimal text.  Text that is not a whole decimal number reads as missing. */
Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
	AsciiString text;
	if (!readRegistryFile( registryFileKey( "", path, key ), text ))
		return FALSE;
	char *end = NULL;
	const unsigned long parsed = strtoul( text.str(), &end, 10 );
	if (end == text.str() || *end != 0)
		return FALSE;
	val = (UnsignedInt)parsed;
	return TRUE;
}
#endif

AsciiString GetRegistryLanguage(void)
{
	static Bool cached = FALSE;
	// NOTE: static causes a memory leak, but we have to keep it because the value is cached.
	static AsciiString val = "english";
	if (cached) {
		return val;
	} else {
		cached = TRUE;
	}

	GetStringFromRegistry("", "Language", val);
	return val;
}

AsciiString GetRegistryGameName(void)
{
	AsciiString val = "GeneralsMPTest";
	GetStringFromRegistry("", "SKU", val);
	return val;
}

UnsignedInt GetRegistryVersion(void)
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "Version", val);
	return val;
}

UnsignedInt GetRegistryMapPackVersion(void)
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "MapPackVersion", val);
	return val;
}
