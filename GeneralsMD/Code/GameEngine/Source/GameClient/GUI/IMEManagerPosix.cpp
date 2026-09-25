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

// FILE: IMEManagerPosix.cpp //////////////////////////////////////////////////////////////////////
// Desc:   GameClient/IMEManager.h off Windows: no input method editor yet.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* IMEManager.cpp is the Windows body, built on IMM32 (the Windows input method editor for Chinese,
	 Japanese and Korean text entry), and is not built here.  Off Windows there is no IME manager:
	 CreateIMEManagerInterface answers NULL, which GameClient::init already handles, and every other
	 caller tests TheIMEManager first.  Text input off Windows, IME composition included, is C3's
	 (SDL3's text-input events). */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/IMEManager.h"

IMEManagerInterface *TheIMEManager = NULL;

IMEManagerInterface *CreateIMEManagerInterface( void )
{
	return NULL;
}
