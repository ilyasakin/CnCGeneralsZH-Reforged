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

// SdlGamepad.cpp: see SdlGamepad.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/Display.h"
#include "GameClient/GamepadMap.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "Platform/DoubleClickTime.h"
#include "SdlDevice/GameClient/SdlGamepad.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"
#include "SdlDevice/GameClient/SdlMouse.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// GamepadMap names the buttons in SDL's order, so an SDL button is a GamepadButtonType as it is
static_assert( (Int)SDL_GAMEPAD_BUTTON_SOUTH == (Int)GAMEPAD_BUTTON_SOUTH, "GamepadMap.h follows SDL_GamepadButton" );
static_assert( (Int)SDL_GAMEPAD_BUTTON_DPAD_UP == (Int)GAMEPAD_BUTTON_DPAD_UP, "GamepadMap.h follows SDL_GamepadButton" );
static_assert( (Int)SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 == (Int)GAMEPAD_BUTTON_LEFT_PADDLE2, "GamepadMap.h follows SDL_GamepadButton" );
static_assert( (Int)SDL_GAMEPAD_BUTTON_TOUCHPAD == (Int)GAMEPAD_BUTTON_TOUCHPAD, "GamepadMap.h follows SDL_GamepadButton" );

namespace {

enum { MAX_PADS = 8, MAX_PRESSED_KEYS = 4, MOUSE_BUTTONS = 3 };

const Real TRIGGER_DEAD_ZONE = 0.1f;
const Real WHEEL_NOTCHES_PER_SECOND = 6.0f;		///< a trigger pulled all the way
const Real CAMERA_KEY_ON = 0.5f;							///< the right stick's tilt that holds an arrow key...
const Real CAMERA_KEY_OFF = 0.35f;						///< ...and the tilt below which it lets go
const Int DOUBLE_CLICK_DISTANCE = 4;					///< Windows' SM_CXDOUBLECLK and SM_CYDOUBLECLK
const Real STICK_DEAD_ZONE = 0.15f;						///< the left stick's, radial
const Real POINTER_CROSSING_SECONDS = 1.2f;		///< full tilt crosses the screen's width in this long
const Int EDGE_BAND = 3;											///< LookAtXlat's edgeScrollSize: a pointer this near an edge scrolls
const Int WARP_PATIENCE_FRAMES = 5;						///< frames a warp may take to come back before it is given up on

/// What one press did, so that its release undoes exactly that
struct Pressed
{
	Int mouseButton;													///< an SdlMouse::Button, or -1
	UnsignedByte keys[ MAX_PRESSED_KEYS ];		///< in the order they went down
	Int keyCount;
};

struct Pad
{
	SDL_Gamepad *gamepad;
	SDL_JoystickID id;
	Bool held[ GAMEPAD_BUTTON_COUNT ];
	Pressed pressed[ GAMEPAD_BUTTON_COUNT ];
	GamepadBinding binding[ GAMEPAD_BUTTON_COUNT ];			///< what the press was bound to, to press it again after a chord
	Bool bound[ GAMEPAD_BUTTON_COUNT ];
	GamepadButtonType chordWith[ GAMEPAD_BUTTON_COUNT ];	///< the held button this press made a chord with
	Int suspended[ GAMEPAD_BUTTON_COUNT ];							///< chords that have let this held button's keys go
	Real axis[ SDL_GAMEPAD_AXIS_COUNT ];
	Bool cameraKey[ 4 ];
	Real wheelCarry;
};

struct LastClick
{
	Bool valid;
	UnsignedInt time;
	Int x, y;
	Int clicks;
};

const UnsignedByte theCameraKeys[ 4 ] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };

std::vector<Pad> thePads;
Int theKeyHolds[ 256 ];
Int theMouseHolds[ MOUSE_BUTTONS ];
LastClick theLastClicks[ MOUSE_BUTTONS ];
Bool theStarted = FALSE;
Bool theUpdated = FALSE;
UnsignedInt theLastUpdate = 0;
Bool theLastUsed = FALSE;					///< a pad, not a hand, was the last thing used

