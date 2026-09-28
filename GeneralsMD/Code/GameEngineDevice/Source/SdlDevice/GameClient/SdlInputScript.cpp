/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

// SdlInputScript.cpp: see SdlInputScript.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "GameClient/Display.h"
#include "GameClient/GamepadMap.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlInputScript.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>

namespace {

struct Action
{
	UnsignedInt frame;		///< the logic frame it waits for, or with byPass the engine pass
	Bool byPass;					///< "p<n>": in the shell the logic frame stands still, so passes count there
	std::string line;
};

std::vector<Action> theActions;
size_t theNext = 0;
UnsignedInt thePasses = 0;		///< SdlInputScript_play's calls: one an engine pass
SDL_Joystick *thePad = NULL;

const char *const theAxisNames[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger", NULL };

Int lookup( const char *name, const char *const *names )
{
	for (Int i = 0; names[i] != NULL; ++i)
		if (strcasecmp( name, names[i] ) == 0)
			return i;
	return -1;
}

Int buttonNamed( const char *name )
{
	for (const LookupListRec *rec = TheGamepadButtonNames; rec->name != NULL; ++rec)
		if (strcasecmp( name, rec->name ) == 0)
			return rec->value;
	return -1;
}

void pushMouse( Uint32 type, Int x, Int y, Uint8 button, bool down )
{
	SDL_Window *window = SdlInput_gameWindow();
	Real wx, wy;
	SdlInput_toWindowPoints( x, y, wx, wy );
	SDL_Event event;
	SDL_zero( event );
	event.type = type;
	if (type == SDL_EVENT_MOUSE_MOTION)
	{
		event.motion.windowID = window != NULL ? SDL_GetWindowID( window ) : 0;
		event.motion.x = wx;
		event.motion.y = wy;
	}
	else
	{
		event.button.windowID = window != NULL ? SDL_GetWindowID( window ) : 0;
		event.button.button = button;
		event.button.down = down;
		event.button.clicks = 1;
		event.button.x = wx;
		event.button.y = wy;
	}
	SDL_PushEvent( &event );
}

/// One action; FALSE when the line is not one this knows
Bool play( const char *line )
{
	char kind[ 16 ] = "", a[ 64 ] = "", b[ 64 ] = "";
	int x = 0, y = 0;
	const int fields = sscanf( line, "%*s %15s %63s %63s %d %d", kind, a, b, &x, &y );
	if (strcmp( kind, "quit" ) == 0 && TheGameEngine != NULL)
	{
		TheGameEngine->setQuitting( TRUE );
		return TRUE;
	}
	if (strcmp( kind, "shot" ) == 0 && TheDisplay != NULL)
	{
		TheDisplay->takeScreenShot();		// written out of the back buffer on the next draw
		return TRUE;
	}
	if (strcmp( kind, "pad" ) == 0 && thePad != NULL)
	{
		if (strcmp( a, "axis" ) == 0 && fields >= 4)
		{
			const Int axis = lookup( b, theAxisNames );
			if (axis < 0)
				return FALSE;
			SDL_SetJoystickVirtualAxis( thePad, axis, (Sint16)x );
		}
		else
		{
			const Int button = buttonNamed( a );
			if (button < 0 || fields < 3)
				return FALSE;
			SDL_SetJoystickVirtualButton( thePad, button, strcmp( b, "down" ) == 0 );
		}
		SDL_UpdateJoysticks();		// each change its own event: two changes in one update would be none
		return TRUE;
	}
	if (strcmp( kind, "mouse" ) == 0)
	{
		if (strcmp( a, "move" ) == 0 && sscanf( line, "%*s %*s %*s %d %d", &x, &y ) == 2)
		{
			pushMouse( SDL_EVENT_MOUSE_MOTION, x, y, 0, false );
			return TRUE;
		}
		const Uint8 button = strcmp( a, "left" ) == 0 ? SDL_BUTTON_LEFT : (strcmp( a, "middle" ) == 0 ? SDL_BUTTON_MIDDLE
			: (strcmp( a, "right" ) == 0 ? SDL_BUTTON_RIGHT : 0));
		if (button == 0 || fields < 5)
			return FALSE;
		const bool down = strcmp( b, "down" ) == 0;
		pushMouse( down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP, x, y, button, down );
		return TRUE;
	}
	if (strcmp( kind, "key" ) == 0 && fields >= 3)
	{
		for (char *c = a; *c != 0; ++c)
			if (*c == '_')
				*c = ' ';		// SDL's names have spaces ("Left Ctrl"); a script's field has none
		const SDL_Scancode scancode = SDL_GetScancodeFromName( a );
		if (scancode == SDL_SCANCODE_UNKNOWN)
			return FALSE;
		SDL_Window *window = SdlInput_gameWindow();
		SDL_Event event;
		SDL_zero( event );
		const bool down = strcmp( b, "down" ) == 0;
		event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.windowID = window != NULL ? SDL_GetWindowID( window ) : 0;
		event.key.scancode = scancode;
		event.key.down = down;
		SDL_PushEvent( &event );
		return TRUE;
	}
	return FALSE;
}

}  // namespace

Bool SdlInputScript_start( void )
{
	const char *path = getenv( "ZH_INPUT_SCRIPT" );
	if (path == NULL || *path == 0 || !theActions.empty())
		return !theActions.empty();
	FILE *file = fopen( path, "r" );
	if (file == NULL)
	{
		fprintf( stderr, "generals: input script: cannot read %s\n", path );
		return FALSE;
	}
	char line[ 256 ];
	Bool padLines = FALSE;
	while (fgets( line, sizeof( line ), file ) != NULL)
	{
		line[ strcspn( line, "\r\n" ) ] = 0;
		unsigned int frame = 0;
		char kind[ 16 ] = "";
		const Bool byPass = line[0] == 'p';
		if (line[0] == '#' || sscanf( byPass ? line + 1 : line, "%u %15s", &frame, kind ) != 2)
			continue;
		Action action;
		action.frame = frame;
		action.byPass = byPass;
		action.line = line;
		theActions.push_back( action );
		padLines = padLines || strcmp( kind, "pad" ) == 0;
	}
	fclose( file );
	if (padLines)
	{
		SDL_VirtualJoystickDesc desc;
		SDL_INIT_INTERFACE( &desc );
		desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
		desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
		desc.name = "ZH_INPUT_SCRIPT pad";
		// ZH_INPUT_SCRIPT_PAD: which family the pad is, by the USB ids SDL knows it by; none is SDL's own layout
		struct Family { const char *name; Uint16 vendor, product; const char *padName; };
		static const Family families[] = {
			{ "xbox", 0x045e, 0x0b12, "Xbox Series X Controller" },
			{ "playstation", 0x054c, 0x0ce6, "DualSense Wireless Controller" },
			{ "nintendo", 0x057e, 0x2009, "Nintendo Switch Pro Controller" },
			{ "deck", 0x28de, 0x1205, "Steam Deck" },
			{ NULL, 0, 0, NULL } };
		const char *family = getenv( "ZH_INPUT_SCRIPT_PAD" );
		for (const Family *f = families; family != NULL && f->name != NULL; ++f)
			if (strcasecmp( family, f->name ) == 0)
			{
				desc.vendor_id = f->vendor;
				desc.product_id = f->product;
				desc.name = f->padName;
			}
		const SDL_JoystickID id = SDL_AttachVirtualJoystick( &desc );
		thePad = id != 0 ? SDL_OpenJoystick( id ) : NULL;
		if (thePad != NULL)
		{
			SDL_SetJoystickVirtualAxis( thePad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );		// triggers at rest
			SDL_SetJoystickVirtualAxis( thePad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
		}
	}
	// the window, and this with it, starts before the debug log is open: stderr says it
	fprintf( stderr, "generals: input script: %d actions from %s%s\n", (int)theActions.size(), path,
		padLines ? (thePad != NULL ? ", on a virtual pad" : ", but no virtual pad") : "" );
	return !theActions.empty();
}

void SdlInputScript_play( UnsignedInt logicFrame )
{
	++thePasses;
	while (theNext < theActions.size()
			&& theActions[ theNext ].frame <= (theActions[ theNext ].byPass ? thePasses : logicFrame))
	{
		const Action &action = theActions[ theNext++ ];
		const Bool known = play( action.line.c_str() );
		DEBUG_LOG(( "INPUT SCRIPT: frame %u: %s%s\n", logicFrame, action.line.c_str(), known ? "" : " (not understood)" ));
	}
}
