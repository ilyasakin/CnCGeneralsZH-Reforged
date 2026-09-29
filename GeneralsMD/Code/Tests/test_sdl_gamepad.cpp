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
 * Cases: a click and a double click; a drag box; X as the order button and B taking back; a Command's key;
 * a chord with a held modifier (Ctrl + the D-pad makes a group); a With chord (the left trigger's Shift let
 * go for the D-pad's group 5, then held again); one Ctrl held by two things goes down once; LB's tap (the
 * idle worker) and its layer (the right stick zooms and turns, and spends the tap); both shoulders stop,
 * LB+Y the army, RB+B scatter; the right stick as the arrow keys with hysteresis; confirm and cancel on a
 * Nintendo and a PlayStation pad and with the swap option; a pad pulled out mid-press lets go of all it
 * held; and every binding in the shipped file parsed, each Command bound.
 *
 * What it cannot see: a real pad's feel, and Steam Input's virtual pad (the Deck, G1's step 1).
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "GameClient/GamepadAim.h"
#include "GameClient/GamepadCycle.h"
#include "GameClient/GamepadFocus.h"
#include "GameClient/GamepadHints.h"
#include "GameClient/GamepadMap.h"
#include "GameClient/GamepadRadial.h"
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
	"CommandMap SELECT_TEAM8\n  Key = KEY_8\n  Transition = UP\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap VIEW_LAST_RADAR_EVENT\n  Key = KEY_SPACE\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap VIEW_COMMAND_CENTER\n  Key = KEY_H\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap BEGIN_CAMERA_ROTATE_LEFT\n  Key = KEY_KP4\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	"CommandMap BEGIN_CAMERA_ROTATE_RIGHT\n  Key = KEY_KP6\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n"
	// the command grid's place S after the shipped map's stop on S, as the game loads the maps: a record read later
	// goes to the front of the list (MetaMap::getMetaMapRec), where the translator finds it first, so S is the place's
	"CommandMap COMMAND_SLOT08\n  Key = KEY_S\n  Transition = DOWN\n  Modifiers = NONE\n  UseableIn = GAME\nEnd\n";

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
	CHECK_EQ( TheGamepadMap->getCount(), 30 );		// faces 4, triggers 2, shoulders 2, their chords 4, View and its base chords 3, Start and the sticks 3, groups 8, back buttons 4
	for (Int i = 0; i < TheGamepadMap->getCount(); ++i)
	{
		const GamepadBinding &binding = TheGamepadMap->get( i );
		CHECK( binding.m_action != GAMEPAD_ACTION_NONE );
		if (binding.m_action == GAMEPAD_ACTION_COMMAND && binding.m_command != GameMessage::MSG_META_STOP)
		{
			MappableKeyType key;
			MappableKeyModState modState;
			CHECK( GamepadMap::keyForCommand( binding.m_command, key, modState ) );
		}
	}
	const GamepadBinding *south = TheGamepadMap->find( GAMEPAD_BUTTON_SOUTH, GAMEPAD_BUTTON_NONE );
	CHECK( south != NULL && south->m_action == GAMEPAD_ACTION_MOUSE_LEFT );
	const GamepadBinding *chord = TheGamepadMap->find( GAMEPAD_BUTTON_DPAD_UP, GAMEPAD_BUTTON_LEFT_TRIGGER );
	CHECK( chord != NULL && chord->m_action == GAMEPAD_ACTION_COMMAND );
	CHECK_EQ( (Int)TheGamepadMap->buttonFor( GAMEPAD_ACTION_COMMAND_BAR ), (Int)GAMEPAD_BUTTON_RIGHT_TRIGGER );
	CHECK_EQ( (Int)TheGamepadMap->buttonFor( GAMEPAD_ACTION_ORDER ), (Int)GAMEPAD_BUTTON_WEST );
	CHECK_EQ( (Int)TheGamepadMap->buttonFor( GAMEPAD_ACTION_CANCEL ), (Int)GAMEPAD_BUTTON_EAST );
	CHECK_EQ( SdlGamepad_count(), 1 );
}

TEST(a_key_an_earlier_record_takes_is_not_the_commands_key)
{
	CHECK( start() );
	MappableKeyType key = MK_NONE;
	MappableKeyModState modState = NONE;
	// S is the grid place's first, so it is not stop's: the pad sends stop's message itself
	CHECK( !GamepadMap::keyForCommand( GameMessage::MSG_META_STOP, key, modState ) );
	// F is attack move's alone
	CHECK( GamepadMap::keyForCommand( GameMessage::MSG_META_TOGGLE_ATTACKMOVE, key, modState ) );
	CHECK_EQ( (Int)key, (Int)KEY_F );
	CHECK_EQ( (Int)modState, (Int)NONE );
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

TEST(west_is_the_order_button_east_takes_back_and_the_right_stick_clicks_the_middle)
{
	CHECK( start() );
	pushMotion( 500, 100 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_EAST, true );		// Cancel, outside a match: nothing to take back, and no click
	padButton( SDL_GAMEPAD_BUTTON_EAST, false );
	padButton( SDL_GAMEPAD_BUTTON_WEST, true );		// Order: Modern's order button, the right one
	padButton( SDL_GAMEPAD_BUTTON_WEST, false );
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
	padButton( SDL_GAMEPAD_BUTTON_NORTH, true );		// TOGGLE_ATTACKMOVE: F
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_NORTH, false );
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_BACK, true );		// VIEW_LAST_RADAR_EVENT: Space, on the release (OnRelease)
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_BACK, false );
	frame( pad );
	SdlGamepad_update( 20000 );									// ...let go on the next update, a tap over two frames
	frame( pad );
	pushKey( SDL_SCANCODE_F, true );
	frame( hand );
	pushKey( SDL_SCANCODE_F, false );
	frame( hand );
	pushKey( SDL_SCANCODE_SPACE, true );
	frame( hand );
	pushKey( SDL_SCANCODE_SPACE, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(view_held_with_a_shoulder_is_the_base_chord_and_its_own_command_does_not_follow)
{
	CHECK( start() );
	clear();
	Output pad;
	padButton( SDL_GAMEPAD_BUTTON_BACK, true );
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );		// the next production building: none outside a match
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );
	padButton( SDL_GAMEPAD_BUTTON_BACK, false );
	SdlGamepad_update( 21000 );
	frame( pad );
	CHECK( pad.events.empty() );		// no Ctrl from the shoulder, and no Space from View's release
	const GamepadBinding *next = TheGamepadMap->find( GAMEPAD_BUTTON_RIGHT_SHOULDER, GAMEPAD_BUTTON_BACK );
	const GamepadBinding *previous = TheGamepadMap->find( GAMEPAD_BUTTON_LEFT_SHOULDER, GAMEPAD_BUTTON_BACK );
	CHECK( next != NULL && next->m_action == GAMEPAD_ACTION_STRUCTURES && next->m_step == 1 );
	CHECK( previous != NULL && previous->m_action == GAMEPAD_ACTION_STRUCTURES && previous->m_step == -1 );
}

