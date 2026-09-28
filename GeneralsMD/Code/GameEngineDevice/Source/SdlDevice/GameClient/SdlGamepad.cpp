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

#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GamepadAim.h"
#include "GameClient/GamepadFocus.h"
#include "GameClient/GamepadHints.h"
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
#include <stdio.h>
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
const Real AIM_FRICTION = 0.55f;								///< the pointer's speed over something aimable (GamepadAim.h)
const Int EDGE_BAND = 3;											///< LookAtXlat's edgeScrollSize: a pointer this near an edge scrolls

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
	Bool menuPress[ GAMEPAD_BUTTON_COUNT ];							///< pressed while a menu had the pad: its release is the menu's
	Real axis[ SDL_GAMEPAD_AXIS_COUNT ];
	Bool cameraKey[ 4 ];
	Real wheelCarry;
	GamepadGlyphSet glyphs;		///< whose buttons the hints draw while this pad is in use
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
	Bool drawn;						///< the game draws the cursor (RM_POLYGON) since a pad took it
};
Pointer thePointer;
Bool theCommandBarMode = FALSE;

// A menu's direction held down (the D-pad or the stick): it acts once, then again after a pause, then often
const UnsignedInt NAV_FIRST_REPEAT_MS = 350, NAV_REPEAT_MS = 110;
Int theNavAction = -1;						///< the GamepadFocus::Action held, or -1
UnsignedInt theNavNext = 0;				///< when it acts again
Int theStickNav = -1;							///< the direction the left stick holds in a menu, or -1
Int theTriggerNav = -1;						///< a trigger held in a menu: PAGE_UP or PAGE_DOWN, or -1

/// A window, and every window above it, shown
Bool isShown( GameWindow *window )
{
	for (GameWindow *w = window; w != NULL; w = w->winGetParent())
		if (w->winIsHidden())
			return FALSE;
	return window != NULL;
}

Bool windowCentre( GameWindow *window, Int &x, Int &y )
{
	Int left = 0, top = 0, width = 0, height = 0;
	window->winGetScreenPosition( &left, &top );
	window->winGetSize( &width, &height );
	x = left + width / 2;
	y = top + height / 2;
	return width > 0 && height > 0;
}

/// The first shown window with this id, below list: several message boxes share their buttons' names
GameWindow *findShown( GameWindow *list, NameKeyType id )
{
	for (GameWindow *window = list; window != NULL; window = window->winGetNext())
	{
		if (window->winIsHidden())
			continue;
		if (window->winGetWindowId() == (Int)id)
			return window;
		GameWindow *child = findShown( window->winGetChild(), id );
		if (child != NULL)
			return child;
	}
	return NULL;
}

GameWindow *findShown( const char *name )
{
	if (TheWindowManager == NULL || TheNameKeyGenerator == NULL)
		return NULL;
	return findShown( TheWindowManager->winGetWindowList(), TheNameKeyGenerator->nameToKey( AsciiString( name ) ) );
}

/// A message box's button shown now: its OK or Yes to accept, its Cancel or No not to (MessageBox.cpp)
GameWindow *dialogButton( Bool accept )
{
	static const char *const acceptNames[] = { "MessageBox.wnd:ButtonOk", "MessageBox.wnd:ButtonYes",
		"QuitMessageBox.wnd:ButtonOk", "QuitMessageBox.wnd:ButtonYes", NULL };
	static const char *const declineNames[] = { "MessageBox.wnd:ButtonCancel", "MessageBox.wnd:ButtonNo",
		"QuitMessageBox.wnd:ButtonCancel", "QuitMessageBox.wnd:ButtonNo", NULL };
	for (const char *const *name = accept ? acceptNames : declineNames; *name != NULL; ++name)
	{
		GameWindow *button = findShown( *name );
		if (button != NULL)
			return button;
	}
	return NULL;
}

// below, with the pointer they move
void movePointerTo( Int x, Int y, UnsignedInt time );
Bool enterCommandBar( UnsignedInt time );
void stepCommandBar( GamepadButtonType button, UnsignedInt time );

