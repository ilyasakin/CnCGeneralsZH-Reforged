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

// GamepadFocus.cpp: see GamepadFocus.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GamepadFocus.h"
#include "GameClient/GamepadHints.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"

#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace {

GamepadFocus::Hooks theHooks = { NULL, NULL, NULL };
Bool thePadDriving = FALSE;
Int theFocusId = 0;										///< the focused widget's window id, 0 for none
std::string theScreenKey;							///< the screen the focus belongs to
std::map<std::string, Int> theLastFocus;	///< per screen, the focus it had when it was left

const UnsignedInt FOCUSABLE = GWS_PUSH_BUTTON | GWS_CHECK_BOX | GWS_RADIO_BUTTON | GWS_COMBO_BOX | GWS_HORZ_SLIDER
	| GWS_VERT_SLIDER | GWS_SCROLL_LISTBOX | GWS_ENTRY_FIELD;

/// The layouts shown above the shell's stack or a match without the modal stack, by their layouts' names: the
/// shell makes Options and the save and load popups beside its stack, and a menu makes its map and difficulty pickers
const char *const thePopupScreens[] = { "QuitMenu.wnd", "QuitNoSave.wnd", "Diplomacy.wnd", "GeneralsExpPoints.wnd",
	"PopupSaveLoad.wnd", "SaveLoad.wnd", "PopupReplay.wnd", "OptionsMenu.wnd", "InGameChat.wnd", "InGamePopupMessage.wnd",
	"ReplayControl.wnd", "DifficultySelect.wnd", "SkirmishMapSelectMenu.wnd", "LanMapSelectMenu.wnd", "DownloadMenu.wnd",
	NULL };

struct Screen
{
	std::vector<GameWindow *> roots;
	std::string key;
	Bool modal;
};

const char *nameOf( GameWindow *window )
{
	WinInstanceData *data = window != NULL ? window->winGetInstanceData() : NULL;
	return data != NULL ? data->m_decoratedNameString.str() : "";
}

Bool endsWith( const char *text, const char *tail )
{
	const size_t a = strlen( text ), b = strlen( tail );
	return a >= b && strcmp( text + a - b, tail ) == 0;
}

/// The popup a top-level window belongs to, NULL for none
const char *popupOf( GameWindow *window )
{
	for (const char *const *name = thePopupScreens; *name != NULL; ++name)
		if (strncmp( nameOf( window ), *name, strlen( *name ) ) == 0)
			return *name;
	return NULL;
}

/// The screen the pad drives now: the top modal window, else the topmost shown popup (the window list runs from the
/// top of the drawing order down, and a match can show the quit menu under Options), else the shell's top layout
Bool screenNow( Screen &screen )
{
	screen.roots.clear();
	screen.key.clear();
	screen.modal = FALSE;
	if (TheWindowManager == NULL)
		return FALSE;
	GameWindow *modal = TheWindowManager->winGetModal();
	if (modal != NULL && !modal->winIsHidden())
	{
		screen.roots.push_back( modal );
		screen.key = std::string( "modal:" ) + nameOf( modal );
		screen.modal = TRUE;
		return TRUE;
	}
	const char *popup = NULL;
	for (GameWindow *window = TheWindowManager->winGetWindowList(); window != NULL; window = window->winGetNext())
	{
		if (window->winIsHidden())
			continue;
		const char *name = popupOf( window );
		if (name == NULL || (popup != NULL && name != popup))
			continue;
		popup = name;
		screen.roots.push_back( window );
	}
	if (popup != NULL)
	{
		screen.key = popup;
		return TRUE;
	}
	if (TheShell != NULL && TheShell->isShellActive() && TheShell->top() != NULL && !TheShell->top()->isHidden())
	{
		for (GameWindow *window = TheShell->top()->getFirstWindow(); window != NULL; window = window->winGetNextInLayout())
			if (!window->winIsHidden())
				screen.roots.push_back( window );
		screen.key = TheShell->top()->getFilename().str();
		return !screen.roots.empty();
	}
	return FALSE;
}