TEST(the_base_chord_steps_round_the_buildings_both_ways)
{
	CHECK_EQ( GamepadCycle::stepFrom( -1, 3, 1 ), 0 );		// none selected: the first...
	CHECK_EQ( GamepadCycle::stepFrom( -1, 3, -1 ), 2 );		// ...or the last
	CHECK_EQ( GamepadCycle::stepFrom( 0, 3, 1 ), 1 );
	CHECK_EQ( GamepadCycle::stepFrom( 2, 3, 1 ), 0 );			// round from the last to the first
	CHECK_EQ( GamepadCycle::stepFrom( 0, 3, -1 ), 2 );		// and back from the first to the last
	CHECK_EQ( GamepadCycle::stepFrom( 0, 1, 1 ), 0 );			// one building: itself
	CHECK_EQ( GamepadCycle::stepFrom( 0, 0, 1 ), -1 );		// none
	CHECK_EQ( GamepadCycle::stepFrom( 5, 3, 1 ), 0 );			// a stale index starts over
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

TEST(a_with_chord_lets_the_left_triggers_shift_go_for_its_length)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX );	// pulled past half: Shift down
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_DPAD_UP, true );					// group 5: Shift up, 5 down
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_DPAD_UP, false );					// 5 up, Shift down again
	frame( pad );
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 12000 );				// eased to 0.37: still held
	frame( pad );
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );	// let go: Shift up
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
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, true );		// SELECT_ALL, Ctrl+A: Ctrl already down
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, false );		// A up; Ctrl stays for the shoulder
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );
	frame( pad );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_A, true );
	pushKey( SDL_SCANCODE_A, false );
	frame( hand );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(the_left_shoulder_held_makes_the_right_stick_zoom_and_turn_and_spends_its_tap)
{
	CHECK( start() );
	pushMotion( 250, 150 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true );		// the camera layer
	SdlGamepad_update( 10000 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTY, SDL_JOYSTICK_AXIS_MIN );	// up all the way: 6 notches a second in
	SdlGamepad_update( 10100 );																	// 0.1 s: 72 of 120
	padAxis( SDL_GAMEPAD_AXIS_RIGHTY, SDL_JOYSTICK_AXIS_MAX );	// down: out
	SdlGamepad_update( 10150 );																	// 0.05 s: -36
	padAxis( SDL_GAMEPAD_AXIS_RIGHTY, 0 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTX, 30000 );									// right: the turn right key, no arrow key
	SdlGamepad_update( 10166 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHTX, 0 );
	SdlGamepad_update( 10182 );
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false );		// the layer was used: no idle worker
	SdlGamepad_update( 10200 );
	SdlGamepad_update( 10216 );
	frame( pad );
	pushWheel( 0.6f, 250, 150 );
	pushWheel( -0.3f, 250, 150 );
	pushKey( SDL_SCANCODE_KP_6, true );
	pushKey( SDL_SCANCODE_KP_6, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(a_tap_of_the_left_shoulder_is_the_next_idle_worker_and_the_triggers_are_no_wheel)
{
	CHECK( start() );
	pushMotion( 260, 160 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true );
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false );		// I, on the release...
	frame( pad );
	SdlGamepad_update( 11000 );															// ...let go on the next update
	frame( pad );
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX );	// Shift, and no zoom however long it is held
	SdlGamepad_update( 11100 );
	padAxis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	SdlGamepad_update( 11200 );
	frame( pad );
	pushKey( SDL_SCANCODE_I, true );
	frame( hand );
	pushKey( SDL_SCANCODE_I, false );
	frame( hand );
	pushKey( SDL_SCANCODE_LSHIFT, true );
	pushKey( SDL_SCANCODE_LSHIFT, false );
	frame( hand );
	CHECK( same( pad, hand ) );
}