/// The command bar's buttons shown now, by their centres (ControlBar.cpp's names for them)
Int commandButtonCentres( ICoord2D *centres, Int max )
{
	Int count = 0;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET && count < max; ++i)
	{
		char name[ 64 ];
		snprintf( name, sizeof( name ), "ControlBar.wnd:ButtonCommand%02d", i + 1 );
		GameWindow *button = findShown( name );
		if (button != NULL && windowCentre( button, centres[ count ].x, centres[ count ].y ))
			++count;
	}
	return count;
}

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
		case GAMEPAD_ACTION_COMMAND_BAR:
			theCommandBarMode = theCommandBarMode ? FALSE : enterCommandBar( time );
			break;
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
	pad.pressed[ button ].mouseButton = -1;
	pad.pressed[ button ].keyCount = 0;

	// a menu, a popup or a box: the pad moves its focus and acts on it (GamepadFocus.h)
	if (GamepadFocus::isActive())
	{
		GamepadFocus::setPadDriving( TRUE );
		Int action = -1;
		switch (button)
		{
			case GAMEPAD_BUTTON_DPAD_UP:				action = GamepadFocus::NAV_UP; break;
			case GAMEPAD_BUTTON_DPAD_DOWN:			action = GamepadFocus::NAV_DOWN; break;
			case GAMEPAD_BUTTON_DPAD_LEFT:			action = GamepadFocus::NAV_LEFT; break;
			case GAMEPAD_BUTTON_DPAD_RIGHT:			action = GamepadFocus::NAV_RIGHT; break;
			case GAMEPAD_BUTTON_SOUTH:					action = GamepadFocus::ACCEPT_DOWN; break;
			case GAMEPAD_BUTTON_EAST:						action = GamepadFocus::BACK; break;
			case GAMEPAD_BUTTON_START:					action = GamepadFocus::START; break;
			case GAMEPAD_BUTTON_LEFT_SHOULDER:	action = GamepadFocus::TAB_PREV; break;
			case GAMEPAD_BUTTON_RIGHT_SHOULDER:	action = GamepadFocus::TAB_NEXT; break;
			case GAMEPAD_BUTTON_WEST:						action = GamepadFocus::ALT_X; break;
			case GAMEPAD_BUTTON_NORTH:					action = GamepadFocus::ALT_Y; break;
			default: break;
		}
		pad.menuPress[ button ] = TRUE;
		if (action >= 0)
			GamepadFocus::act( (GamepadFocus::Action)action );
		if (action >= GamepadFocus::NAV_UP && action <= GamepadFocus::NAV_RIGHT)
		{
			theNavAction = action;
			theNavNext = time + NAV_FIRST_REPEAT_MS;
		}
		return;
	}

	// command-bar mode has the D-pad, and East leaves it
	const Bool dpad = button == GAMEPAD_BUTTON_DPAD_UP || button == GAMEPAD_BUTTON_DPAD_DOWN
		|| button == GAMEPAD_BUTTON_DPAD_LEFT || button == GAMEPAD_BUTTON_DPAD_RIGHT;
	if (theCommandBarMode && dpad)
	{
		stepCommandBar( button, time );
		return;
	}
	if (theCommandBarMode && button == GAMEPAD_BUTTON_EAST)
	{
		theCommandBarMode = FALSE;
		return;
	}

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
	if (pad.bound[ button ])
		pad.pressed[ button ] = apply( pad.binding[ button ], time );
}

