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

// GamepadMap.h: a gamepad's buttons, each bound to what a mouse button or a key already does (G1).
//
// The rule of G1 (docs/mac-port/tasks/G1-gamepad.md): a gamepad only produces the input the mouse and
// keyboard already produce.  So a binding here never names a GameMessage.  It names a mouse button, or a
// key with its modifiers, and the device layer (SdlGamepad) presses exactly that through the mouse's and
// keyboard's own devices.  The command maps then turn the key into whatever the player's input scheme
// binds it to, so Modern, Legacy and W A S D each keep their own meaning, and a rebound key carries over.
//
// The buttons are named by where they sit (SDL3's South, East, West, North...), not by what is printed on
// them: the same hand does the same thing on every pad, and only the drawn hint changes.
//
// Data\INI\GamepadReforged.ini fills it, one block a binding:
//
//   GamepadBinding DPadUp             ; the button
//     With = LeftShoulder             ; optional: only while this button is held
//     Action = Command                ; MouseLeft, MouseMiddle, MouseRight, Modifier, Key, Command, CommandBar
//                                     ; or Structures
//     Command = SELECT_TEAM5          ; for Command: a command map's name for a command
//     Step = Next                     ; for Structures: Next or Previous
//     OnRelease = Yes                 ; optional: act on the release, and only if no chord used the button
//   End
//
// Modifier holds modifiers alone (`Modifiers = SHIFT`), as a hand holds Shift.  Key presses a named key
// with its modifiers (`Key = KEY_S`, `Modifiers = CTRL`: the command maps' names).  Command
// presses whatever key, with whatever modifiers, the player's input scheme binds that command to at the
// moment of the press: S for STOP under Modern, G under W A S D.  Either way what arrives is a key.
//
// Structures selects the local player's next or previous production building and looks at it
// (GamepadCycle.h): the selection message is the one a click on that building sends.
//
// A binding with a With replaces its button's own binding while the other button is held.  The held
// button's own keys are let go for as long as the chord lasts (SdlGamepad.h).  OnRelease suits a button
// that is also a chord's With and whose own action cannot be taken back (a command): it acts only on a
// release with no chord made while it was held.  Loaded with no CRC, as
// the command maps are: it is the player's input, not the game's rules.

#pragma once

#ifndef __GAMEPADMAP_H
#define __GAMEPADMAP_H

#include "Common/SubsystemInterface.h"
#include "GameClient/MetaEvent.h"

#include <vector>

class INI;

/// The buttons by position, in SDL_GamepadButton's order (SDL3), so the device layer can cast
enum GamepadButtonType
{
	GAMEPAD_BUTTON_NONE = -1,
	GAMEPAD_BUTTON_SOUTH = 0,
	GAMEPAD_BUTTON_EAST,
	GAMEPAD_BUTTON_WEST,
	GAMEPAD_BUTTON_NORTH,
	GAMEPAD_BUTTON_BACK,
	GAMEPAD_BUTTON_GUIDE,
	GAMEPAD_BUTTON_START,
	GAMEPAD_BUTTON_LEFT_STICK,
	GAMEPAD_BUTTON_RIGHT_STICK,
	GAMEPAD_BUTTON_LEFT_SHOULDER,
	GAMEPAD_BUTTON_RIGHT_SHOULDER,
	GAMEPAD_BUTTON_DPAD_UP,
	GAMEPAD_BUTTON_DPAD_DOWN,
	GAMEPAD_BUTTON_DPAD_LEFT,
	GAMEPAD_BUTTON_DPAD_RIGHT,
	GAMEPAD_BUTTON_MISC1,
	GAMEPAD_BUTTON_RIGHT_PADDLE1,		///< the Deck's R4
	GAMEPAD_BUTTON_LEFT_PADDLE1,		///< the Deck's L4
	GAMEPAD_BUTTON_RIGHT_PADDLE2,		///< the Deck's R5
	GAMEPAD_BUTTON_LEFT_PADDLE2,		///< the Deck's L5
	GAMEPAD_BUTTON_TOUCHPAD,

	GAMEPAD_BUTTON_COUNT
};

/// The INI's names for GamepadButtonType, in its order
extern const LookupListRec TheGamepadButtonNames[];

enum GamepadActionType
{
	GAMEPAD_ACTION_NONE = 0,
	GAMEPAD_ACTION_MOUSE_LEFT,
	GAMEPAD_ACTION_MOUSE_MIDDLE,
	GAMEPAD_ACTION_MOUSE_RIGHT,
	GAMEPAD_ACTION_MODIFIER,
	GAMEPAD_ACTION_KEY,
	GAMEPAD_ACTION_COMMAND,
	GAMEPAD_ACTION_COMMAND_BAR,		///< in and out of command-bar mode (SdlGamepad.h)
	GAMEPAD_ACTION_STRUCTURES			///< the next or previous production building (GamepadCycle.h)
};

extern const LookupListRec TheGamepadActionNames[];

struct GamepadBinding
{
	GamepadButtonType			m_button;
	GamepadButtonType			m_with;				///< GAMEPAD_BUTTON_NONE: the button's own binding
	GamepadActionType			m_action;
	MappableKeyType				m_key;				///< for GAMEPAD_ACTION_KEY
	MappableKeyModState		m_modState;		///< for GAMEPAD_ACTION_MODIFIER, and held around GAMEPAD_ACTION_KEY's key
	GameMessage::Type			m_command;		///< for GAMEPAD_ACTION_COMMAND: a meta message the maps bind
	Int										m_step;				///< for GAMEPAD_ACTION_STRUCTURES: 1 the next, -1 the previous
	Bool									m_onRelease;	///< act on the release, only if no chord used the button
};

class GamepadMap : public SubsystemInterface
{
public:
	GamepadMap();
	virtual ~GamepadMap();

	virtual void init() { }
	virtual void reset() { }
	virtual void update() { }

	static void parseGamepadBinding( INI *ini );
	static void parseCommandName( INI *ini, void *instance, void *store, const void *userData );

	/** The key and modifiers the player's command map binds command to now, as a key press would need
		* them; FALSE when it binds none */
	static Bool keyForCommand( GameMessage::Type command, MappableKeyType &key, MappableKeyModState &modState );

	/// The binding of button while with is held (GAMEPAD_BUTTON_NONE: its own), or NULL
	const GamepadBinding *find( GamepadButtonType button, GamepadButtonType with ) const;

	/// Every binding with a With naming this button, so a press can look for a chord
	Bool hasChordsWith( GamepadButtonType with ) const;

	/// A binding in, replacing the one with the same button and With (the INI's overwrite)
	void set( const GamepadBinding &binding );
	void clear() { m_bindings.clear(); }
	Int getCount() const { return (Int)m_bindings.size(); }
	const GamepadBinding &get( Int index ) const { return m_bindings[ index ]; }

private:
	std::vector<GamepadBinding> m_bindings;
};

extern GamepadMap *TheGamepadMap;

#endif // __GAMEPADMAP_H