TEST(both_shoulders_stop_and_the_left_one_with_y_is_the_whole_army_and_the_right_one_with_b_scatters)
{
	CHECK( start() );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );		// STOP: no key (the grid's S), no Ctrl of its own
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );
	padButton( SDL_GAMEPAD_BUTTON_NORTH, true );						// SELECT_ALL: Ctrl+A
	padButton( SDL_GAMEPAD_BUTTON_NORTH, false );
	padButton( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false );		// chords were made: no idle worker
	SdlGamepad_update( 12000 );
	frame( pad );
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );		// Ctrl...
	padButton( SDL_GAMEPAD_BUTTON_EAST, true );							// ...let go for SCATTER, Shift+Ctrl+X
	padButton( SDL_GAMEPAD_BUTTON_EAST, false );						// ...and held again
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );
	frame( pad );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_A, true );
	pushKey( SDL_SCANCODE_A, false );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_LCTRL, false );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_LSHIFT, true );
	pushKey( SDL_SCANCODE_X, true );
	pushKey( SDL_SCANCODE_X, false );
	pushKey( SDL_SCANCODE_LSHIFT, false );
	pushKey( SDL_SCANCODE_LCTRL, false );
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_LCTRL, false );
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

/// Frames until the pointer's move shows (it goes straight to SdlMouse; a few frames prove no more comes)
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

TEST(command_bar_mode_steps_to_the_nearest_button_that_way)
{
	// the command bar's grid, 7 by 2, 50 pixels apart, and a gap where a slot is empty
	ICoord2D centres[ 13 ];
	Int count = 0;
	for (Int row = 0; row < 2; ++row)
		for (Int column = 0; column < 7; ++column)
			if (!(row == 1 && column == 3))
			{
				centres[ count ].x = 100 + column * 50;
				centres[ count ].y = 500 + row * 50;
				++count;
			}
	CHECK_EQ( count, 13 );
	CHECK_EQ( SdlGamepad_pickNeighbour( centres, count, 100, 500, 1, 0 ), 1 );		// right: the next column
	CHECK_EQ( SdlGamepad_pickNeighbour( centres, count, 100, 500, 0, 1 ), 7 );		// down: the row below
	CHECK_EQ( SdlGamepad_pickNeighbour( centres, count, 100, 500, -1, 0 ), -1 );	// nothing left of the first
	CHECK_EQ( SdlGamepad_pickNeighbour( centres, count, 250, 500, 0, 1 ), 9 );		// over the gap: the nearest below
	CHECK_EQ( SdlGamepad_pickNeighbour( centres, count, 300, 550, -1, 0 ), 9 );		// left across the gap on its own row
}

namespace {
GamepadFocus::Box box( Int left, Int top, Int width, Int height )
{
	const GamepadFocus::Box b = { left, top, left + width - 1, top + height - 1 };
	return b;
}
Int centreX( const GamepadFocus::Box &b ) { return (b.left + b.right) / 2; }
Int centreY( const GamepadFocus::Box &b ) { return (b.top + b.bottom) / 2; }
}  // namespace