void releaseButton( Pad &pad, GamepadButtonType button, UnsignedInt time )
{
	if (!pad.held[ button ])
		return;
	pad.held[ button ] = FALSE;
	if (pad.menuPress[ button ])
	{
		// pressed in a menu: its release is the menu's too (A's click ends on the release)
		pad.menuPress[ button ] = FALSE;
		if (button == GAMEPAD_BUTTON_SOUTH)
			GamepadFocus::act( GamepadFocus::ACCEPT_UP );
		if (button >= GAMEPAD_BUTTON_DPAD_UP && button <= GAMEPAD_BUTTON_DPAD_RIGHT)
			theNavAction = -1;
		return;
	}
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

/// The Steam Deck itself, read once: its controls behind Steam Input are a pad SDL calls an Xbox one
Bool isSteamDeck( void )
{
	static int known = -1;
	if (known < 0)
	{
		known = 0;
		char vendor[ 64 ] = "", product[ 64 ] = "";
		FILE *file = fopen( "/sys/class/dmi/id/board_vendor", "r" );
		if (file != NULL)
		{
			if (fgets( vendor, sizeof( vendor ), file ) == NULL)
				vendor[0] = 0;
			fclose( file );
		}
		file = fopen( "/sys/class/dmi/id/product_name", "r" );
		if (file != NULL)
		{
			if (fgets( product, sizeof( product ), file ) == NULL)
				product[0] = 0;
			fclose( file );
		}
		known = strncmp( vendor, "Valve", 5 ) == 0 && (strncmp( product, "Jupiter", 7 ) == 0 || strncmp( product, "Galileo", 7 ) == 0);
	}
	return known == 1;
}

/// Whose glyphs a pad's hints draw: its family by SDL's type, the Deck's own controls by their IDs
GamepadGlyphSet glyphsFor( SDL_Gamepad *gamepad )
{
	const Uint16 VALVE = 0x28DE, DECK_CONTROLS = 0x1205;
	if (SDL_GetGamepadVendor( gamepad ) == VALVE && (SDL_GetGamepadProduct( gamepad ) == DECK_CONTROLS || isSteamDeck()))
		return GAMEPAD_GLYPHS_STEAM_DECK;		// the Deck's controls, straight or through Steam Input
	switch (SDL_GetGamepadType( gamepad ))
	{
		case SDL_GAMEPAD_TYPE_PS3:
		case SDL_GAMEPAD_TYPE_PS4:
		case SDL_GAMEPAD_TYPE_PS5:
			return GAMEPAD_GLYPHS_PLAYSTATION;
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
		case SDL_GAMEPAD_TYPE_GAMECUBE:
			return GAMEPAD_GLYPHS_NINTENDO;
		default:
			return GAMEPAD_GLYPHS_XBOX;		// Xbox pads, and any pad SDL labels A B X Y
	}
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
	pad.glyphs = glyphsFor( gamepad );
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

/** The game draws the cursor itself while a pad is in use: Mouse.ini's polygon images (W3DMouse's RM_POLYGON,
	* which hides the platform's).  The pad moves the game's pointer directly, never the platform's: a warp
	* of the platform's pointer does not come back on gamescope's Xwayland (the Steam Deck's Game Mode,
	* measured), and a console-style cursor is the game's own on every platform anyway.  A real mouse or
	* trackpad gives the platform's cursor back (SdlGamepad_noteHand). */
void drawCursor( Bool drawn )
{
	if (thePointer.drawn == drawn || TheMouse == NULL)
		return;
	thePointer.drawn = drawn;
	TheMouse->setRedrawMode( drawn ? Mouse::RM_POLYGON : Mouse::RM_WINDOWS );
	DEBUG_LOG(( "SdlGamepad: the cursor is drawn by %s, at %d,%d\n", drawn ? "the game (a pad is in use)" : "the platform",
		thePointer.shownX, thePointer.shownY ));
}

/// The pointer to a pixel: straight to SdlMouse, as the platform's own motion would arrive
void showPointer( Int x, Int y, UnsignedInt time )
{
	thePointer.shownX = x;
	thePointer.shownY = y;
	drawCursor( TRUE );
	if (SdlMouse::active() != NULL)
		SdlMouse::active()->addEvent( SdlMouse::EVENT_MOVE, x, y, SdlMouse::BUTTON_LEFT, 0, 0, time );
}

/// The pad takes the pointer and puts it on a pixel (a button's centre)
void movePointerTo( Int x, Int y, UnsignedInt time )
{
	thePointer.owned = TRUE;
	thePointer.x = (Real)x;
	thePointer.y = (Real)y;
	showPointer( x, y, time );
}

/// Into command-bar mode, on its top-left button; FALSE when no command bar is shown
Bool enterCommandBar( UnsignedInt time )
{
	ICoord2D centres[ MAX_COMMANDS_PER_SET ];
	const Int count = commandButtonCentres( centres, MAX_COMMANDS_PER_SET );
	Int first = -1;
	for (Int i = 0; i < count; ++i)
		if (first < 0 || centres[i].y < centres[first].y || (centres[i].y == centres[first].y && centres[i].x < centres[first].x))
			first = i;
	if (first < 0)
		return FALSE;
	movePointerTo( centres[first].x, centres[first].y, time );
	return TRUE;
}

/// Command-bar mode's D-pad: the pointer to the nearest button that way
void stepCommandBar( GamepadButtonType button, UnsignedInt time )
{
	ICoord2D centres[ MAX_COMMANDS_PER_SET ];
	const Int count = commandButtonCentres( centres, MAX_COMMANDS_PER_SET );
	if (count == 0)
	{
		theCommandBarMode = FALSE;		// the bar went away (a unit deselected): the D-pad is the D-pad again
		return;
	}
	const Int dx = button == GAMEPAD_BUTTON_DPAD_LEFT ? -1 : (button == GAMEPAD_BUTTON_DPAD_RIGHT ? 1 : 0);
	const Int dy = button == GAMEPAD_BUTTON_DPAD_UP ? -1 : (button == GAMEPAD_BUTTON_DPAD_DOWN ? 1 : 0);
	const Int next = SdlGamepad_pickNeighbour( centres, count, thePointer.shownX, thePointer.shownY, dx, dy );
	if (next >= 0)
		movePointerTo( centres[next].x, centres[next].y, time );
}

// GamepadFocus's hooks: the pointer, the left button and the keys, as this layer gives them to the world
void focusPointTo( Int x, Int y )
{
	movePointerTo( x, y, (UnsignedInt)SDL_GetTicks() );
}

void focusLeftButton( Bool down )
{
	if (down)
		mouseDown( SdlMouse::BUTTON_LEFT, (UnsignedInt)SDL_GetTicks() );
	else
		mouseUp( SdlMouse::BUTTON_LEFT, (UnsignedInt)SDL_GetTicks() );
}

void focusKey( UnsignedByte dik, Bool down )
{
	if (down)
		keyDown( dik );
	else
		keyUp( dik );
}

/// The pad was used: its buttons show in the hints, it takes the pointer, and a pointer parked in the
/// edge band goes to the centre
void padUsed( const Pad &pad, UnsignedInt time )
{
	GamepadHints::setShown( pad.glyphs );		// the last pad pressed sets the glyphs
	GamepadFocus::setPadDriving( TRUE );
	if (theLastUsed)
		return;
	theLastUsed = TRUE;
	drawCursor( TRUE );		// the game's own cursor from the first press, where the game's pointer is
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

/// Options > Controls > Controller: off, every pad is ignored (the tests have no GlobalData: on)
Bool padsWanted( void )
{
	return TheGlobalData == NULL || TheGlobalData->m_gamepadEnabled;
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
		const GamepadFocus::Hooks hooks = { focusPointTo, focusLeftButton, focusKey };
		GamepadFocus::setHooks( hooks );
		if (!theStarted)
		{
			// before the debug log is open, so on stderr as well
			fprintf( stderr, "generals: no gamepads: SDL's gamepad subsystem did not start: %s\n", SDL_GetError() );
			DEBUG_LOG(( "SdlGamepad: no gamepads: %s\n", SDL_GetError() ));
		}
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
	theCommandBarMode = FALSE;
	GamepadHints::setShown( GAMEPAD_GLYPHS_NONE );
	GamepadHints::setCommandBarMode( FALSE );
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

void SdlGamepad_noteMotion( Int /*x*/, Int /*y*/ )
{
	// the pad never moves the platform's pointer, so any motion the platform reports is a hand's
	thePointer.owned = FALSE;
	SdlGamepad_noteHand();
}

void SdlGamepad_noteHand( void )
{
	theLastUsed = FALSE;
	drawCursor( FALSE );		// the platform's cursor again
	GamepadFocus::setPadDriving( FALSE );
	GamepadHints::setShown( GAMEPAD_GLYPHS_NONE );		// the keys' letters again
}

Bool SdlGamepad_isLastUsed( void )
{
	return theLastUsed;
}

Bool SdlGamepad_inCommandBar( void )
{
	return theCommandBarMode;
}

Int SdlGamepad_pickNeighbour( const ICoord2D *centres, Int count, Int x, Int y, Int dx, Int dy )
{
	return GamepadFocus::pickNeighbour( centres, count, x, y, dx, dy );
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
			if (!padsWanted())
				return TRUE;		// the option is off: taken, and nothing done with it
			Pad *pad = findPad( event.gbutton.which );
			if (pad == NULL || event.gbutton.button >= GAMEPAD_BUTTON_COUNT)
				return TRUE;
			const GamepadButtonType button = (GamepadButtonType)event.gbutton.button;		// the same order (GamepadMap.h)
			if (event.gbutton.down)
				padUsed( *pad, milliseconds( event.gbutton.timestamp ) );
			if (event.gbutton.down)
				pressButton( *pad, button, milliseconds( event.gbutton.timestamp ) );
			else
				releaseButton( *pad, button, milliseconds( event.gbutton.timestamp ) );
			return TRUE;
		}

		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		{
			if (!padsWanted())
				return TRUE;
			Pad *pad = findPad( event.gaxis.which );
			if (pad != NULL && event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT)
			{
				const Real value = event.gaxis.value / 32767.0f;
				pad->axis[ event.gaxis.axis ] = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
				const Real deadZone = (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
					? TRIGGER_DEAD_ZONE : STICK_DEAD_ZONE;
				if (value > deadZone || value < -deadZone)
					padUsed( *pad, milliseconds( event.gaxis.timestamp ) );
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

	// the option turned off while a pad held something: let it all go, and the hints with it
	if (!padsWanted())
	{
		if (theLastUsed || theCommandBarMode)
		{
			SdlGamepad_releaseAll();
			theCommandBarMode = FALSE;
			SdlGamepad_noteHand();
		}
		GamepadHints::setCommandBarMode( FALSE );
		return;
	}

	// a menu has the pad: directions held repeat, the left stick is a D-pad, the triggers page; the pointer,
	// the camera and the wheel wait for the world (GamepadFocus.h)
	if (GamepadFocus::isActive())
	{
		static UnsignedInt stickNext = 0, triggerNext = 0;
		if (theNavAction >= 0 && nowMs >= theNavNext)
		{
			GamepadFocus::act( (GamepadFocus::Action)theNavAction );
			theNavNext = nowMs + NAV_REPEAT_MS;
		}
		Real sx = 0.0f, sy = 0.0f, lt = 0.0f, rt = 0.0f;
		for (size_t i = 0; i < thePads.size(); ++i)
		{
			const Real px = thePads[i].axis[ SDL_GAMEPAD_AXIS_LEFTX ], py = thePads[i].axis[ SDL_GAMEPAD_AXIS_LEFTY ];
			if (px * px + py * py > sx * sx + sy * sy)
			{
				sx = px;
				sy = py;
			}
			lt = thePads[i].axis[ SDL_GAMEPAD_AXIS_LEFT_TRIGGER ] > lt ? thePads[i].axis[ SDL_GAMEPAD_AXIS_LEFT_TRIGGER ] : lt;
			rt = thePads[i].axis[ SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ] > rt ? thePads[i].axis[ SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ] : rt;
			for (Int k = 0; k < 4; ++k)
				if (thePads[i].cameraKey[ k ])
				{
					thePads[i].cameraKey[ k ] = FALSE;
					keyUp( theCameraKeys[ k ] );
				}
		}
		// the stick: a direction past 0.6 on its main axis, kept until it falls under 0.4
		const Real STICK_ON = 0.6f, STICK_OFF = 0.4f;
		Int stick = -1;
		const Real ax = sx < 0 ? -sx : sx, ay = sy < 0 ? -sy : sy;
		if (theStickNav == GamepadFocus::NAV_LEFT && sx < -STICK_OFF) stick = GamepadFocus::NAV_LEFT;
		else if (theStickNav == GamepadFocus::NAV_RIGHT && sx > STICK_OFF) stick = GamepadFocus::NAV_RIGHT;
		else if (theStickNav == GamepadFocus::NAV_UP && sy < -STICK_OFF) stick = GamepadFocus::NAV_UP;
		else if (theStickNav == GamepadFocus::NAV_DOWN && sy > STICK_OFF) stick = GamepadFocus::NAV_DOWN;
		else if (ax >= ay && ax > STICK_ON) stick = sx < 0 ? GamepadFocus::NAV_LEFT : GamepadFocus::NAV_RIGHT;
		else if (ay > STICK_ON) stick = sy < 0 ? GamepadFocus::NAV_UP : GamepadFocus::NAV_DOWN;
		if (stick != theStickNav)
		{
			theStickNav = stick;
			if (stick >= 0)
			{
				GamepadFocus::setPadDriving( TRUE );
				GamepadFocus::act( (GamepadFocus::Action)stick );
				stickNext = nowMs + NAV_FIRST_REPEAT_MS;
			}
		}
		else if (stick >= 0 && nowMs >= stickNext)
		{
			GamepadFocus::act( (GamepadFocus::Action)stick );
			stickNext = nowMs + NAV_REPEAT_MS;
		}
		// the triggers: a page of a list, then again while held
		const Int trigger = rt > 0.5f ? GamepadFocus::PAGE_DOWN : (lt > 0.5f ? GamepadFocus::PAGE_UP : -1);
		if (trigger != theTriggerNav)
		{
			theTriggerNav = trigger;
			if (trigger >= 0)
			{
				GamepadFocus::act( (GamepadFocus::Action)trigger );
				triggerNext = nowMs + NAV_FIRST_REPEAT_MS;
			}
		}
		else if (trigger >= 0 && nowMs >= triggerNext)
		{
			GamepadFocus::act( (GamepadFocus::Action)trigger );
			triggerNext = nowMs + NAV_REPEAT_MS * 2;
		}
		for (size_t i = 0; i < thePads.size(); ++i)
			thePads[i].wheelCarry = 0.0f;
		theCommandBarMode = FALSE;
		GamepadHints::setCommandBarMode( FALSE );
		return;
	}
	theStickNav = theTriggerNav = -1;

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
		theCommandBarMode = FALSE;		// the stick takes the pointer off the bar
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
		Real speed = width / POINTER_CROSSING_SECONDS;		// pixels a second at full tilt
		if (GamepadAim::nearTarget( thePointer.shownX, thePointer.shownY, GamepadAim::radiusFor( height ) / 2 ))
			speed *= AIM_FRICTION;		// aim assist: slower over a unit or a building, so a flick stops on it
		thePointer.x += vx * speed * seconds;
		thePointer.y += vy * speed * seconds;
		// held to the screen: at an edge it edge-scrolls, as a mouse there does
		thePointer.x = thePointer.x < 0.0f ? 0.0f : (thePointer.x > width - 1 ? (Real)(width - 1) : thePointer.x);
		thePointer.y = thePointer.y < 0.0f ? 0.0f : (thePointer.y > height - 1 ? (Real)(height - 1) : thePointer.y);
		const Int x = (Int)thePointer.x, y = (Int)thePointer.y;
		if (x != thePointer.shownX || y != thePointer.shownY)
			showPointer( x, y, nowMs );
	}
	else if (theLastUsed && thePointer.owned && !theCommandBarMode && width > 0 && height > 0 && seconds > 0.0f)
	{
		// aim assist: the resting pointer eases onto the nearest unit or building the player can see
		ICoord2D target;
		if (GamepadAim::magnetTarget( thePointer.shownX, thePointer.shownY, GamepadAim::radiusFor( height ), target ))
		{
			GamepadAim::easeToward( thePointer.x, thePointer.y, target, seconds );
			const Int x = (Int)thePointer.x, y = (Int)thePointer.y;
			if (x != thePointer.shownX || y != thePointer.shownY)
				showPointer( x, y, nowMs );
		}
	}
	GamepadHints::setCommandBarMode( theCommandBarMode );		// before this frame's drawing reads it

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