/// The focusable widgets under window, itself included; gadgets' own parts are not descended into
void collect( GameWindow *window, std::vector<GameWindow *> &out )
{
	if (window == NULL || window->winIsHidden())
		return;
	if (window->winGetStyle() & FOCUSABLE)
	{
		if (BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ))
			out.push_back( window );
		return;
	}
	for (GameWindow *child = window->winGetChild(); child != NULL; child = child->winGetNext())
		collect( child, out );
}

void focusables( const Screen &screen, std::vector<GameWindow *> &out )
{
	out.clear();
	for (size_t i = 0; i < screen.roots.size(); ++i)
		collect( screen.roots[i], out );
}

void rectOf( GameWindow *window, Int &x, Int &y, Int &width, Int &height )
{
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
}

ICoord2D centreOf( GameWindow *window )
{
	Int x, y, width, height;
	rectOf( window, x, y, width, height );
	ICoord2D centre = { x + width / 2, y + height / 2 };
	return centre;
}

GameWindow *byId( const std::vector<GameWindow *> &widgets, Int id )
{
	for (size_t i = 0; i < widgets.size(); ++i)
		if (widgets[i]->winGetWindowId() == id)
			return widgets[i];
	return NULL;
}

GameWindow *byNameTail( const std::vector<GameWindow *> &widgets, const char *const *tails, UnsignedInt style )
{
	for (const char *const *tail = tails; *tail != NULL; ++tail)
		for (size_t i = 0; i < widgets.size(); ++i)
			if ((widgets[i]->winGetStyle() & style) && endsWith( nameOf( widgets[i] ), *tail ))
				return widgets[i];
	return NULL;
}

const char *const theBackNames[] = { ":ButtonBack", ":ButtonCancel", ":ButtonNo", ":ButtonReturn", ":ButtonSingleBack",
	":ButtonMultiBack", ":ButtonLoadReplayBack", ":ButtonDiffBack", NULL };		// the last four: the main menu's panes'
const char *const theExitNames[] = { ":ButtonExit", NULL };
const char *const theStartNames[] = { ":ButtonStart", ":ButtonStartGame", ":ButtonAccept", ":ButtonOk", ":ButtonPlay",
	":ButtonLoad", ":ButtonContinue", ":ButtonResume", NULL };
const char *const theModalDefaults[] = { ":RadioButtonMedium", ":ButtonNo", ":ButtonCancel", ":ButtonOk", ":ButtonYes", NULL };
const char *const theScreenDefaults[] = { ":ButtonSinglePlayer", ":ButtonMedium", ":RadioButtonMedium", ":ButtonResume",
	":ButtonStart", ":ButtonAccept", ":ButtonOk", ":ButtonContinue", ":ButtonPlay", ":ButtonLoad", NULL };

const char *const theXNames[] = { ":ButtonDelete", ":ButtonDeleteReplay", ":ButtonDirectConnect", NULL };
const char *const theYNames[] = { ":ButtonDefaults", ":ButtonSave", ":ButtonCopyReplay", ":ButtonHost", NULL };
const char *const theMapSourceNames[] = { ":RadioButtonSystemMaps", ":RadioButtonUserMaps", NULL };

Bool isTab( GameWindow *window )
{
	return (window->winGetStyle() & GWS_PUSH_BUTTON) && strstr( nameOf( window ), "Tab" ) != NULL;
}

Bool isChosen( GameWindow *window )
{
	return BitTest( window->winGetInstanceData()->getState(), WIN_STATE_SELECTED );
}