TEST(the_dpad_takes_the_aligned_widget_before_a_nearer_diagonal_one_and_nothing_outside_the_cone)
{
	// a wide button at the top, a narrow one below its right end, and a far one straight below its middle
	const GamepadFocus::Box boxes[] = { box( 100, 100, 200, 30 ), box( 280, 150, 60, 30 ), box( 150, 400, 100, 30 ),
		box( 700, 120, 60, 30 ) };
	// down from the top button: the narrow one under its right end is straight below it (it overlaps the button's
	// span) and nearest, whatever the remembered column
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[0], 0, 1, centreX( boxes[0] ) ), 1 );
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[0], 0, 1, 310 ), 1 );
	// up from the far bottom one: the wide top button over it, not the narrow one off to its right, nearer
	GamepadFocus::Box upward[] = { boxes[0], box( 300, 250, 60, 30 ), boxes[2] };
	CHECK_EQ( GamepadFocus::pickNeighbourBox( upward, 3, upward[2], 0, -1, centreX( upward[2] ) ), 0 );
	// right from the narrow one (row 165): the far button at 700 is inside its row's reach, the cone
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[1], 1, 0, centreY( boxes[1] ) ), 3 );
	// up from the far bottom one (column 200): the top button; nothing to its left or right at all
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[2], 0, -1, centreX( boxes[2] ) ), 0 );
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[2], -1, 0, centreY( boxes[2] ) ), -1 );
	// right from the bottom one: the narrow button is above the 45 degree cone, the far one (28 degrees up) inside it
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[2], 1, 0, centreY( boxes[2] ) ), 3 );
	// and with the far one moved up out of the cone, nothing
	GamepadFocus::Box high[] = { boxes[0], boxes[1], boxes[2], box( 400, 60, 60, 30 ) };
	CHECK_EQ( GamepadFocus::pickNeighbourBox( high, 4, high[2], 1, 0, centreY( high[2] ) ), -1 );
}

TEST(the_dpad_keeps_its_column_through_a_shorter_row)
{
	// three rows: three buttons, one wide one on the left, three again; going down from the right-hand column
	GamepadFocus::Box boxes[ 7 ];
	for (Int c = 0; c < 3; ++c)
	{
		boxes[ c ] = box( 100 + 150 * c, 100, 120, 30 );
		boxes[ 4 + c ] = box( 100 + 150 * c, 300, 120, 30 );
	}
	boxes[ 3 ] = box( 100, 200, 330, 30 );		// the short row: one wide button, ending 30 pixels short of the right column
	const Int column = centreX( boxes[ 2 ] );	// the right-hand column, 459
	const Int middle = GamepadFocus::pickNeighbourBox( boxes, 7, boxes[ 2 ], 0, 1, column );
	CHECK_EQ( middle, 3 );										// the short row's button, nearer than the right column's next one
	// from it, the column remembered: the right-hand button of the last row, not the one under the short button
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 7, boxes[ middle ], 0, 1, column ), 6 );
	// with the lane taken from the short button itself, it would have gone to the middle column
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 7, boxes[ middle ], 0, 1, centreX( boxes[ middle ] ) ), 5 );
}

TEST(the_dpad_goes_left_and_right_along_its_row)
{
	// two rows of three, the lower row a little offset: right keeps the row, never jumping down to it
	GamepadFocus::Box boxes[ 6 ];
	for (Int c = 0; c < 3; ++c)
	{
		boxes[ c ] = box( 100 + 150 * c, 100, 120, 30 );
		boxes[ 3 + c ] = box( 130 + 150 * c, 145, 120, 30 );
	}
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 6, boxes[ 0 ], 1, 0, centreY( boxes[ 0 ] ) ), 1 );
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 6, boxes[ 1 ], 1, 0, centreY( boxes[ 1 ] ) ), 2 );
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 6, boxes[ 2 ], 1, 0, centreY( boxes[ 2 ] ) ), -1 );	// no wrap
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 6, boxes[ 3 ], -1, 0, centreY( boxes[ 3 ] ) ), -1 );	// its row ends
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 6, boxes[ 1 ], 0, 1, centreX( boxes[ 1 ] ) ), 4 );		// down: below it
}

TEST(the_command_cards_grid_keeps_its_column_going_down)
{
	// the command bar: 7 columns by 2 rows, 50 pixels apart, buttons 44 wide, one slot empty in the bottom row
	GamepadFocus::Box boxes[ 13 ];
	Int count = 0;
	for (Int row = 0; row < 2; ++row)
		for (Int column = 0; column < 7; ++column)
			if (!(row == 1 && column == 3))
				boxes[ count++ ] = box( 78 + column * 50, 478 + row * 50, 44, 44 );
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, count, boxes[ 2 ], 0, 1, centreX( boxes[ 2 ] ) ), 9 );	// column 2
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, count, boxes[ 3 ], 0, 1, centreX( boxes[ 3 ] ) ), 9 );	// over the gap: the nearest in the cone
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, count, boxes[ 9 ], 1, 0, centreY( boxes[ 9 ] ) ), 10 );	// across the gap on its row
}