/// The pointer the left stick moves
struct Pointer
{
	Bool owned;						///< the pad has it; FALSE once a hand moves the mouse
	Real x, y;						///< in the game's pixels, the fraction kept
	Int shownX, shownY;		///< where it was last put
	Bool warpPending;			///< a warp has not come back yet
	Int warpWait;					///< frames it has waited
	Bool direct;					///< warps never come back here: moves go to SdlMouse directly
};
Pointer thePointer;

UnsignedInt milliseconds( Uint64 timestampNs )
{
	return (UnsignedInt)(timestampNs / 1000000u);
}

void keyDown( UnsignedByte dik )
{
	if (theKeyHolds[ dik ]++ == 0 && SdlKeyboard::active() != NULL)
		SdlKeyboard::active()->addKey( dik, TRUE );
}

void keyUp( UnsignedByte dik )
{
	if (theKeyHolds[ dik ] > 0 && --theKeyHolds[ dik ] == 0 && SdlKeyboard::active() != NULL)
		SdlKeyboard::active()->addKey( dik, FALSE );
}

/// Where a pad's click or wheel goes: the pad's pointer while it has it, else where the mouse left it
void pointerPosition( SdlMouse *mouse, Int &x, Int &y )
{
	if (thePointer.owned)
	{
		x = thePointer.shownX;
		y = thePointer.shownY;
	}
	else
		mouse->getPointerPosition( x, y );
}

void mouseDown( Int button, UnsignedInt time )
{
	if (theMouseHolds[ button ]++ != 0)
		return;
	SdlMouse *mouse = SdlMouse::active();
	if (mouse == NULL)
		return;
	Int x, y;
	pointerPosition( mouse, x, y );
	// SDL's click count: one more for a press soon enough after the last and near enough to it
	LastClick &last = theLastClicks[ button ];
	Int clicks = 1;
	if (last.valid && time - last.time <= systemDoubleClickTimeMS()
			&& abs( x - last.x ) <= DOUBLE_CLICK_DISTANCE && abs( y - last.y ) <= DOUBLE_CLICK_DISTANCE)
		clicks = last.clicks + 1;
	last.valid = TRUE;
	last.time = time;
	last.x = x;
	last.y = y;
	last.clicks = clicks;
	mouse->addEvent( SdlMouse::EVENT_BUTTON_DOWN, x, y, (SdlMouse::Button)button, clicks, 0, time );
}

void mouseUp( Int button, UnsignedInt time )
{
	if (theMouseHolds[ button ] <= 0 || --theMouseHolds[ button ] != 0)
		return;
	SdlMouse *mouse = SdlMouse::active();
	if (mouse == NULL)
		return;
	Int x, y;
	pointerPosition( mouse, x, y );
	mouse->addEvent( SdlMouse::EVENT_BUTTON_UP, x, y, (SdlMouse::Button)button, theLastClicks[ button ].clicks, 0, time );
}

void pressKeys( Pressed &pressed, MappableKeyType key, MappableKeyModState modState )
{
	// the modifiers first, so the key goes down with them held, as a hand types it
	if (modState & CTRL)
		pressed.keys[ pressed.keyCount++ ] = KEY_LCTRL;
	if (modState & ALT)
		pressed.keys[ pressed.keyCount++ ] = KEY_LALT;
	if (modState & SHIFT)
		pressed.keys[ pressed.keyCount++ ] = KEY_LSHIFT;
	if (key != MK_NONE)
		pressed.keys[ pressed.keyCount++ ] = (UnsignedByte)key;
	for (Int i = 0; i < pressed.keyCount; ++i)
		keyDown( pressed.keys[ i ] );
}

Pressed apply( const GamepadBinding &binding, UnsignedInt time )
{
	Pressed pressed;
	pressed.mouseButton = -1;
	pressed.keyCount = 0;
	switch (binding.m_action)
	{
		case GAMEPAD_ACTION_MOUSE_LEFT:		pressed.mouseButton = SdlMouse::BUTTON_LEFT; break;
		case GAMEPAD_ACTION_MOUSE_MIDDLE:	pressed.mouseButton = SdlMouse::BUTTON_MIDDLE; break;
		case GAMEPAD_ACTION_MOUSE_RIGHT:	pressed.mouseButton = SdlMouse::BUTTON_RIGHT; break;
		case GAMEPAD_ACTION_MODIFIER:
			pressKeys( pressed, MK_NONE, binding.m_modState );
			break;
		case GAMEPAD_ACTION_KEY:
			pressKeys( pressed, binding.m_key, binding.m_modState );
			break;
		case GAMEPAD_ACTION_COMMAND:
		{
			// whatever the player's input scheme binds the command to, now
			MappableKeyType key;
			MappableKeyModState modState;
			if (GamepadMap::keyForCommand( binding.m_command, key, modState ))
				pressKeys( pressed, key, modState );
			break;
		}
		default:
			break;
	}
	if (pressed.mouseButton >= 0)
		mouseDown( pressed.mouseButton, time );
	return pressed;
}

