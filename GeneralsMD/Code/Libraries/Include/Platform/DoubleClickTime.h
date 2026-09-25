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

// How far apart two clicks may be and still be a double click, in milliseconds.
//
// A platform service, so it lives behind this name.  Windows returns the player's own setting,
// GetDoubleClickTime(), exactly as its callers always read it.  Elsewhere the setting is C3's to read
// (input is C3's), and until then this is 500, Windows' own default.
//
// Callers: GlobalData's m_doubleClickTimeMS, and GadgetListBox's static, which is initialised before
// GlobalData exists and so cannot read that field.

#pragma once

#if defined(_WIN32)
#include <windows.h>
inline unsigned int systemDoubleClickTimeMS( void ) { return ::GetDoubleClickTime(); }
#else
inline unsigned int systemDoubleClickTimeMS( void ) { return 500; }
#endif