TEST(the_dpad_goes_down_its_column_to_the_buttons_below_before_across_to_a_nearer_column)
{
	// Options' Controls page at 1280x800: the Orders column's last box, the Input column's last two beside and below
	// it, and Cancel far below in the Orders column's own span
	const GamepadFocus::Box boxes[] = { box( 458, 280, 364, 31 ), box( 858, 318, 337, 31 ), box( 858, 355, 337, 31 ),
		box( 622, 638, 284, 40 ) };
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 4, boxes[0], 0, 1, centreX( boxes[0] ) ), 3 );
	// with nothing in line below, the step goes aside, to the nearest in the cone
	CHECK_EQ( GamepadFocus::pickNeighbourBox( boxes, 3, boxes[0], 0, 1, centreX( boxes[0] ) ), 1 );
}

TEST(aim_assist_picks_the_nearest_point_within_its_radius)
{
	const ICoord2D points[] = { { 100, 100 }, { 130, 100 }, { 112, 109 }, { 400, 400 } };
	CHECK_EQ( GamepadAim::nearest( points, 4, 110, 100, 24 ), 2 );		// 9.2 away, nearer than 10 and 20
	CHECK_EQ( GamepadAim::nearest( points, 4, 300, 300, 24 ), -1 );	// nothing within the radius
	CHECK_EQ( GamepadAim::nearest( points, 4, 400, 424, 24 ), 3 );		// exactly on the radius counts
	CHECK_EQ( GamepadAim::nearest( points, 4, 115, 90, 24 ), 0 );		// equals (18.0 and 18.0; 112,109 is 19.2): the first
	CHECK_EQ( GamepadAim::nearest( points, 0, 0, 0, 24 ), -1 );
	CHECK( GamepadAim::radiusFor( 800 ) == 24 && GamepadAim::radiusFor( 1080 ) == 32 && GamepadAim::radiusFor( 200 ) == 12 );
}

TEST(aim_assist_eases_onto_its_target_and_never_overshoots)
{
	const ICoord2D target = { 200, 100 };
	Real x = 176.0f, y = 100.0f, last = x;
	Bool overshot = FALSE, monotonic = TRUE;
	for (Int frame = 0; frame < 30; ++frame)		// half a second at 60 frames a second
	{
		GamepadAim::easeToward( x, y, target, 1.0f / 60.0f );
		overshot = overshot || x > 200.0f;
		monotonic = monotonic && x >= last;
		last = x;
	}
	CHECK( !overshot && monotonic );
	CHECK( x == 200.0f && y == 100.0f );		// arrived exactly: the last pixel is closed outright
	x = 176.0f;
	GamepadAim::easeToward( x, y, target, 0.0f );
	CHECK( x == 176.0f );		// no time, no move
	GamepadAim::easeToward( x, y, target, 0.06f );
	CHECK( x > 190.0f && x < 192.0f );		// one time constant: 24 * e^-1 = 8.8 left
}

TEST(the_radial_menus_sectors_start_at_the_top_and_go_clockwise)
{
	CHECK_EQ( GamepadRadial::sectorFor( 0.0f, -1.0f, 4 ), 0 );		// up (a stick's up is negative)
	CHECK_EQ( GamepadRadial::sectorFor( 1.0f, 0.0f, 4 ), 1 );			// right
	CHECK_EQ( GamepadRadial::sectorFor( 0.0f, 1.0f, 4 ), 2 );			// down
	CHECK_EQ( GamepadRadial::sectorFor( -1.0f, 0.0f, 4 ), 3 );		// left
	CHECK_EQ( GamepadRadial::sectorFor( -0.2f, -0.9f, 4 ), 0 );		// a little left of up is still the top
	CHECK_EQ( GamepadRadial::sectorFor( 0.3f, 0.0f, 4 ), -1 );		// under the picking tilt: no pick
	CHECK_EQ( GamepadRadial::sectorFor( 1.0f, 0.0f, 0 ), -1 );		// no sectors
	CHECK_EQ( GamepadRadial::sectorFor( 0.0f, -1.0f, 14 ), 0 );
	CHECK_EQ( GamepadRadial::sectorFor( 0.0f, 1.0f, 14 ), 7 );		// straight down is the middle of fourteen
	CHECK_EQ( GamepadRadial::sectorFor( -0.05f, -1.0f, 14 ), 0 );	// just left of up wraps to the first, not the last
	CHECK_EQ( GamepadRadial::sectorFor( -0.5f, -0.87f, 14 ), 13 );	// 30 degrees left of up: the last sector
	CHECK_EQ( GamepadRadial::sectorFor( 1.0f, 0.0f, 1 ), 0 );			// one sector takes every direction
}