/// The first widget in reading order, tabs aside: the top row (a band a line high, so a slider a few pixels above
/// a combo box beside it is not first), then its leftmost
GameWindow *firstInReadingOrder( const std::vector<GameWindow *> &widgets )
{
	const Int ROW = 16;
	Int top = 0;
	Bool any = FALSE;
	for (size_t i = 0; i < widgets.size(); ++i)
	{
		Int x, y, width, height;
		rectOf( widgets[i], x, y, width, height );
		if (!isTab( widgets[i] ) && (!any || y < top))
		{
			top = y;
			any = TRUE;
		}
	}
	GameWindow *best = NULL;
	Int bestX = 0;
	for (size_t i = 0; i < widgets.size(); ++i)
	{
		Int x, y, width, height;
		rectOf( widgets[i], x, y, width, height );
		if (!isTab( widgets[i] ) && y <= top + ROW && (best == NULL || x < bestX))
		{
			best = widgets[i];
			bestX = x;
		}
	}
	return best;
}

Bool isMainMenu( const Screen &screen )
{
	return !screen.modal && endsWith( screen.key.c_str(), "MainMenu.wnd" );
}

/// The screens whose pages are the content, so each page starts on its first widget (skirmish setup's tabs only
/// switch its info panel, and it starts on Start Game)
const char *const thePagedScreens[] = { "OptionsMenu.wnd", NULL };

/// The screen's first focus: a paged screen's page's first widget, else the primary action by name, else the first
/// widget in reading order
GameWindow *defaultFocus( const Screen &screen, const std::vector<GameWindow *> &widgets )
{
	for (const char *const *paged = thePagedScreens; *paged != NULL && !screen.modal; ++paged)
		if (screen.key.compare( 0, strlen( *paged ), *paged ) == 0)
		{
			GameWindow *first = firstInReadingOrder( widgets );
			if (first != NULL)
				return first;
		}
	GameWindow *named = byNameTail( widgets, screen.modal ? theModalDefaults : theScreenDefaults,
		GWS_PUSH_BUTTON | GWS_RADIO_BUTTON );		// radios: the difficulty pickers start on Medium
	return named != NULL ? named : firstInReadingOrder( widgets );
}

/// X's or Y's button here, NULL for none; X on a map list picks the map source not shown
GameWindow *secondaryButton( const std::vector<GameWindow *> &widgets, Bool y )
{
	GameWindow *named = byNameTail( widgets, y ? theYNames : theXNames, GWS_PUSH_BUTTON );
	if (named != NULL || y)
		return named;
	for (const char *const *tail = theMapSourceNames; *tail != NULL; ++tail)
		for (size_t i = 0; i < widgets.size(); ++i)
			if ((widgets[i]->winGetStyle() & GWS_RADIO_BUTTON) && endsWith( nameOf( widgets[i] ), *tail )
					&& !isChosen( widgets[i] ))
				return widgets[i];
	return NULL;
}

void pointAt( GameWindow *window )
{
	if (window != NULL && theHooks.pointTo != NULL)
	{
		const ICoord2D centre = centreOf( window );
		theHooks.pointTo( centre.x, centre.y );
	}
}

UnsignedInt theFocusChanges = 0;

/// A press held through a transition (GamepadFocus::update)
struct Held
{
	Bool held;
	GamepadFocus::Action action;
	std::string screen;
	UnsignedInt at;
};
Held theHeld = { FALSE, GamepadFocus::BACK, std::string(), 0 };
UnsignedInt theTransitionSince = 0;		///< when the shell's transition handler last began running, 0 while finished

/// The shell is running a transition (the menus drop a press meanwhile), and began it under HOLD_MS ago: a
/// transition that never reports finished (one of the main menu's panes) is not waited on.  On the main menu its
/// own flag says it (MainMenuTakesPresses): it lets go as a pane's transition ends and starts the side's logo
/// transition at once, so the transition handler seen from here would look busy past the moment presses work.
Bool shellLocked( UnsignedInt now, Bool mainMenu )
{
	const Bool running = mainMenu ? !MainMenuTakesPresses()
		: (TheTransitionHandler != NULL && !TheTransitionHandler->isFinished());
	if (!running)
	{
		theTransitionSince = 0;
		return FALSE;
	}
	if (theTransitionSince == 0)
		theTransitionSince = now;
	return now - theTransitionSince < (UnsignedInt)GamepadFocus::HOLD_MS;
}