void unapply( Pressed &pressed, UnsignedInt time )
{
	if (pressed.mouseButton >= 0)
		mouseUp( pressed.mouseButton, time );
	for (Int i = pressed.keyCount - 1; i >= 0; --i)
		keyUp( pressed.keys[ i ] );
	pressed.mouseButton = -1;
	pressed.keyCount = 0;
}

Pad *findPad( SDL_JoystickID id )
{
	for (size_t i = 0; i < thePads.size(); ++i)
		if (thePads[i].id == id)
			return &thePads[i];
	return NULL;
}

void pressButton( Pad &pad, GamepadButtonType button, UnsignedInt time )
{
	if (pad.held[ button ])
		return;
	pad.held[ button ] = TRUE;
	pad.bound[ button ] = FALSE;
	pad.chordWith[ button ] = GAMEPAD_BUTTON_NONE;
	if (TheGamepadMap != NULL)
	{
		// a chord with a button already held wins over the button's own binding
		for (Int with = 0; with < GAMEPAD_BUTTON_COUNT && !pad.bound[ button ]; ++with)
		{
			if (with == button || !pad.held[ with ])
				continue;
			const GamepadBinding *chord = TheGamepadMap->find( button, (GamepadButtonType)with );
			if (chord != NULL)
			{
				pad.binding[ button ] = *chord;
				pad.bound[ button ] = TRUE;
				pad.chordWith[ button ] = (GamepadButtonType)with;
			}
		}
		const GamepadBinding *own = pad.bound[ button ] ? NULL : TheGamepadMap->find( button, GAMEPAD_BUTTON_NONE );
		if (own != NULL)
		{
			pad.binding[ button ] = *own;
			pad.bound[ button ] = TRUE;
		}
	}
	// the held button's own keys let go while the chord lasts: its Shift is not part of the chord
	const GamepadButtonType with = pad.chordWith[ button ];
	if (with != GAMEPAD_BUTTON_NONE && pad.suspended[ with ]++ == 0)
		unapply( pad.pressed[ with ], time );
	pad.pressed[ button ].mouseButton = -1;
	pad.pressed[ button ].keyCount = 0;
	if (pad.bound[ button ])
		pad.pressed[ button ] = apply( pad.binding[ button ], time );
}

void releaseButton( Pad &pad, GamepadButtonType button, UnsignedInt time )
{
	if (!pad.held[ button ])
		return;
	pad.held[ button ] = FALSE;
	unapply( pad.pressed[ button ], time );		// nothing left in it while a chord has it let go
	// chords made with this button no longer give anything back to it
	pad.suspended[ button ] = 0;
	for (Int other = 0; other < GAMEPAD_BUTTON_COUNT; ++other)
		if (pad.chordWith[ other ] == button)
			pad.chordWith[ other ] = GAMEPAD_BUTTON_NONE;
	// ...and a chord ending gives the button it was made with its keys back, if it is still held
	const GamepadButtonType with = pad.chordWith[ button ];
	pad.chordWith[ button ] = GAMEPAD_BUTTON_NONE;
	if (with != GAMEPAD_BUTTON_NONE && pad.suspended[ with ] > 0 && --pad.suspended[ with ] == 0
			&& pad.held[ with ] && pad.bound[ with ])
		pad.pressed[ with ] = apply( pad.binding[ with ], time );
}