TEST(the_right_trigger_with_no_command_bar_shown_does_nothing)
{
	CHECK( start() );
	clear();
	Output pad;
	padAxis( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX );
	SdlGamepad_update( 13000 );
	padAxis( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	SdlGamepad_update( 13100 );
	frame( pad );
	CHECK( pad.events.empty() );
	CHECK( !SdlGamepad_inCommandBar() );
}

// ---- The hints (GamepadHints.h) ------------------------------------------------------------------

TEST(each_familys_face_glyphs_are_what_sdl_says_is_printed_on_the_buttons)
{
	struct Family { GamepadGlyphSet set; SDL_GamepadType type; };
	const Family families[] = {
		{ GAMEPAD_GLYPHS_XBOX, SDL_GAMEPAD_TYPE_XBOXONE },
		{ GAMEPAD_GLYPHS_XBOX, SDL_GAMEPAD_TYPE_STANDARD },
		{ GAMEPAD_GLYPHS_PLAYSTATION, SDL_GAMEPAD_TYPE_PS5 },
		{ GAMEPAD_GLYPHS_NINTENDO, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO },
		{ GAMEPAD_GLYPHS_STEAM_DECK, SDL_GAMEPAD_TYPE_XBOXONE },		// the Deck's are labelled as an Xbox pad's
	};
	const SDL_GamepadButton faces[] = { SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH };
	for (size_t f = 0; f < sizeof( families ) / sizeof( families[0] ); ++f)
		for (size_t b = 0; b < 4; ++b)
		{
			const char *name = GamepadHints::glyphName( families[f].set, faces[b] );
			const char *printed = NULL;
			switch (SDL_GetGamepadButtonLabelForType( families[f].type, faces[b] ))
			{
				case SDL_GAMEPAD_BUTTON_LABEL_A:				printed = "_button_a"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_B:				printed = "_button_b"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_X:				printed = "_button_x"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_Y:				printed = "_button_y"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_CROSS:		printed = "_button_cross"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:		printed = "_button_circle"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:		printed = "_button_square"; break;
				case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:	printed = "_button_triangle"; break;
				default: break;
			}
			const bool matches = name != NULL && printed != NULL && strlen( name ) >= strlen( printed )
				&& strcmp( name + strlen( name ) - strlen( printed ), printed ) == 0;
			if (!matches)
				printf( "    family %d button %d: glyph %s, SDL prints %s\n", (int)families[f].set, (int)faces[b], name ? name : "(none)", printed ? printed : "(unknown)" );
			CHECK( matches );
		}
}

TEST(no_family_draws_a_logo_or_a_coloured_glyph)
{
	for (Int set = GAMEPAD_GLYPHS_NONE + 1; set < GAMEPAD_GLYPHS_COUNT; ++set)
	{
		CHECK( GamepadHints::glyphName( (GamepadGlyphSet)set, GAMEPAD_BUTTON_GUIDE ) == NULL );
		for (Int button = 0; button < GAMEPAD_GLYPH_COUNT; ++button)
		{
			const char *name = GamepadHints::glyphName( (GamepadGlyphSet)set, button );
			if (name == NULL)
				continue;
			const bool plain = strstr( name, "guide" ) == NULL && strstr( name, "home" ) == NULL
				&& strstr( name, "quickaccess" ) == NULL && strstr( name, "controller_" ) == NULL && strstr( name, "color" ) == NULL;
			if (!plain)
				printf( "    family %d draws %s\n", (int)set, name );
			CHECK( plain );
		}
	}
}

TEST(every_glyph_named_is_in_its_vendored_font_map)
{
	const char *const maps[ GAMEPAD_GLYPHS_COUNT ] = { NULL, "kenney_input_xbox_series_map.txt",
		"kenney_input_playstation_series_map.txt", "kenney_input_nintendo_switch_map.txt", "kenney_input_steam_deck_map.txt" };
	std::string probe;
	if (!readFile( (std::string( KENNEY_DIR ) + "/License.txt").c_str(), probe ))
	{
		printf( "  SKIP: %s is not vendored (Tools/vendor.sh fetches it): the glyph names are not checked against it\n", KENNEY_DIR );
		return;
	}
	CHECK( probe.find( "Creative Commons Zero, CC0" ) != std::string::npos );
	for (Int set = GAMEPAD_GLYPHS_NONE + 1; set < GAMEPAD_GLYPHS_COUNT; ++set)
	{
		std::string map;
		CHECK( readFile( (std::string( KENNEY_DIR ) + "/" + maps[set]).c_str(), map ) );
		for (Int button = 0; button < GAMEPAD_GLYPH_COUNT; ++button)
		{
			const char *name = GamepadHints::glyphName( (GamepadGlyphSet)set, button );
			if (name == NULL)
				continue;
			const bool found = map.find( std::string( "\n" ) + name + ":" ) != std::string::npos
				|| map.compare( 0, strlen( name ) + 1, std::string( name ) + ":" ) == 0;
			if (!found)
				printf( "    %s has no %s\n", maps[set], name );
			CHECK( found );
		}
	}
}

TEST(the_hints_follow_the_device_in_use_live)
{
	CHECK( start() );
	pushMotion( 321, 123 );
	clear();
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_NONE );
	padButton( SDL_GAMEPAD_BUTTON_WEST, true );		// the virtual pad: SDL knows no family, so Xbox's A B X Y
	padButton( SDL_GAMEPAD_BUTTON_WEST, false );
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_XBOX );
	pushKey( SDL_SCANCODE_S, true );							// a hand on the keyboard: the letters again
	pushKey( SDL_SCANCODE_S, false );
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_NONE );
	padButton( SDL_GAMEPAD_BUTTON_WEST, true );
	padButton( SDL_GAMEPAD_BUTTON_WEST, false );
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_XBOX );
	pushMotion( 322, 124 );												// a hand on the mouse
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_NONE );
	clear();
}