const char *actionName( GamepadFocus::Action action )
{
	return action == GamepadFocus::ACCEPT_DOWN ? "A" : action == GamepadFocus::BACK ? "B" : "?";
}

void setFocus( GameWindow *window )
{
	static Int loggedId = 0;
	static std::string loggedScreen;
	theFocusId = window != NULL ? window->winGetWindowId() : 0;
	if (theFocusId != loggedId || theScreenKey != loggedScreen)
	{
		loggedId = theFocusId;
		loggedScreen = theScreenKey;
		++theFocusChanges;
		DEBUG_LOG(( "GAMEPAD FOCUS: %s %s\n", theScreenKey.c_str(), window != NULL ? nameOf( window ) : "(none)" ));
	}
	if (!theScreenKey.empty())
		theLastFocus[ theScreenKey ] = theFocusId;
	pointAt( window );
}

/// TRUE once the focusable widgets have been the same ones for SETTLE_MS (a screen is not still arriving)
Bool hasSettled( const std::vector<GameWindow *> &widgets )
{
	const UnsignedInt SETTLE_MS = 250;
	static UnsignedInt lastSignature = 0, sameSince = 0;
	UnsignedInt signature = 2166136261u;		// FNV-1a over the widgets' ids, in order
	for (size_t i = 0; i < widgets.size(); ++i)
		signature = (signature ^ (UnsignedInt)widgets[i]->winGetWindowId()) * 16777619u;
	const UnsignedInt now = Clock_Milliseconds();
	if (signature != lastSignature)
	{
		lastSignature = signature;
		sameSince = now;
	}
	return now - sameSince >= SETTLE_MS;
}

/// The focus on this screen: kept, restored from the last visit, or the default
GameWindow *currentFocus( const Screen &screen, const std::vector<GameWindow *> &widgets )
{
	if (screen.key != theScreenKey)
	{
		theScreenKey = screen.key;
		std::map<std::string, Int>::const_iterator last = theLastFocus.find( screen.key );
		theFocusId = last != theLastFocus.end() ? last->second : 0;
		GameWindow *restored = byId( widgets, theFocusId );
		if (restored != NULL)
			setFocus( restored );
	}
	GameWindow *focus = byId( widgets, theFocusId );
	if (focus == NULL)
	{
		// a pane moving in shows its buttons one by one, and some panes' transitions never report finished: the
		// default is shown at once, and kept once the screen's widgets have stood still for a moment
		focus = defaultFocus( screen, widgets );
		if (hasSettled( widgets ))
			setFocus( focus );
	}
	return focus;
}

void tapKey( UnsignedByte key )
{
	if (theHooks.key != NULL)
	{
		theHooks.key( key, TRUE );
		theHooks.key( key, FALSE );
	}
}

/// A key straight to one widget, as the window manager sends a focused gadget its keys; its answer
WindowMsgHandledType keyTo( GameWindow *window, UnsignedByte key )
{
	const WindowMsgHandledType down = TheWindowManager->winSendInputMsg( window, GWM_CHAR, key, KEY_STATE_DOWN );
	TheWindowManager->winSendInputMsg( window, GWM_CHAR, key, KEY_STATE_UP );
	return down;
}

Int selectedRow( GameWindow *listbox )
{
	Int row = -1;
	GadgetListBoxGetSelected( listbox, &row );
	return row;
}

/// A button pressed as a click presses it: the pointer on it, down and up
void press( GameWindow *button )
{
	pointAt( button );
	if (theHooks.leftButton != NULL)
	{
		theHooks.leftButton( TRUE );
		theHooks.leftButton( FALSE );
	}
}

