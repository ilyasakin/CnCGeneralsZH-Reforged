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
#include "GameClient/GamepadFocus.h"
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
	Bool settled;					///< "s": when the menu has stood still (GamepadFocus::isSettled), whatever the clock
	Bool next;						///< "n": straight after the action before it, in the same pass
	std::string line;
};

/// How long the menu must stand still before an "s" step: a second, where the default focus's own rule is 250 ms,
/// because a pane can hold its widgets still while its hover and focus are still catching up (seen on the M3 Pro Mac: a
/// Back that came 250 ms after the difficulty pane stood still moved the focus to Hard instead)
const UnsignedInt SETTLED_MS = 1000;
const UnsignedInt AGAIN_MS = 2000;		///< how long after a press that moved nothing it is pressed again (the menu still settled)
const Int MAX_AGAIN = 2;

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
		const Bool settled = line[0] == 's' && line[1] == ' ', next = line[0] == 'n' && line[1] == ' ';
		if (line[0] == '#')
			continue;
		if (settled || next)
		{
			if (sscanf( line + 2, "%15s", kind ) != 1)
				continue;
		}
		else if (sscanf( byPass ? line + 1 : line, "%u %15s", &frame, kind ) != 2)
			continue;
		Action action;
		action.frame = frame;
		action.byPass = byPass;
		action.settled = settled;
		action.next = next;
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
	static UnsignedInt lastPlayedPass = 0;
	// the last "s" step played, the focus changes counted then, and how often it has been pressed again
	static size_t lastStep = (size_t)-1;
	static UnsignedInt changesAtStep = 0, pressedAt = 0;
	static Int again = 0;
	Bool repeating = FALSE;
	while (theNext < theActions.size())
	{
		const Action &due = theActions[ theNext ];
		if (due.settled && !repeating)
		{
			// a menu step: after the step before has had a pass, and the menu has stood still since
			if (lastPlayedPass == thePasses)
				break;
			// every step of a menu walk moves the focus or the screen; one that moved nothing was dropped (the
			// menus ignore a press while a transition runs), so once the menu has stood still a while it is pressed
			// again, twice at most, and logged apart so that a walk's count of steps still checks
			const Bool tookNothing = lastStep != (size_t)-1 && GamepadFocus::focusChanges() == changesAtStep;
			if (tookNothing && again < MAX_AGAIN)
			{
				// measured from the press: the menu stood still before it too, since the press moved nothing
				if ((UnsignedInt)SDL_GetTicks() - pressedAt < AGAIN_MS || !GamepadFocus::isSettled( SETTLED_MS ))
					break;
				++again;
				theNext = lastStep;
				repeating = TRUE;
				continue;
			}
			if (!GamepadFocus::isSettled( SETTLED_MS ))
				break;
		}
		else if (!due.settled && !due.next && due.frame > (due.byPass ? thePasses : logicFrame))
			break;
		lastPlayedPass = thePasses;
		if (due.settled)
		{
			if (!repeating)
				again = 0;
			lastStep = theNext;
			changesAtStep = GamepadFocus::focusChanges();
			pressedAt = (UnsignedInt)SDL_GetTicks();
		}
		const Action &action = theActions[ theNext++ ];
		const Bool known = play( action.line.c_str() );
		if (repeating)
			DEBUG_LOG(( "INPUT SCRIPT AGAIN: frame %u: %s (the press before changed nothing)\n", logicFrame, action.line.c_str() ));
		else
			DEBUG_LOG(( "INPUT SCRIPT: frame %u: %s%s\n", logicFrame, action.line.c_str(), known ? "" : " (not understood)" ));
		if (repeating && (theNext >= theActions.size() || !theActions[ theNext ].next))
			repeating = FALSE;		// the step and its "n" release, pressed again
	}
}