TEST(the_controller_option_off_ignores_the_pad_and_lets_go_of_what_it_held)
{
	CHECK( start() );
	pushMotion( 150, 450 );
	clear();
	Output pad, hand;
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true );		// Ctrl held by the pad
	frame( pad );
	TheWritableGlobalData->m_gamepadEnabled = FALSE;				// Options > Controls > Controller, unticked
	SdlGamepad_update( 60000 );															// lets go of the Ctrl
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, true );						// ignored
	padButton( SDL_GAMEPAD_BUTTON_SOUTH, false );
	frame( pad );
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_NONE );
	TheWritableGlobalData->m_gamepadEnabled = TRUE;
	padButton( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false );	// its release, from before: nothing held now
	frame( pad );
	pushKey( SDL_SCANCODE_LCTRL, true );
	frame( hand );
	pushKey( SDL_SCANCODE_LCTRL, false );
	frame( hand );
	frame( hand );
	CHECK( same( pad, hand ) );
}

/// A second virtual pad that SDL takes for a real family by its USB ids, as it takes a plugged-in one
SDL_Joystick *attachFamilyPad( Uint16 vendor, Uint16 product, const char *name )
{
	SDL_VirtualJoystickDesc desc;
	SDL_INIT_INTERFACE( &desc );
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.vendor_id = vendor;
	desc.product_id = product;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.name = name;
	const SDL_JoystickID id = SDL_AttachVirtualJoystick( &desc );
	SDL_Joystick *joystick = id != 0 ? SDL_OpenJoystick( id ) : NULL;
	if (joystick != NULL)
	{
		SDL_SetJoystickVirtualAxis( joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
		SDL_SetJoystickVirtualAxis( joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN );
	}
	pump();
	return joystick;
}

void detachFamilyPad( SDL_Joystick *joystick )
{
	const SDL_JoystickID id = SDL_GetJoystickID( joystick );
	SDL_CloseJoystick( joystick );
	SDL_DetachVirtualJoystick( id );
	pump();
}

void press( SDL_Joystick *joystick, SDL_GamepadButton button )
{
	SDL_SetJoystickVirtualButton( joystick, button, true );
	pump();
	SDL_SetJoystickVirtualButton( joystick, button, false );
	pump();
}

/// A tap of a pad's confirm button against a hand's left click, and of its cancel button against nothing: the
/// pad confirms (the left button) with confirm, and cancel clicks nothing at all (GamepadCancel)
bool confirmsWith( SDL_Joystick *joystick, SDL_GamepadButton confirm, SDL_GamepadButton cancel )
{
	static int calls = 0;
	const float x = 400.0f + 20.0f * (calls++ % 10), y = 310.0f;		// a new place each time: no double click with the last
	pushMotion( x, y );
	clear();
	Output pad, hand, cancelled;
	press( joystick, confirm );
	frame( pad );
	pushButton( SDL_BUTTON_LEFT, true, 1, x, y );
	pushButton( SDL_BUTTON_LEFT, false, 1, x, y );
	frame( hand );
	press( joystick, cancel );
	frame( cancelled );
	if (!cancelled.events.empty())
	{
		printf( "    cancel gave %d event(s), the first %s\n", (int)cancelled.events.size(), cancelled.events[0].c_str() );
		return false;
	}
	return same( pad, hand );
}

TEST(a_nintendo_pad_confirms_with_a_on_the_right_and_its_hints_draw_a_for_confirm)
{
	CHECK( start() );
	SDL_Joystick *nintendo = attachFamilyPad( 0x057e, 0x2009, "Nintendo Switch Pro Controller" );		// SDL's ids for it
	CHECK( nintendo != NULL );
	CHECK_EQ( SdlGamepad_count(), 2 );
	CHECK( confirmsWith( nintendo, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH ) );
	press( nintendo, SDL_GAMEPAD_BUTTON_DPAD_UP );		// used last: its family and its confirm show
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_NINTENDO );
	CHECK( GamepadHints::isConfirmSwapped() );
	// confirm's hint is the glyph of A, which does it; cancel's is B's
	CHECK( strcmp( GamepadHints::glyphName( GAMEPAD_GLYPHS_NINTENDO, GamepadHints::physicalFor( GAMEPAD_BUTTON_SOUTH ) ), "switch_button_a" ) == 0 );
	CHECK( strcmp( GamepadHints::glyphName( GAMEPAD_GLYPHS_NINTENDO, GamepadHints::physicalFor( GAMEPAD_BUTTON_EAST ) ), "switch_button_b" ) == 0 );
	CHECK_EQ( GamepadHints::physicalFor( GAMEPAD_BUTTON_NORTH ), (Int)GAMEPAD_BUTTON_NORTH );
	// the Xbox-labelled pad, used next, confirms at the bottom again
	CHECK( confirmsWith( thePad, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST ) );
	CHECK( !GamepadHints::isConfirmSwapped() );
	CHECK( strcmp( GamepadHints::glyphName( GAMEPAD_GLYPHS_XBOX, GamepadHints::physicalFor( GAMEPAD_BUTTON_SOUTH ) ), "xbox_button_a" ) == 0 );
	detachFamilyPad( nintendo );
	CHECK_EQ( SdlGamepad_count(), 1 );
}