/// The tab buttons (named ...Tab...), left to right, and which is chosen
Int tabButtons( const std::vector<GameWindow *> &widgets, std::vector<GameWindow *> &tabs )
{
	tabs.clear();
	for (size_t i = 0; i < widgets.size(); ++i)
		if (isTab( widgets[i] ))
			tabs.push_back( widgets[i] );
	for (size_t i = 1; i < tabs.size(); ++i)
		for (size_t j = i; j > 0 && centreOf( tabs[j] ).x < centreOf( tabs[j - 1] ).x; --j)
		{
			GameWindow *swap = tabs[j];
			tabs[j] = tabs[j - 1];
			tabs[j - 1] = swap;
		}
	for (size_t i = 0; i < tabs.size(); ++i)
		if (isChosen( tabs[i] ))
			return (Int)i;
	return -1;
}

}  // namespace

void GamepadFocus::setHooks( const Hooks &hooks )
{
	theHooks = hooks;
}

void GamepadFocus::update( void )
{
	if (!theHeld.held)
		return;
	const UnsignedInt now = Clock_Milliseconds();
	Screen screen;
	const Bool up = screenNow( screen );
	if (!up || screen.key != theHeld.screen)
	{
		theHeld.held = FALSE;
		DEBUG_LOG(( "GAMEPAD HELD: %s dropped, the screen changed\n", actionName( theHeld.action ) ));
		return;
	}
	if (now - theHeld.at > (UnsignedInt)HOLD_MS)
	{
		theHeld.held = FALSE;
		DEBUG_LOG(( "GAMEPAD HELD: %s dropped, the transition outlasted %d ms\n", actionName( theHeld.action ), (Int)HOLD_MS ));
		return;
	}
	if (shellLocked( now, isMainMenu( screen ) ))
		return;
	const Action action = theHeld.action;
	theHeld.held = FALSE;
	DEBUG_LOG(( "GAMEPAD HELD: %s pressed now, %u ms after the press, the transition over\n", actionName( action ), now - theHeld.at ));
	act( action );
	if (action == ACCEPT_DOWN)
		act( ACCEPT_UP );
}

UnsignedInt GamepadFocus::focusChanges( void )
{
	return theFocusChanges;
}

Bool GamepadFocus::isSettled( UnsignedInt stillMs )
{
	static UnsignedInt lastSignature = 0, sameSince = 0;
	Screen screen;
	UnsignedInt signature = 2166136261u;		// FNV-1a over the screen, the focus, and each widget's id and rectangle
	std::vector<GameWindow *> widgets;
	// a screen with nothing to focus yet (the shell still loading, a pane not yet shown) is not a menu to press on
	const Bool up = screenNow( screen ) && (focusables( screen, widgets ), !widgets.empty());
	if (up)
	{
		for (size_t i = 0; i < screen.key.size(); ++i)
			signature = (signature ^ (UnsignedInt)(unsigned char)screen.key[i]) * 16777619u;
		signature = (signature ^ (UnsignedInt)theFocusId) * 16777619u;
		for (size_t i = 0; i < widgets.size(); ++i)
		{
			Int x, y, width, height;
			rectOf( widgets[i], x, y, width, height );
			const Int parts[5] = { widgets[i]->winGetWindowId(), x, y, width, height };
			for (Int k = 0; k < 5; ++k)
				signature = (signature ^ (UnsignedInt)parts[k]) * 16777619u;
		}
	}
	const UnsignedInt now = Clock_Milliseconds();
	if (!up || signature != lastSignature)
	{
		lastSignature = signature;
		sameSince = now;
		return FALSE;
	}
	// the menus ignore a press while a transition runs (MainMenu.cpp's dontAllowTransitions), even with every
	// widget in its place; one pane's transition never reports finished, so a long enough stillness stands in
	const UnsignedInt TRANSITION_GIVE_UP_MS = 3000;
	const Bool moving = (isMainMenu( screen ) ? !MainMenuTakesPresses()
			: (TheTransitionHandler != NULL && !TheTransitionHandler->isFinished()))
		|| (TheShell != NULL && !TheShell->isAnimFinished());
	return now - sameSince >= (moving ? TRANSITION_GIVE_UP_MS : stillMs);
}

Bool GamepadFocus::isActive( void )
{
	Screen screen;
	return screenNow( screen );
}

