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

// The engine's character type for text, and nothing else.
//
// Its own header, so that a library which needs the type but not the engine's preamble can have it.
// WW3D2's text renderer is the reason: it takes the engine's strings, and pulling Lib/BaseType.h into
// WW3D2 would bring `#define NULL 0` against four WWLib headers that already define NULL differently,
// which is ill-formed (measured, B1's handoff §5a).  So this file includes nothing and defines one name.
// BaseType.h includes it, so nothing that already had WideChar loses it.

#pragma once

#ifndef LIB_WIDECHAR_H
#define LIB_WIDECHAR_H

typedef wchar_t WideChar;	///< UTF-16 code unit on Windows, where wchar_t is two bytes

#endif // LIB_WIDECHAR_H
