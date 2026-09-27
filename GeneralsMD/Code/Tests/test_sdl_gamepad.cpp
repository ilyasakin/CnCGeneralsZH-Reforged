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
/*
 * G1's test 1 (docs/mac-port/tasks/G1-gamepad.md): a gamepad produces exactly the input a mouse and
 * keyboard produce.  On SDL3's offscreen video driver, with SDL's virtual joystick as the pad.
 *
 * Each case drives the pad through SDL (SDL_SetJoystickVirtualButton/Axis, then SDL_PollEvent into
 * SdlInput_dispatch, as SdlGameEngine::serviceWindowsOS does), and separately pushes the SDL mouse and key
 * events a hand would make for the same thing.  Both come out of the engine's own devices - SdlMouse's
 * MouseIO, Keyboard::update's KeyboardIO with its modifier state - and must be equal, event for event.
 * Everything downstream of those (Mouse and Keyboard's MSG_RAW_* messages, the command maps, the
 * translators) is the same code for both, so equal here is equal there.  The time and the sequence number
 * are not compared: they are when, not what.
 *
 * The bindings are the shipped Data/INI/GamepadReforged.ini, loaded by the engine's INI code.  Its
 * Command bindings resolve through a command map: here a made-up one that binds each command the file
 * uses as the game's maps do (the game's own CommandMap.ini is in the retail archives).
 *
 * Cases: a click and a double click; a drag box; the right and middle buttons; a Command's key; a chord
 * with a held modifier (Ctrl + the D-pad makes a group); a With chord (the left shoulder's Shift let go
 * for the D-pad's group 5, then held again); one Ctrl held by two things goes down once; the triggers as
 * the wheel; the right stick as the arrow keys with hysteresis; a pad pulled out mid-press lets go of all
 * it held; and every binding in the shipped file parsed, each Command bound.
 *
 * What it cannot see: a real pad's feel, and Steam Input's virtual pad (the Deck, G1's step 1).
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "GameClient/GamepadMap.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "SdlDevice/GameClient/SdlGamepad.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"
#include "SdlDevice/GameClient/SdlMouse.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {

SDL_Window *theWindow = NULL;
SDL_Joystick *thePad = NULL;

// A command map binding each command GamepadReforged.ini names, with the game's keys for them
const char *const TEST_COMMAND_MAP =
	"CommandMap STOP\n  Key = KEY_S\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap TOGGLE_ATTACKMOVE\n  Key = KEY_F\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_MATCHING_UNITS\n  Key = KEY_D\n  Transition = DOWN\n  Modifiers = CTRL\n  UseableIn = GAME\nEnd\n"
	"CommandMap OPTIONS\n  Key = KEY_ESC\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_NEXT_IDLE_WORKER\n  Key = KEY_I\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_ALL_MILITARY\n  Key = KEY_Q\n  Transition = DOWN\n  Modifiers = CTRL\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_ALL\n  Key = KEY_A\n  Transition = DOWN\n  Modifiers = CTRL\n  UseableIn = GAME\nEnd\n"
	"CommandMap SCATTER\n  Key = KEY_X\n  Transition = DOWN\n  Modifiers = SHIFT_CTRL\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM1\n  Key = KEY_1\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM2\n  Key = KEY_2\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM3\n  Key = KEY_3\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM4\n  Key = KEY_4\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM5\n  Key = KEY_5\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM6\n  Key = KEY_6\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM7\n  Key = KEY_7\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap SELECT_TEAM8\n  Key = KEY_8\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n";

bool writeFile( const std::string &path, const std::string &text )
{
	FILE *file = fopen( path.c_str(), "wb" );
	if (file == NULL)
		return false;
	const bool ok = fwrite( text.data(), 1, text.size(), file ) == text.size();
	fclose( file );
	return ok;
}

bool readFile( const char *path, std::string &text )
{
	FILE *file = fopen( path, "rb" );
	if (file == NULL)
		return false;
	char buffer[4096];
	size_t got;
	text.clear();
	while ((got = fread( buffer, 1, sizeof( buffer ), file )) > 0)
		text.append( buffer, got );
	fclose( file );
	return true;
}

/// The maps, once: the engine's INI code reading a scratch folder's Data\INI, as the game reads its own
bool loadMaps( void )
{
	static bool loaded = false, ok = false;
	if (loaded)
		return ok;
	loaded = true;
	std::string bindings;
	if (!readFile( GAMEPAD_INI, bindings ))
	{
		printf( "  cannot read %s\n", GAMEPAD_INI );
		return false;
	}
	char folder[] = "/tmp/test_sdl_gamepad.XXXXXX";
	if (mkdtemp( folder ) == NULL)
		return false;
	const std::string root = folder;
	if (system( ("mkdir -p '" + root + "/Data/INI'").c_str() ) != 0
			|| !writeFile( root + "/Data/INI/TestCommandMap.ini", TEST_COMMAND_MAP )
			|| !writeFile( root + "/Data/INI/GamepadReforged.ini", bindings )
			|| chdir( root.c_str() ) != 0)
		return false;

	TheWritableGlobalData = NEW GlobalData;
	TheWritableGlobalData->m_inputScheme = INPUT_SCHEME_MODERN;
	TheWritableGlobalData->m_wasdCamera = FALSE;
	TheLocalFileSystem = NEW PosixLocalFileSystem;
	TheFileSystem = NEW FileSystem;
	TheMetaMap = NEW MetaMap;
	TheGamepadMap = NEW GamepadMap;
	try
	{
		INI ini;
		ini.load( "Data\\INI\\TestCommandMap.ini", INI_LOAD_OVERWRITE, NULL );
		ini.load( "Data\\INI\\GamepadReforged.ini", INI_LOAD_OVERWRITE, NULL );
		ok = true;
	}
	catch (...)
	{
		printf( "  the INI files did not load\n" );
	}
	system( ("rm -rf '" + root + "'").c_str() );
	return ok;
}

SdlKeyboard *theKeyboard( void )
{
	static SdlKeyboard *keyboard = NULL;
	if (keyboard == NULL)
	{
		keyboard = new SdlKeyboard;
		keyboard->init();
	}
	return keyboard;
}

class OpenMouse : public SdlMouse
{
public:
	using SdlMouse::getMouseEvent;
};

OpenMouse *theMouse( void )
{
	static OpenMouse *mouse = NULL;
	if (mouse == NULL)
		mouse = new OpenMouse;
	return mouse;
}

// what serviceWindowsOS does: every event to dispatch
void pump( void )
{
	SDL_Event event;
	while (SDL_PollEvent( &event ))
		SdlInput_dispatch( event );
}

/// Video, the gamepads, the maps, the devices and the virtual pad, once
bool start( void )
{
	static bool started = false, ok = false;
	if (started)
		return ok;
	started = true;
	initMemoryManager();
	SDL_SetHint( SDL_HINT_VIDEO_DRIVER, "offscreen" );
	ok = SDL_Init( SDL_INIT_VIDEO );
	if (ok)
		theWindow = SDL_CreateWindow( "test_sdl_gamepad", 800, 600, 0 );
	ok = ok && theWindow != NULL && SdlGamepad_start();
	if (!ok)
	{
		printf( "  SDL: %s\n", SDL_GetError() );
		return false;
	}
	ok = loadMaps();
	theKeyboard();
	theMouse();

	SDL_VirtualJoystickDesc desc;
	SDL_INIT_INTERFACE( &desc );
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;		// button i is SDL_GamepadButton i
	desc.name = "test_sdl_gamepad pad";
	const SDL_JoystickID id = SDL_AttachVirtualJoystick( &desc );
	thePad = id != 0 ? SDL_OpenJoystick( id ) : NULL;
	if (thePad == NULL)
	{
		printf( "  no virtual pad: %s\n", SDL_GetError() );
		return ok = false;
	}
	// triggers at rest: the virtual joystick's axis minimum is the gamepad's 0
	SDL_SetJoystickVirtualAxis( thePad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	SDL_SetJoystickVirtualAxis( thePad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	pump();
	return ok;
}

void padButton( SDL_GamepadButton button, bool down )
{
	SDL_SetJoystickVirtualButton( thePad, button, down );
	pump();
}

void padAxis( SDL_GamepadAxis axis, Sint16 value )
{
	SDL_SetJoystickVirtualAxis( thePad, axis, value );
	pump();
}

void pushKey( SDL_Scancode scancode, bool down )
{
	// a hand's key: an SDL key event through SdlInput_dispatch, as a keyboard's arrives
	SDL_Event event;
	SDL_zero( event );
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = SDL_GetWindowID( theWindow );
	event.key.scancode = scancode;
	event.key.down = down;
	SDL_PushEvent( &event );
	pump();
}

void pushButton( Uint8 button, bool down, Uint8 clicks, float x, float y )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.windowID = SDL_GetWindowID( theWindow );
	event.button.button = button;
	event.button.down = down;
	event.button.clicks = clicks;
	event.button.x = x;
	event.button.y = y;
	SDL_PushEvent( &event );
	pump();
}

void pushMotion( float x, float y )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.windowID = SDL_GetWindowID( theWindow );
	event.motion.x = x;
	event.motion.y = y;
	SDL_PushEvent( &event );
	pump();
}

void pushWheel( float y, float x, float at )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.windowID = SDL_GetWindowID( theWindow );
	event.wheel.y = y;
	event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
	event.wheel.mouse_x = x;
	event.wheel.mouse_y = at;
	SDL_PushEvent( &event );
	pump();
}

/// What the devices made since the last call, as text: the mouse's MouseIO and one frame of keys
struct Output
{
	std::vector<std::string> events;
};

std::string describe( const MouseIO &io )
{
	char line[160];
	snprintf( line, sizeof( line ), "mouse %d,%d L%d/%u M%d/%u R%d/%u wheel %d", io.pos.x, io.pos.y,
		(int)io.leftState, io.leftFrame, (int)io.middleState, io.middleFrame, (int)io.rightState, io.rightFrame, io.wheelPos );
	return line;
}

std::string describe( const KeyboardIO &io )
{
	char line[64];
	snprintf( line, sizeof( line ), "key %02x state %04x", io.key, io.state );
	return line;
}

/// One client frame: the mouse's events, then the keyboard's update, as GameClient::update takes them
void frame( Output &out )
{
	MouseIO io;
	while (theMouse()->getMouseEvent( &io, FALSE ) == MOUSE_OK)
		out.events.push_back( describe( io ) );
	theKeyboard()->update();
	for (KeyboardIO *key = theKeyboard()->getFirstKey(); key->key != KEY_NONE; ++key)
		out.events.push_back( describe( *key ) );
}

void clear( void )
{
	Output ignored;
	frame( ignored );
}

bool same( const Output &pad, const Output &hand )
{
	bool equal = pad.events == hand.events;
	if (!equal || pad.events.empty())
	{
		printf( "    pad (%d):\n", (int)pad.events.size() );
		for (size_t i = 0; i < pad.events.size(); ++i)
			printf( "      %s\n", pad.events[i].c_str() );
		printf( "    hand (%d):\n", (int)hand.events.size() );
		for (size_t i = 0; i < hand.events.size(); ++i)
			printf( "      %s\n", hand.events[i].c_str() );
	}
	return equal && !pad.events.empty();
}

}  // namespace

TEST(every_shipped_binding_parses_and_each_command_is_bound)
{
	CHECK( start() );
	CHECK( TheGamepadMap != NULL );
	CHECK_EQ( TheGamepadMap->getCount(), 21 );		// the mouse 3, modifiers 2, orders 4, groups 8, back buttons 4
	for (Int i = 0; i < TheGamepadMap->getCount(); ++i)
	{
		const GamepadBinding &binding = TheGamepadMap->get( i );
		CHECK( binding.m_action != GAMEPAD_ACTION_NONE );
		if (binding.m_action == GAMEPAD_ACTION_COMMAND)
		{
			MappableKeyType key;
			MappableKeyModState modState;
			CHECK( GamepadMap::keyForCommand( binding.m_command, key, modState ) );
		}
	}
	const GamepadBinding *south = TheGamepadMap->find( GAMEPAD_BUTTON_SOUTH, GAMEPAD_BUTTON_NONE );
	CHECK( south != NULL && south->m_action == GAMEPAD_ACTION_MOUSE_LEFT );
	const GamepadBinding *chord = TheGamepadMap->find( GAMEPAD_BUTTON_DPAD_UP, GAMEPAD_BUTTON_LEFT_SHOULDER );
	CHECK( chord != NULL && chord->m_action == GAMEPAD_ACTION_COMMAND );
	CHECK_EQ( SdlGamepad_count(), 1 );
}

TEST(south_clicks_and_double_clicks_where_the_pointer_is_as_the_left_button)
{
	CHECK( start() );
	pushMotion( 100, 200 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, true );
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, false );
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, true );
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, false );
	frame( pad );
	pushButton( SDL_BUTTON_LEFT, true, 1, 100, 200 );
	pushButton( SDL_BUTTON_LEFT, false, 1, 100, 200 );
	pushButton( SDL_BUTTON_LEFT, true, 2, 100, 200 );
	pushButton( SDL_BUTTON_LEFT, false, 2, 100, 200 );
	frame( hand );
	CHECK( same( pad, hand ) );
	// armed: the second press is a double click, not two presses
	CHECK( pad.events.size() == 4 && pad.events[2].find( "L" ) != std::string::npos
		&& pad.events[2] != pad.events[0] );
}

TEST(south_held_across_a_move_is_a_drag_box)
{
	CHECK( start() );
	pushMotion( 300, 300 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, true );
	pushMotion( 380, 340 );
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, false );
	frame( pad );
	pushMotion( 300, 300 );
	clear();
	pushButton( SDL_BUTTON_LEFT, true, 1, 300, 300 );
	pushMotion( 380, 340 );
	pushButton( SDL_BUTTON_LEFT, false, 1, 380, 340 );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(east_is_the_right_button_and_the_right_stick_click_the_middle)
{
	CHECK( start() );
	pushMotion( 500, 100 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_EAST, true );
	padButton( SDL_GAMEPAD_BUTTON_EAST, false );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_STICK, true );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_STICK, false );
	frame( pad );
	pushButton( SDL_BUTTON_RIGHT, true, 1, 500, 100 );
	pushButton( SDL_BUTTON_RIGHT, false, 1, 500, 100 );
	pushButton( SDL_BUTTON_MIDDLE, true, 1, 500, 100 );
	pushButton( SDL_BUTTON_MIDDLE, false, 1, 500, 100 );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(a_command_presses_the_key_the_players_map_binds_it_to)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_WEST, true );		// STOP: S
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_WEST, false );
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_BACK, true );		// SELECT_MATCHING_UNITS: Ctrl+D
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_BACK, false );
	frame( pad );
	pushKey( SDL_SCANCODE_S, true );
	frame( hand );
	pushKey( SDL_SCANCODE_S, false );
	frame( hand );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_D, true );
	frame( hand );
	pushKey( SDL_SCANCODE_D, false );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(ctrl_held_on_the_right_shoulder_makes_the_dpads_group)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_DPAD_UP, true );			// group 1's key with Ctrl held: make group 1
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_DPAD_UP, false );
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );
	frame( pad );
	pushKey( SDL_SCANCODE_LCTRL, true );
	frame( hand );
	pushKey( SDL_SCANCODE_1, true );
	frame( hand );
	pushKey( SDL_SCANCODE_1, false );
	frame( hand );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(a_with_chord_lets_the_shoulders_shift_go_for_its_length)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true );		// Shift down
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_DPAD_UP, true );					// group 5: Shift up, 5 down
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_DPAD_UP, false );					// 5 up, Shift down again
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false );		// Shift up
	frame( pad );
	pushKey( SDL_SCANCODE_LSHIFT, true );
	frame( hand );
	pushKey( SDL_SCANCODE_LSHIFT, false );
	pushKey( SDL_SCANCODE_5, true );
	frame( hand );
	pushKey( SDL_SCANCODE_5, false );
	pushKey( SDL_SCANCODE_LSHIFT, true );
	frame( hand );
	pushKey( SDL_SCANCODE_LSHIFT, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(one_ctrl_held_by_two_bindings_goes_down_once)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );		// Ctrl
	padButton( SDL_GAMEPAD_BUTTON_BACK, true );							// Ctrl+D: Ctrl already down
	padButton( SDL_GAMEPAD_BUTTON_BACK, false );						// D up; Ctrl stays for the shoulder
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );
	frame( pad );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_D, true );
	pushKey( SDL_SCANCODE_D, false );
	frame( hand );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(the_triggers_are_the_wheel_in_proportion_to_the_pull)
{
	CHECK( start() );
	pushMotion( 250, 150 );
	clear();
	Output pad, hand;
	SdlGamepad_update( 10000 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX );		// all the way: 6 notches a second
	SdlGamepad_update( 10100 );																					// 0.1 s: 72 of 120
	padAxis( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX );
	SdlGamepad_update( 10150 );																					// out, 0.05 s: -36
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	SdlGamepad_update( 10200 );																					// released: nothing
	frame( pad );
	pushWheel( 0.6f, 250, 150 );
	pushWheel( -0.3f, 250, 150 );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(the_right_stick_holds_the_arrow_keys_with_some_hysteresis)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padAxis( SDL_GAMEPAD_AXIS_RIGHTY, -20000 );		// up, past 0.5
	SdlGamepad_update( 20000 );
	frame( pad );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTY, -14000 );		// eased to 0.43: still held
	SdlGamepad_update( 20016 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTX, 30000 );		// and right
	SdlGamepad_update( 20032 );
	frame( pad );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTY, 0 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTX, 0 );
	SdlGamepad_update( 20048 );
	frame( pad );
	pushKey( SDL_SCANCODE_UP, true );
	frame( hand );
	pushKey( SDL_SCANCODE_RIGHT, true );
	frame( hand );
	pushKey( SDL_SCANCODE_UP, false );
	pushKey( SDL_SCANCODE_RIGHT, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

namespace {

/// Frames until the pointer's move shows: a warp coming back, or the direct path once warps are given up
std::vector<std::string> settle( UnsignedInt &now )
{
	Output out;
	for (Int i = 0; i < 8; ++i)
	{
		now += 16;
		SdlGamepad_update( now );
		pump();
		frame( out );
	}
	return out.events;
}

bool movedTo( const std::vector<std::string> &events, Int x, Int y )
{
	char want[64];
	snprintf( want, sizeof( want ), "mouse %d,%d L0/0 M0/0 R0/0 wheel 0", x, y );
	int found = 0;
	for (size_t i = 0; i < events.size(); ++i)
		found += events[i] == want ? 1 : 0;
	if (found != 1)
	{
		printf( "    wanted one \"%s\" in:\n", want );
		for (size_t i = 0; i < events.size(); ++i)
			printf( "      %s\n", events[i].c_str() );
	}
	return found == 1;
}

}  // namespace

TEST(the_left_stick_moves_the_pointer_and_the_move_arrives_as_the_mouses)
{
	CHECK( start() );
	pushMotion( 400, 300 );
	clear();
	UnsignedInt now = 30000;
	SdlGamepad_update( now );
	padAxis( SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX );		// full tilt right
	now += 100;
	SdlGamepad_update( now );		// 0.1 s at 800 / 1.2 s = 66.7 pixels
	padAxis( SDL_GAMEPAD_AXIS_LEFTX, 0 );
	const std::vector<std::string> events = settle( now );
	CHECK( movedTo( events, 466, 300 ) );
	Int x, y;
	CHECK( SdlGamepad_pointer( x, y ) && x == 466 && y == 300 );
	CHECK( SdlGamepad_isLastUsed() );
	// a click now lands where the pointer was put, as the mouse's would
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, true );
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, false );
	frame( pad );
	pushButton( SDL_BUTTON_LEFT, true, 1, 466, 300 );
	pushButton( SDL_BUTTON_LEFT, false, 1, 466, 300 );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(a_hand_on_the_mouse_takes_the_pointer_back)
{
	CHECK( start() );
	pushMotion( 123, 234 );
	clear();
	Int x, y;
	CHECK( !SdlGamepad_pointer( x, y ) );
	CHECK( !SdlGamepad_isLastUsed() );
}

TEST(a_pointer_parked_in_the_edge_band_goes_to_the_centre_when_the_pad_is_first_used)
{
	CHECK( start() );
	pushMotion( 0, 0 );		// where gamescope left it: in the band, so the camera would edge-scroll
	clear();
	UnsignedInt now = 40000;
	SdlGamepad_update( now );
	padAxis( SDL_GAMEPAD_AXIS_LEFTY, 10000 );		// a nudge past the dead zone: the pad is in use
	padAxis( SDL_GAMEPAD_AXIS_LEFTY, 0 );
	CHECK( movedTo( settle( now ), 400, 300 ) );
	// armed: out of the band, first use leaves the pointer where it is
	pushMotion( 200, 100 );
	clear();
	padAxis( SDL_GAMEPAD_AXIS_LEFTY, 10000 );
	padAxis( SDL_GAMEPAD_AXIS_LEFTY, 0 );
	const std::vector<std::string> events = settle( now );
	CHECK( events.empty() );
	Int x, y;
	CHECK( SdlGamepad_pointer( x, y ) && x == 200 && y == 100 );
}

TEST(full_tilt_holds_the_pointer_at_the_screens_edge_where_it_edge_scrolls)
{
	CHECK( start() );
	pushMotion( 400, 300 );
	clear();
	UnsignedInt now = 50000;
	SdlGamepad_update( now );
	padAxis( SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MIN );		// full tilt left for a second
	for (Int i = 0; i < 10; ++i)
	{
		now += 100;
		SdlGamepad_update( now );
		pump();
	}
	padAxis( SDL_GAMEPAD_AXIS_LEFTX, 0 );
	settle( now );
	Int x, y;
	CHECK( SdlGamepad_pointer( x, y ) && x == 0 && y == 300 );
}

TEST(a_pad_pulled_out_mid_press_lets_go_of_all_it_held)
{
	CHECK( start() );
	pushMotion( 600, 500 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, true );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );
	frame( pad );
	const SDL_JoystickID id = SDL_GetJoystickID( thePad );
	SDL_CloseJoystick( thePad );
	SDL_DetachVirtualJoystick( id );
	thePad = NULL;
	pump();
	frame( pad );
	CHECK_EQ( SdlGamepad_count(), 0 );
	pushButton( SDL_BUTTON_LEFT, true, 1, 600, 500 );
	pushKey( SDL_SCANCODE_LCTRL, true );
	frame( hand );
	pushButton( SDL_BUTTON_LEFT, false, 1, 600, 500 );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}