void GamepadFocus::setPadDriving( Bool driving )
{
	thePadDriving = driving;
}

Bool GamepadFocus::hidesCursor( void )
{
	return thePadDriving && isActive();
}

GameWindow *GamepadFocus::getFocus( void )
{
	Screen screen;
	if (!screenNow( screen ))
		return NULL;
	std::vector<GameWindow *> widgets;
	focusables( screen, widgets );
	return widgets.empty() ? NULL : currentFocus( screen, widgets );
}

Int GamepadFocus::pickNeighbour( const ICoord2D *centres, Int count, Int x, Int y, Int dx, Int dy )
{
	Int best = -1, bestScore = 0;
	for (Int i = 0; i < count; ++i)
	{
		const Int ox = centres[i].x - x, oy = centres[i].y - y;
		const Int along = ox * dx + oy * dy;
		if (along <= 0)
			continue;
		const Int across = abs( ox * dy - oy * dx );
		const Int score = along + 2 * across;
		if (best < 0 || score < bestScore)
		{
			best = i;
			bestScore = score;
		}
	}
	return best;
}

Bool GamepadFocus::act( Action action )
{
	Screen screen;
	if (!screenNow( screen ))
		return FALSE;
	const UnsignedInt now = Clock_Milliseconds();
	if ((action == ACCEPT_DOWN || action == BACK) && !getenv( "ZH_TEST_NO_HOLD" ) && shellLocked( now, isMainMenu( screen ) ))
	{
		theHeld.held = TRUE;
		theHeld.action = action;
		theHeld.screen = screen.key;
		theHeld.at = now;
		DEBUG_LOG(( "GAMEPAD HELD: %s, pressed during a transition on %s\n", actionName( action ), screen.key.c_str() ));
		return TRUE;
	}
	if (action == ACCEPT_UP && theHeld.held && theHeld.action == ACCEPT_DOWN)
		return TRUE;		// the held press's release: the held press is a whole click when it is pressed
	if (theHeld.held && action >= NAV_UP && action <= NAV_RIGHT)
	{
		theHeld.held = FALSE;		// the player moved on
		DEBUG_LOG(( "GAMEPAD HELD: %s dropped, the focus moved\n", actionName( theHeld.action ) ));
	}
	std::vector<GameWindow *> widgets;
	focusables( screen, widgets );
	GameWindow *focus = widgets.empty() ? NULL : currentFocus( screen, widgets );
	const Bool isList = focus != NULL && (focus->winGetStyle() & GWS_SCROLL_LISTBOX);
	const Bool isSlider = focus != NULL && (focus->winGetStyle() & GWS_ALL_SLIDER);
	GameWindow *dropdown = (focus != NULL && (focus->winGetStyle() & GWS_COMBO_BOX)) ? GadgetComboBoxGetListBox( focus ) : NULL;
	const Bool dropdownOpen = dropdown != NULL && !dropdown->winIsHidden();

	switch (action)
	{
		case NAV_UP: case NAV_DOWN: case NAV_LEFT: case NAV_RIGHT:
		{
			const Bool vertical = action == NAV_UP || action == NAV_DOWN;
			const UnsignedByte key = action == NAV_UP ? KEY_UP : action == NAV_DOWN ? KEY_DOWN : action == NAV_LEFT ? KEY_LEFT : KEY_RIGHT;
			// an open combo's list, a list, a slider: the direction is theirs while it moves something
			if (dropdownOpen && vertical)
			{
				keyTo( dropdown, key );
				return TRUE;
			}
			if (isList && vertical)
			{
				const Int before = selectedRow( focus );
				keyTo( focus, key );
				if (selectedRow( focus ) != before)
					return TRUE;
			}
			if (isSlider && ((focus->winGetStyle() & GWS_HORZ_SLIDER) ? !vertical : vertical))
			{
				keyTo( focus, key );
				return TRUE;
			}
			if (focus == NULL)
				return TRUE;
			std::vector<ICoord2D> centres;
			for (size_t i = 0; i < widgets.size(); ++i)
				centres.push_back( centreOf( widgets[i] ) );
			const ICoord2D from = centreOf( focus );
			const Int dx = action == NAV_LEFT ? -1 : action == NAV_RIGHT ? 1 : 0;
			const Int dy = action == NAV_UP ? -1 : action == NAV_DOWN ? 1 : 0;
			Int next = pickNeighbour( &centres[0], (Int)centres.size(), from.x, from.y, dx, dy );
			if (next < 0 && vertical && isMainMenu( screen ))
			{
				// the main menu's column wraps: the far end of the same column
				const Int COLUMN = 40;
				for (size_t i = 0; i < centres.size(); ++i)
					if (abs( centres[i].x - from.x ) < COLUMN && (next < 0 || (dy > 0 ? centres[i].y < centres[ next ].y
							: centres[i].y > centres[ next ].y)))
						next = (Int)i;
			}
			if (next >= 0)
				setFocus( widgets[ next ] );
			return TRUE;
		}

		case ACCEPT_DOWN:
		case ACCEPT_UP:
		{
			if (focus == NULL)
				return TRUE;
			if (dropdownOpen || isList)
			{
				if (action == ACCEPT_DOWN)
					keyTo( dropdownOpen ? dropdown : focus, KEY_ENTER );
				return TRUE;
			}
			if (action == ACCEPT_DOWN)
				pointAt( focus );
			if (theHooks.leftButton != NULL)
				theHooks.leftButton( action == ACCEPT_DOWN );
			return TRUE;
		}

		case BACK:
		{
			if (dropdownOpen)
			{
				press( focus );		// the combo itself: a click closes its list
				return TRUE;
			}
			GameWindow *back = byNameTail( widgets, theBackNames, GWS_PUSH_BUTTON );
			GameWindow *exitButton = back == NULL && isMainMenu( screen ) ? byNameTail( widgets, theExitNames, GWS_PUSH_BUTTON ) : NULL;
			if (back != NULL)
				press( back );
			else if (exitButton != NULL)
				setFocus( exitButton );		// the top of the main menu: B goes to Exit, and A on it is the player's own choice
			else
				tapKey( KEY_ESC );		// the shell's menus take Escape as back
			return TRUE;
		}

		case START:
		{
			GameWindow *go = byNameTail( widgets, theStartNames, GWS_PUSH_BUTTON );
			if (go != NULL)
				press( go );
			return go != NULL;
		}

		case TAB_PREV:
		case TAB_NEXT:
		{
			std::vector<GameWindow *> tabs;
			const Int chosen = tabButtons( widgets, tabs );
			if (tabs.empty())
				return FALSE;
			Int next = chosen < 0 ? 0 : chosen + (action == TAB_NEXT ? 1 : -1);
			next = next < 0 ? 0 : (next >= (Int)tabs.size() ? (Int)tabs.size() - 1 : next);
			press( tabs[ next ] );
			theFocusId = 0;		// the new page's own first focus, found on the next action
			theLastFocus.erase( screen.key );
			theScreenKey.clear();
			return TRUE;
		}

		case ALT_X:
		case ALT_Y:
		{
			GameWindow *button = secondaryButton( widgets, action == ALT_Y );
			if (button != NULL)
				press( button );
			return button != NULL;
		}

		case PAGE_UP:
		case PAGE_DOWN:
		{
			GameWindow *list = dropdownOpen ? dropdown : (isList ? focus : NULL);
			for (Int i = 0; list != NULL && i < 8; ++i)
				keyTo( list, action == PAGE_UP ? KEY_UP : KEY_DOWN );
			return list != NULL;
		}
	}
	return FALSE;
}

