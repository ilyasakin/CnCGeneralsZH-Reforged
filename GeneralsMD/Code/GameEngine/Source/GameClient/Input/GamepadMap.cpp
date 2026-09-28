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

// GamepadMap.cpp: see GamepadMap.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "GameClient/GamepadMap.h"

GamepadMap *TheGamepadMap = NULL;

const LookupListRec TheGamepadButtonNames[] =
{
	{ "South",					GAMEPAD_BUTTON_SOUTH },
	{ "East",						GAMEPAD_BUTTON_EAST },
	{ "West",						GAMEPAD_BUTTON_WEST },
	{ "North",					GAMEPAD_BUTTON_NORTH },
	{ "Back",						GAMEPAD_BUTTON_BACK },
	{ "Guide",					GAMEPAD_BUTTON_GUIDE },
	{ "Start",					GAMEPAD_BUTTON_START },
	{ "LeftStick",			GAMEPAD_BUTTON_LEFT_STICK },
	{ "RightStick",			GAMEPAD_BUTTON_RIGHT_STICK },
	{ "LeftShoulder",		GAMEPAD_BUTTON_LEFT_SHOULDER },
	{ "RightShoulder",	GAMEPAD_BUTTON_RIGHT_SHOULDER },
	{ "DPadUp",					GAMEPAD_BUTTON_DPAD_UP },
	{ "DPadDown",				GAMEPAD_BUTTON_DPAD_DOWN },
	{ "DPadLeft",				GAMEPAD_BUTTON_DPAD_LEFT },
	{ "DPadRight",			GAMEPAD_BUTTON_DPAD_RIGHT },
	{ "Misc1",					GAMEPAD_BUTTON_MISC1 },
	{ "RightPaddle1",		GAMEPAD_BUTTON_RIGHT_PADDLE1 },
	{ "LeftPaddle1",		GAMEPAD_BUTTON_LEFT_PADDLE1 },
	{ "RightPaddle2",		GAMEPAD_BUTTON_RIGHT_PADDLE2 },
	{ "LeftPaddle2",		GAMEPAD_BUTTON_LEFT_PADDLE2 },
	{ "Touchpad",				GAMEPAD_BUTTON_TOUCHPAD },
	{ "LeftTrigger",		GAMEPAD_BUTTON_LEFT_TRIGGER },
	{ "RightTrigger",		GAMEPAD_BUTTON_RIGHT_TRIGGER },
	{ NULL, 0 }
};

const LookupListRec TheGamepadActionNames[] =
{
	{ "None",						GAMEPAD_ACTION_NONE },
	{ "MouseLeft",			GAMEPAD_ACTION_MOUSE_LEFT },
	{ "MouseMiddle",		GAMEPAD_ACTION_MOUSE_MIDDLE },
	{ "MouseRight",			GAMEPAD_ACTION_MOUSE_RIGHT },
	{ "Modifier",				GAMEPAD_ACTION_MODIFIER },
	{ "Key",						GAMEPAD_ACTION_KEY },
	{ "Command",				GAMEPAD_ACTION_COMMAND },
	{ "CommandBar",			GAMEPAD_ACTION_COMMAND_BAR },
	{ "Structures",			GAMEPAD_ACTION_STRUCTURES },
	{ "Order",					GAMEPAD_ACTION_ORDER },
	{ "Cancel",					GAMEPAD_ACTION_CANCEL },
	{ NULL, 0 }
};

static const LookupListRec TheGamepadStepNames[] =
{
	{ "Next",						1 },
	{ "Previous",				-1 },
	{ NULL, 0 }
};

static const FieldParse TheGamepadBindingFieldParseTable[] =
{
	{ "With",						INI::parseLookupList,		TheGamepadButtonNames,	offsetof( GamepadBinding, m_with ) },
	{ "Action",					INI::parseLookupList,		TheGamepadActionNames,	offsetof( GamepadBinding, m_action ) },
	{ "Key",						INI::parseLookupList,		KeyNames,								offsetof( GamepadBinding, m_key ) },
	{ "Modifiers",			INI::parseLookupList,		ModifierNames,					offsetof( GamepadBinding, m_modState ) },
	{ "Command",				GamepadMap::parseCommandName,	NULL,							offsetof( GamepadBinding, m_command ) },
	{ "Step",						INI::parseLookupList,		TheGamepadStepNames,		offsetof( GamepadBinding, m_step ) },
	{ "OnRelease",			INI::parseBool,					NULL,										offsetof( GamepadBinding, m_onRelease ) },
	{ "CameraLayer",		INI::parseBool,					NULL,										offsetof( GamepadBinding, m_cameraLayer ) },
	{ NULL,							NULL,										0,											0 }
};