void releasePad( Pad &pad, UnsignedInt time )
{
	for (Int button = 0; button < GAMEPAD_BUTTON_COUNT; ++button)
		releaseButton( pad, (GamepadButtonType)button, time );
	for (Int i = 0; i < 4; ++i)
		if (pad.cameraKey[ i ])
		{
			pad.cameraKey[ i ] = FALSE;
			keyUp( theCameraKeys[ i ] );
		}
	for (Int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
		pad.axis[ axis ] = 0.0f;
	pad.wheelCarry = 0.0f;
}

void openPad( SDL_JoystickID id )
{
	if (thePads.size() >= MAX_PADS || findPad( id ) != NULL)
		return;
	SDL_Gamepad *gamepad = SDL_OpenGamepad( id );
	if (gamepad == NULL)
		return;
	Pad pad;
	memset( &pad, 0, sizeof( pad ) );
	pad.gamepad = gamepad;
	pad.id = id;
	for (Int button = 0; button < GAMEPAD_BUTTON_COUNT; ++button)
	{
		pad.chordWith[ button ] = GAMEPAD_BUTTON_NONE;
		pad.pressed[ button ].mouseButton = -1;
	}
	thePads.push_back( pad );
}

void closePad( SDL_JoystickID id, UnsignedInt time )
{
	for (size_t i = 0; i < thePads.size(); ++i)
		if (thePads[i].id == id)
		{
			releasePad( thePads[i], time );
			SDL_CloseGamepad( thePads[i].gamepad );
			thePads.erase( thePads.begin() + i );
			return;
		}
}

/// The game's screen in pixels: the display's, or the window's before there is one
void screenSize( Int &width, Int &height )
{
	width = TheDisplay != NULL ? (Int)TheDisplay->getWidth() : 0;
	height = TheDisplay != NULL ? (Int)TheDisplay->getHeight() : 0;
	SDL_Window *window = SdlInput_gameWindow();
	if ((width <= 0 || height <= 0) && window != NULL)
		SDL_GetWindowSize( window, &width, &height );
}

/// The pointer to a pixel: warped, so it comes back as the platform's own motion, or straight to SdlMouse
void showPointer( Int x, Int y, UnsignedInt time )
{
	thePointer.shownX = x;
	thePointer.shownY = y;
	SDL_Window *window = SdlInput_gameWindow();
	if (!thePointer.direct && window != NULL)
	{
		Real wx, wy;
		SdlInput_toWindowPoints( x, y, wx, wy );
		SDL_WarpMouseInWindow( window, wx, wy );
		thePointer.warpPending = TRUE;
		thePointer.warpWait = 0;
	}
	else if (SdlMouse::active() != NULL)
		SdlMouse::active()->addEvent( SdlMouse::EVENT_MOVE, x, y, SdlMouse::BUTTON_LEFT, 0, 0, time );
}

/// The pad was used: it takes the pointer, and one parked in the edge band goes to the centre
void padUsed( UnsignedInt time )
{
	if (theLastUsed)
		return;
	theLastUsed = TRUE;
	if (thePointer.owned || SdlMouse::active() == NULL)
		return;
	Int x, y, width, height;
	SdlMouse::active()->getPointerPosition( x, y );
	screenSize( width, height );
	thePointer.owned = TRUE;
	thePointer.x = (Real)x;
	thePointer.y = (Real)y;
	thePointer.shownX = x;
	thePointer.shownY = y;
	if (width > 0 && height > 0 && (x < EDGE_BAND || y < EDGE_BAND || x >= width - EDGE_BAND || y >= height - EDGE_BAND))
	{
		thePointer.x = (Real)(width / 2);
		thePointer.y = (Real)(height / 2);
		showPointer( width / 2, height / 2, time );
	}
}

Real triggerPull( Real value )
{
	return value <= TRIGGER_DEAD_ZONE ? 0.0f : (value - TRIGGER_DEAD_ZONE) / (1.0f - TRIGGER_DEAD_ZONE);
}

}  // namespace

Bool SdlGamepad_start( void )
{
	if (!theStarted)
	{
		theStarted = SDL_InitSubSystem( SDL_INIT_GAMEPAD );
		if (!theStarted)
			DEBUG_LOG(( "SdlGamepad: no gamepads: %s\n", SDL_GetError() ));
	}
	return theStarted;
}