void GamepadFocus::draw( void )
{
	if (!thePadDriving || TheDisplay == NULL)
		return;
	Screen screen;
	if (!screenNow( screen ))
		return;
	std::vector<GameWindow *> widgets;
	focusables( screen, widgets );
	GameWindow *focus = widgets.empty() ? NULL : currentFocus( screen, widgets );

	// the frame, a hand's width outside the widget
	if (focus != NULL)
	{
		Int x, y, width, height;
		rectOf( focus, x, y, width, height );
		TheDisplay->drawOpenRect( x - 3, y - 3, width + 6, height + 6, 2.0f, GameMakeColor( 255, 210, 60, 255 ) );
	}

	// the hint bar: the buttons that do something here, bottom right
	struct Item { Int button; const char *label; GameWindow *owner; };		// owner: a button whose own text is the word
	const Int MAX_ITEMS = 6;
	Item items[ MAX_ITEMS ];
	Int count = 0;
	items[ count++ ] = { GAMEPAD_BUTTON_SOUTH, "GUI:GamepadSelect", NULL };
	items[ count++ ] = { GAMEPAD_BUTTON_EAST, "GUI:GamepadBack", NULL };
	GameWindow *xButton = secondaryButton( widgets, FALSE ), *yButton = secondaryButton( widgets, TRUE );
	if (xButton != NULL)
		items[ count++ ] = { GAMEPAD_BUTTON_WEST, NULL, xButton };
	if (yButton != NULL)
		items[ count++ ] = { GAMEPAD_BUTTON_NORTH, NULL, yButton };
	if (byNameTail( widgets, theStartNames, GWS_PUSH_BUTTON ) != NULL)
		items[ count++ ] = { GAMEPAD_BUTTON_START, "GUI:GamepadStart", NULL };
	std::vector<GameWindow *> tabs;
	tabButtons( widgets, tabs );
	if (!tabs.empty())
		items[ count++ ] = { GAMEPAD_BUTTON_RIGHT_SHOULDER, "GUI:GamepadTabs", NULL };

	static DisplayString *glyphs[ MAX_ITEMS ] = { NULL }, *words[ MAX_ITEMS ] = { NULL };
	const Int points = TheDisplay->getHeight() / 45 > 11 ? TheDisplay->getHeight() / 45 : 11;
	GameFont *wordFont = TheFontLibrary != NULL ? TheFontLibrary->getFont( AsciiString( "Arial" ), points, TRUE ) : NULL;
	Int x = TheDisplay->getWidth() - 12;
	const Int centreY = TheDisplay->getHeight() - points - 10;		// the row's middle: glyph ink and word centred on it
	for (Int i = count - 1; i >= 0; --i)
	{
		GameFont *glyphFont = NULL;
		UnicodeString glyph;
		if (TheDisplayStringManager == NULL || wordFont == NULL
				|| !GamepadHints::glyphFor( GamepadHints::getShown(), items[i].button, points * 2, glyphFont, glyph ))
			continue;
		if (glyphs[i] == NULL)
			glyphs[i] = TheDisplayStringManager->newDisplayString();
		if (words[i] == NULL)
			words[i] = TheDisplayStringManager->newDisplayString();
		if (glyphs[i] == NULL || words[i] == NULL)
			continue;
		const UnicodeString word = items[i].owner != NULL ? items[i].owner->winGetText()
			: (TheGameText != NULL ? TheGameText->fetch( items[i].label ) : UnicodeString::TheEmptyString);
		if (glyphs[i]->getFont() != glyphFont)
			glyphs[i]->setFont( glyphFont );
		if (glyphs[i]->getText() != glyph)
			glyphs[i]->setText( glyph );
		if (words[i]->getFont() != wordFont)
			words[i]->setFont( wordFont );
		if (words[i]->getText() != word)
			words[i]->setText( word );
		Int gw, gh, ww, wh;
		glyphs[i]->getSize( &gw, &gh );
		words[i]->getSize( &ww, &wh );
		x -= ww;
		words[i]->draw( x, centreY - wh / 2, GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		x -= gw + 4;
		glyphs[i]->draw( x, GamepadHints::glyphTop( GamepadHints::getShown(), items[i].button, gh, centreY ),
			GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		x -= 18;
	}
}