TEST(a_playstation_pad_confirms_with_cross_at_the_bottom)
{
	CHECK( start() );
	SDL_Joystick *playstation = attachFamilyPad( 0x054c, 0x0ce6, "DualSense Wireless Controller" );
	CHECK( playstation != NULL );
	CHECK( confirmsWith( playstation, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST ) );
	press( playstation, SDL_GAMEPAD_BUTTON_DPAD_UP );		// used last, after the hand's clicks
	CHECK_EQ( (Int)GamepadHints::getShown(), (Int)GAMEPAD_GLYPHS_PLAYSTATION );
	CHECK( !GamepadHints::isConfirmSwapped() );
	CHECK( strcmp( GamepadHints::glyphName( GAMEPAD_GLYPHS_PLAYSTATION, GamepadHints::physicalFor( GAMEPAD_BUTTON_SOUTH ) ), "playstation_button_cross" ) == 0 );
	detachFamilyPad( playstation );
}

TEST(the_swap_option_trades_confirm_and_cancel_on_every_family)
{
	CHECK( start() );
	SDL_Joystick *nintendo = attachFamilyPad( 0x057e, 0x2009, "Nintendo Switch Pro Controller" );
	CHECK( nintendo != NULL );
	TheWritableGlobalData->m_gamepadSwapConfirm = TRUE;		// Options > Controls > Swap Confirm and Cancel
	CHECK( confirmsWith( thePad, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH ) );
	CHECK( GamepadHints::isConfirmSwapped() );
	CHECK( confirmsWith( nintendo, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST ) );		// swapped back: B below confirms
	CHECK( !GamepadHints::isConfirmSwapped() );
	TheWritableGlobalData->m_gamepadSwapConfirm = FALSE;
	CHECK( confirmsWith( thePad, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST ) );		// armed: the option did it
	detachFamilyPad( nintendo );
}

TEST(a_press_is_released_as_what_it_was_pressed_as_when_the_option_changes_between)
{
	CHECK( start() );
	pushMotion( 250, 350 );
	clear();
	Output pad, hand;
	SDL_SetJoystickVirtualButton( thePad, SDL_GAMEPAD_BUTTON_SOUTH, true );		// confirm: the left button down
	pump();
	TheWritableGlobalData->m_gamepadSwapConfirm = TRUE;
	SDL_SetJoystickVirtualButton( thePad, SDL_GAMEPAD_BUTTON_SOUTH, false );	// still the left button's release
	pump();
	TheWritableGlobalData->m_gamepadSwapConfirm = FALSE;
	frame( pad );
	pushButton( SDL_BUTTON_LEFT, true, 1, 250, 350 );
	pushButton( SDL_BUTTON_LEFT, false, 1, 250, 350 );
	frame( hand );
	CHECK( same( pad, hand ) );
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