GamepadMap::GamepadMap()
{
}

GamepadMap::~GamepadMap()
{
}

const GamepadBinding *GamepadMap::find( GamepadButtonType button, GamepadButtonType with ) const
{
	for (size_t i = 0; i < m_bindings.size(); ++i)
		if (m_bindings[i].m_button == button && m_bindings[i].m_with == with)
			return &m_bindings[i];
	return NULL;
}

GamepadButtonType GamepadMap::buttonFor( GamepadActionType action ) const
{
	for (size_t i = 0; i < m_bindings.size(); ++i)
		if (m_bindings[i].m_action == action && m_bindings[i].m_with == GAMEPAD_BUTTON_NONE)
			return m_bindings[i].m_button;
	return GAMEPAD_BUTTON_NONE;
}

Bool GamepadMap::hasChordsWith( GamepadButtonType with ) const
{
	for (size_t i = 0; i < m_bindings.size(); ++i)
		if (m_bindings[i].m_with == with)
			return TRUE;
	return FALSE;
}

void GamepadMap::set( const GamepadBinding &binding )
{
	for (size_t i = 0; i < m_bindings.size(); ++i)
		if (m_bindings[i].m_button == binding.m_button && m_bindings[i].m_with == binding.m_with)
		{
			m_bindings[i] = binding;
			return;
		}
	m_bindings.push_back( binding );
}

/*static*/ void GamepadMap::parseCommandName( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	// the names the command maps use (MetaEvent.cpp), so the command maps must already exist
	const char *name = ini->getNextToken();
	const GameMessage::Type type = TheMetaMap != NULL ? TheMetaMap->findGameMessageMetaType( name ) : GameMessage::MSG_INVALID;
	if (type == GameMessage::MSG_INVALID)
		throw INI_INVALID_DATA;
	*(GameMessage::Type *)store = type;
}

/*static*/ Bool GamepadMap::keyForCommand( GameMessage::Type command, MappableKeyType &key, MappableKeyModState &modState )
{
	if (TheMetaMap == NULL)
		return FALSE;
	for (const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec != NULL; rec = rec->m_next)
		if (rec->m_meta == command && rec->m_key != MK_NONE)
		{
			key = rec->m_key;
			modState = rec->m_modState;
			return TRUE;
		}
	return FALSE;
}

/*static*/ void GamepadMap::parseGamepadBinding( INI *ini )
{
	const Int button = INI::scanLookupList( ini->getNextToken(), TheGamepadButtonNames );

	GamepadBinding binding;
	binding.m_button = (GamepadButtonType)button;
	binding.m_with = GAMEPAD_BUTTON_NONE;
	binding.m_action = GAMEPAD_ACTION_NONE;
	binding.m_key = MK_NONE;
	binding.m_modState = NONE;
	binding.m_command = GameMessage::MSG_INVALID;
	binding.m_step = 0;
	binding.m_onRelease = FALSE;
	binding.m_cameraLayer = FALSE;
	ini->initFromINI( &binding, TheGamepadBindingFieldParseTable );

	// a modifier action with no modifier, a key action with no key, a command action with no command, a
	// structures action with no step, or a chord with itself, can only be a mistake in the file
	if ((binding.m_action == GAMEPAD_ACTION_MODIFIER && binding.m_modState == NONE)
			|| (binding.m_action == GAMEPAD_ACTION_KEY && binding.m_key == MK_NONE)
			|| (binding.m_action == GAMEPAD_ACTION_COMMAND && binding.m_command == GameMessage::MSG_INVALID)
			|| (binding.m_action == GAMEPAD_ACTION_STRUCTURES && binding.m_step == 0)
			|| binding.m_with == binding.m_button)
		throw INI_INVALID_DATA;

	if (TheGamepadMap != NULL)
		TheGamepadMap->set( binding );
}

/*static*/ void INI::parseGamepadBindingDefinition( INI *ini )
{
	GamepadMap::parseGamepadBinding( ini );
}