void SdlGamepad_stop( void )
{
	if (!theStarted)
		return;
	SdlGamepad_releaseAll();
	for (size_t i = 0; i < thePads.size(); ++i)
		SDL_CloseGamepad( thePads[i].gamepad );
	thePads.clear();
	SDL_QuitSubSystem( SDL_INIT_GAMEPAD );
	theStarted = FALSE;
	theUpdated = FALSE;
	memset( theKeyHolds, 0, sizeof( theKeyHolds ) );
	memset( theMouseHolds, 0, sizeof( theMouseHolds ) );
	memset( theLastClicks, 0, sizeof( theLastClicks ) );
	memset( &thePointer, 0, sizeof( thePointer ) );
	theLastUsed = FALSE;
}

void SdlGamepad_releaseAll( void )
{
	const UnsignedInt now = (UnsignedInt)SDL_GetTicks();
	for (size_t i = 0; i < thePads.size(); ++i)
		releasePad( thePads[i], now );
}

Int SdlGamepad_count( void )
{
	return (Int)thePads.size();
}

void SdlGamepad_noteMotion( Int x, Int y )
{
	if (thePointer.warpPending && x == thePointer.shownX && y == thePointer.shownY)
	{
		thePointer.warpPending = FALSE;		// the pad's own warp, back as the platform's motion
		return;
	}
	if (thePointer.warpPending)
		return;		// the platform's motion from before the warp, still in the queue
	thePointer.owned = FALSE;
	theLastUsed = FALSE;
}

void SdlGamepad_noteHand( void )
{
	theLastUsed = FALSE;
}

Bool SdlGamepad_isLastUsed( void )
{
	return theLastUsed;
}

Bool SdlGamepad_pointer( Int &x, Int &y )
{
	x = thePointer.shownX;
	y = thePointer.shownY;
	return thePointer.owned;
}

Bool SdlGamepad_dispatch( const SDL_Event &event )
{
	switch (event.type)
	{
		case SDL_EVENT_GAMEPAD_ADDED:
			openPad( event.gdevice.which );
			return TRUE;

		case SDL_EVENT_GAMEPAD_REMOVED:
			closePad( event.gdevice.which, milliseconds( event.gdevice.timestamp ) );
			return TRUE;

		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		{
			Pad *pad = findPad( event.gbutton.which );
			if (pad == NULL || event.gbutton.button >= GAMEPAD_BUTTON_COUNT)
				return TRUE;
			const GamepadButtonType button = (GamepadButtonType)event.gbutton.button;		// the same order (GamepadMap.h)
			if (event.gbutton.down)
				padUsed( milliseconds( event.gbutton.timestamp ) );
			if (event.gbutton.down)
				pressButton( *pad, button, milliseconds( event.gbutton.timestamp ) );
			else
				releaseButton( *pad, button, milliseconds( event.gbutton.timestamp ) );
			return TRUE;
		}

		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		{
			Pad *pad = findPad( event.gaxis.which );
			if (pad != NULL && event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT)
			{
				const Real value = event.gaxis.value / 32767.0f;
				pad->axis[ event.gaxis.axis ] = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
				const Real deadZone = (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
					? TRIGGER_DEAD_ZONE : STICK_DEAD_ZONE;
				if (value > deadZone || value < -deadZone)
					padUsed( milliseconds( event.gaxis.timestamp ) );
			}
			return TRUE;
		}

		default:
			// the rest of the joystick and gamepad events (the joystick layer's own, remapping, touchpads,
			// sensors): nothing reads them
			return event.type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && event.type < SDL_EVENT_FINGER_DOWN;
	}
}

void SdlGamepad_update( UnsignedInt nowMs )
{
	Real seconds = theUpdated ? (nowMs - theLastUpdate) / 1000.0f : 0.0f;
	if (seconds < 0.0f || seconds > 0.1f)
		seconds = seconds < 0.0f ? 0.0f : 0.1f;		// a stall is not a long zoom
	theUpdated = TRUE;
	theLastUpdate = nowMs;

	// a warp that never comes back: this platform cannot warp the pointer (a Wayland without pointer
	// warping, perhaps gamescope): the moves go to SdlMouse from here on, and the log says why
	if (thePointer.warpPending && ++thePointer.warpWait > WARP_PATIENCE_FRAMES)
	{
		thePointer.warpPending = FALSE;
		thePointer.direct = TRUE;
		DEBUG_LOG(( "SdlGamepad: the pointer's warps do not come back on the %s video driver; the pad moves "
			"the game's pointer directly, and the platform's own pointer stays where it is\n", SDL_GetCurrentVideoDriver() ));
		if (SdlMouse::active() != NULL)
			SdlMouse::active()->addEvent( SdlMouse::EVENT_MOVE, thePointer.shownX, thePointer.shownY, SdlMouse::BUTTON_LEFT, 0, 0, nowMs );
	}

	// the left stick moves the pointer: every pad's tilt, a radial dead zone, a squared response
	Real vx = 0.0f, vy = 0.0f;
	for (size_t i = 0; i < thePads.size(); ++i)
	{
		const Real sx = thePads[i].axis[ SDL_GAMEPAD_AXIS_LEFTX ], sy = thePads[i].axis[ SDL_GAMEPAD_AXIS_LEFTY ];
		const Real tilt = sqrtf( sx * sx + sy * sy );
		if (tilt <= STICK_DEAD_ZONE)
			continue;
		const Real past = ((tilt > 1.0f ? 1.0f : tilt) - STICK_DEAD_ZONE) / (1.0f - STICK_DEAD_ZONE);
		vx += sx / tilt * past * past;
		vy += sy / tilt * past * past;
	}
	Int width, height;
	screenSize( width, height );
	if ((vx != 0.0f || vy != 0.0f) && width > 0 && height > 0 && seconds > 0.0f)
	{
		if (!thePointer.owned && SdlMouse::active() != NULL)
		{
			Int x, y;
			SdlMouse::active()->getPointerPosition( x, y );
			thePointer.owned = TRUE;
			thePointer.x = (Real)x;
			thePointer.y = (Real)y;
			thePointer.shownX = x;
			thePointer.shownY = y;
		}
		const Real speed = width / POINTER_CROSSING_SECONDS;		// pixels a second at full tilt
		thePointer.x += vx * speed * seconds;
		thePointer.y += vy * speed * seconds;
		// held to the screen: at an edge it edge-scrolls, as a mouse there does
		thePointer.x = thePointer.x < 0.0f ? 0.0f : (thePointer.x > width - 1 ? (Real)(width - 1) : thePointer.x);
		thePointer.y = thePointer.y < 0.0f ? 0.0f : (thePointer.y > height - 1 ? (Real)(height - 1) : thePointer.y);
		const Int x = (Int)thePointer.x, y = (Int)thePointer.y;
		if (x != thePointer.shownX || y != thePointer.shownY)
			showPointer( x, y, nowMs );
	}

	for (size_t i = 0; i < thePads.size(); ++i)
	{
		Pad &pad = thePads[i];

		// the triggers are the wheel: the right one in (up), the left one out, 120 a notch as SdlInput's
		const Real pull = triggerPull( pad.axis[ SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ] ) - triggerPull( pad.axis[ SDL_GAMEPAD_AXIS_LEFT_TRIGGER ] );
		pad.wheelCarry += pull * WHEEL_NOTCHES_PER_SECOND * 120.0f * seconds;
		const Int delta = (Int)pad.wheelCarry;		// toward zero: the fraction waits for the next frame
		pad.wheelCarry -= (Real)delta;
		if (delta != 0 && SdlMouse::active() != NULL)
		{
			Int x, y;
			pointerPosition( SdlMouse::active(), x, y );
			SdlMouse::active()->addEvent( SdlMouse::EVENT_WHEEL, x, y, SdlMouse::BUTTON_LEFT, 0, delta, nowMs );
		}

		// the right stick holds the arrow keys: up, down, left, right (a stick's up is negative)
		const Real rx = pad.axis[ SDL_GAMEPAD_AXIS_RIGHTX ], ry = pad.axis[ SDL_GAMEPAD_AXIS_RIGHTY ];
		const Real tilt[ 4 ] = { -ry, ry, -rx, rx };
		for (Int k = 0; k < 4; ++k)
		{
			const Bool want = tilt[ k ] > (pad.cameraKey[ k ] ? CAMERA_KEY_OFF : CAMERA_KEY_ON);
			if (want == pad.cameraKey[ k ])
				continue;
			pad.cameraKey[ k ] = want;
			if (want)
				keyDown( theCameraKeys[ k ] );
			else
				keyUp( theCameraKeys[ k ] );
		}
	}
}
