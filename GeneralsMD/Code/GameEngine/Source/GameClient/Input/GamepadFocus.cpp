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
#include "GameClient/GameConsole.h"
#include "GameClient/GameClient.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/Mouse.h"
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
#include "GameLogic/GameLogic.h"

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
	Bool panel;		///< the trainer's cheat panel: no windows, its keys are the page's (panelKeys)
};

/// The trainer's cheat panel (GameConsole: Window/Html/Cheats.html over the match) is a screen while it is up and
/// no popup is over it.  Its keys are not windows: the focus is one of them, kept by the click it sends
const char *const CHEAT_PANEL_SCREEN = "Cheats.html";

Bool panelUp( void )
{
	return TheGameConsole != NULL && TheGameConsole->isCheatPanelOpen() && GameConsole::cheatsAvailable();
}

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
	screen.panel = FALSE;
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
	if (panelUp())
	{
		screen.key = CHEAT_PANEL_SCREEN;
		screen.panel = TRUE;
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
Bool theAwaitingPage = FALSE;						///< a tab was pressed and its page is not up yet
UnsignedInt theAwaitingSignature = 0, theAwaitingSince = 0;

/// The focusable widgets, as one number: FNV-1a over their ids, in order
UnsignedInt widgetSignature( const std::vector<GameWindow *> &widgets )
{
	UnsignedInt signature = 2166136261u;
	for (size_t i = 0; i < widgets.size(); ++i)
		signature = (signature ^ (UnsignedInt)widgets[i]->winGetWindowId()) * 16777619u;
	return signature;
}
Int theLaneX = 0, theLaneY = 0;		///< the D-pad's remembered column and row (GamepadFocus::pickNeighbourBox)
Int theLaneFocus = 0;							///< the widget they were remembered on: the focus moved otherwise, they start over

/// A press held through a transition (GamepadFocus::update)
struct Held
{
	Bool held;
	GamepadFocus::Action action;
	std::string screen;
	UnsignedInt atFrame, atMs;
};
Held theHeld = { FALSE, GamepadFocus::BACK, std::string(), 0, 0 };
UnsignedInt theTransitionSince = 0;		///< when (ms) the shell's transition handler last began running, 0 while finished

/** The client's frame: the shell's transitions step once a frame (WindowTransitions.ini counts FrameDelay in frames),
	* so the waits on them are counted in frames too; on a slow machine a transition takes longer in wall time */
/// Pictures drawn while a match stood paused (GamepadFocus::draw counts them): the client's frame is the logic's
/// (GameLogic::update sets it), which stands still under the pause menu, where a menu's stillness is still counted
UnsignedInt thePausedFrames = 0;

UnsignedInt clientFrame( void )
{
	return TheGameClient != NULL ? TheGameClient->getFrame() + thePausedFrames + 1 : 1;		// never 0, which means "none"
}

/// The shell is running a transition (a pane's buttons still scaling in drop a press, and so does the main
/// menu's own lock), and began it under HOLD_MS ago: a transition that never reports finished (one of the main
/// menu's panes) is not waited on.  The main menu's flag alone (MainMenuTakesPresses) is not enough: measured,
/// it was already clear while the difficulty pane's Back still dropped the click.
Bool shellLocked( void )
{
	const Bool running = TheTransitionHandler != NULL && !TheTransitionHandler->isFinished();
	const UnsignedInt now = Clock_Milliseconds() | 1;		// never 0, which means "none"
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
		if (window != NULL)
		{
			Int x, y, width, height;
			rectOf( window, x, y, width, height );
			DEBUG_LOG(( "GAMEPAD FOCUS AT: %d,%d %dx%d\n", x, y, width, height ));
		}
	}
	if (!theScreenKey.empty())
		theLastFocus[ theScreenKey ] = theFocusId;
	pointAt( window );
}

std::string thePanelFocus;					///< the cheat panel's focused key, by its click; kept while the panel is shut
Int thePanelLaneX = 0, thePanelLaneY = 0;	///< as theLaneX and theLaneY, over the panel's keys
std::string thePanelLaneFocus;

void setPanelFocus( const std::vector<GamepadFocus::Box> &boxes, const std::vector<std::string> &clicks, Int index )
{
	if (clicks[ index ] == thePanelFocus)
		return;
	thePanelFocus = clicks[ index ];
	++theFocusChanges;
	const GamepadFocus::Box &box = boxes[ index ];
	DEBUG_LOG(( "GAMEPAD FOCUS: %s %s\n", CHEAT_PANEL_SCREEN, thePanelFocus.c_str() ));
	DEBUG_LOG(( "GAMEPAD FOCUS AT: %d,%d %dx%d\n", box.left, box.top, box.right - box.left + 1, box.bottom - box.top + 1 ));
}

/// The cheat panel's keys as it last drew them, each with its click ("close" first), and the focused one's index:
/// the one focused before, else the first cheat's first key.  -1 before the panel has drawn any
Int panelKeys( std::vector<GamepadFocus::Box> &boxes, std::vector<std::string> &clicks )
{
	std::vector<IRegion2D> rects;
	TheGameConsole->cheatPanelKeys( rects, clicks );
	boxes.clear();
	for (size_t i = 0; i < rects.size(); ++i)
	{
		const GamepadFocus::Box box = { rects[i].lo.x, rects[i].lo.y, rects[i].hi.x - 1, rects[i].hi.y - 1 };
		boxes.push_back( box );
	}
	if (boxes.empty())
		return -1;
	for (size_t i = 0; i < clicks.size(); ++i)
		if (clicks[i] == thePanelFocus)
			return (Int)i;
	const Int first = boxes.size() > 1 ? 1 : 0;
	setPanelFocus( boxes, clicks, first );
	return first;
}

/// The pad on the cheat panel: the D-pad goes from key to key as on a menu's widgets, A clicks the key (the
/// panel takes the press as it takes a mouse's), B shuts the panel as Escape does
Bool actOnPanel( GamepadFocus::Action action )
{
	std::vector<GamepadFocus::Box> boxes;
	std::vector<std::string> clicks;
	const Int focus = panelKeys( boxes, clicks );
	switch (action)
	{
		case GamepadFocus::NAV_UP: case GamepadFocus::NAV_DOWN: case GamepadFocus::NAV_LEFT: case GamepadFocus::NAV_RIGHT:
		{
			if (focus < 0)
				return TRUE;
			const Bool vertical = action == GamepadFocus::NAV_UP || action == GamepadFocus::NAV_DOWN;
			const Int dx = action == GamepadFocus::NAV_LEFT ? -1 : action == GamepadFocus::NAV_RIGHT ? 1 : 0;
			const Int dy = action == GamepadFocus::NAV_UP ? -1 : action == GamepadFocus::NAV_DOWN ? 1 : 0;
			const GamepadFocus::Box &from = boxes[ focus ];
			if (clicks[ focus ] != thePanelLaneFocus)
			{
				thePanelLaneX = (from.left + from.right) / 2;
				thePanelLaneY = (from.top + from.bottom) / 2;
			}
			const Int next = GamepadFocus::pickNeighbourBox( &boxes[0], (Int)boxes.size(), from, dx, dy,
				vertical ? thePanelLaneX : thePanelLaneY );
			if (next >= 0)
			{
				setPanelFocus( boxes, clicks, next );
				if (vertical)
					thePanelLaneY = (boxes[ next ].top + boxes[ next ].bottom) / 2;
				else
					thePanelLaneX = (boxes[ next ].left + boxes[ next ].right) / 2;
				thePanelLaneFocus = clicks[ next ];
				if (theHooks.pointTo != NULL)
					theHooks.pointTo( thePanelLaneX, thePanelLaneY );
			}
			return TRUE;
		}

		case GamepadFocus::ACCEPT_DOWN:
		case GamepadFocus::ACCEPT_UP:
			if (focus < 0)
				return TRUE;
			if (action == GamepadFocus::ACCEPT_DOWN && theHooks.pointTo != NULL)
				theHooks.pointTo( (boxes[ focus ].left + boxes[ focus ].right) / 2, (boxes[ focus ].top + boxes[ focus ].bottom) / 2 );
			if (theHooks.leftButton != NULL)
				theHooks.leftButton( action == GamepadFocus::ACCEPT_DOWN );
			if (action == GamepadFocus::ACCEPT_DOWN)
				DEBUG_LOG(( "GAMEPAD FOCUS: %s %s pressed\n", CHEAT_PANEL_SCREEN, clicks[ focus ].c_str() ));
			return TRUE;

		case GamepadFocus::BACK:
			TheGameConsole->closeCheatPanel();
			DEBUG_LOG(( "GAMEPAD FOCUS: %s shut\n", CHEAT_PANEL_SCREEN ));
			return TRUE;

		default:
			return FALSE;		// Start, the shoulders, X and Y: nothing here, and the match waits until B
	}
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
	if (focus == NULL && theAwaitingPage)
	{
		// a tab was pressed: no default until its page's widgets are the ones up (or a second has gone)
		if (widgetSignature( widgets ) == theAwaitingSignature && Clock_Milliseconds() - theAwaitingSince < 1000)
			return NULL;
		theAwaitingPage = FALSE;
	}
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
/// Every tab button shown under window, enabled or not (Options disables the tab of the page it is on)
void collectTabs( GameWindow *window, std::vector<GameWindow *> &out )
{
	if (window == NULL || window->winIsHidden())
		return;
	if (isTab( window ))
	{
		out.push_back( window );
		return;
	}
	for (GameWindow *child = window->winGetChild(); child != NULL; child = child->winGetNext())
		collectTabs( child, out );
}

/// The screen's tabs, left to right, and which one is chosen: marked chosen, disabled (Options' way of showing its
/// page: its tab is the one that cannot be pressed), or the one whose page (its name, Tab for Page) is shown; -1
Int tabButtons( const Screen &screen, std::vector<GameWindow *> &tabs )
{
	tabs.clear();
	for (size_t i = 0; i < screen.roots.size(); ++i)
		collectTabs( screen.roots[i], tabs );
	for (size_t i = 1; i < tabs.size(); ++i)
		for (size_t j = i; j > 0 && centreOf( tabs[j] ).x < centreOf( tabs[j - 1] ).x; --j)
		{
			GameWindow *swap = tabs[j];
			tabs[j] = tabs[j - 1];
			tabs[j - 1] = swap;
		}
	for (size_t i = 0; i < tabs.size(); ++i)
		if (isChosen( tabs[i] ) || !BitTest( tabs[i]->winGetStatus(), WIN_STATUS_ENABLED ))
			return (Int)i;
	// else the tab whose page (its name, Tab for Page) is shown
	for (size_t i = 0; i < tabs.size() && TheWindowManager != NULL && TheNameKeyGenerator != NULL; ++i)
	{
		std::string name = nameOf( tabs[i] );
		const size_t at = name.rfind( "Tab" );
		if (at == std::string::npos)
			continue;
		name.replace( at, 3, "Page" );
		GameWindow *page = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( AsciiString( name.c_str() ) ) );
		if (page != NULL && !page->winIsHidden())
			return (Int)i;
	}
	return -1;
}

/// A combo box's list while it is open under the pad: the pad's own highlight, which it moves and draws itself.  The
/// list box's selection is never the highlight: in EA's list box a selection is the choice (GLM_SELECTED, then the
/// combo's GCM_SELECTED to its screen, which applies it, and the list shuts), so an arrow key into the list chose the
/// next row and closed it.  Nothing is chosen until A picks the highlight; B closes the list with nothing changed.
struct PadList { Int comboId; Int highlight; };
PadList thePadList = { 0, -1 };

GameWindow *openList( GameWindow *combo )
{
	GameWindow *list = combo != NULL && (combo->winGetStyle() & GWS_COMBO_BOX) ? GadgetComboBoxGetListBox( combo ) : NULL;
	return list != NULL && !list->winIsHidden() ? list : NULL;
}

/// The list scrolled, if it has to be, so that row shows
void showRow( GameWindow *list, Int row )
{
	if (row < GadgetListBoxGetTopVisibleEntry( list ))
		GadgetListBoxSetTopVisibleEntry( list, row );
	else if (row > GadgetListBoxGetBottomVisibleEntry( list ))
		GadgetListBoxSetBottomVisibleEntry( list, row );
}

/// Where a row of the list is drawn now, by the list's own reckoning (GadgetListBoxGetEntryBasedOnXY down its middle);
/// FALSE while it is scrolled out of sight
Bool rowRect( GameWindow *list, Int row, Int &x, Int &y, Int &width, Int &height )
{
	Int lx, ly, lw, lh;
	rectOf( list, lx, ly, lw, lh );
	Int top = -1, bottom = -1;
	for (Int yy = ly; yy < ly + lh; ++yy)
	{
		Int r = -1, c = -1;
		GadgetListBoxGetEntryBasedOnXY( list, lx + lw / 3, yy, r, c );
		if (r == row)
		{
			if (top < 0)
				top = yy;
			bottom = yy;
		}
	}
	if (top < 0)
		return FALSE;
	x = lx; y = top; width = lw; height = bottom - top + 1;
	return TRUE;
}

/// A combo box's list opened or shut as its drop-down button does it (the combo's own GBM_SELECTED from that button),
/// which chooses nothing.  Not a click: B's shut and A's open again came as two clicks within the double-click time,
/// which the combo took for no toggle at all, and the D-pad then left the combo with its list shut
void toggleList( GameWindow *combo )
{
	GameWindow *button = GadgetComboBoxGetDropDownButton( combo );
	if (button != NULL && TheWindowManager != NULL)
		TheWindowManager->winSendSystemMsg( combo, GBM_SELECTED, (WindowMsgData)button, button->winGetWindowId() );
}

/// The combo box beside combo that way (dx), in its row (overlapping it top to bottom), the nearest; NULL for none
GameWindow *comboBeside( const std::vector<GameWindow *> &widgets, GameWindow *combo, Int dx )
{
	Int cx, cy, cw, ch;
	rectOf( combo, cx, cy, cw, ch );
	GameWindow *best = NULL;
	Int bestGap = 0;
	for (size_t i = 0; i < widgets.size(); ++i)
	{
		GameWindow *w = widgets[i];
		if (w == combo || !(w->winGetStyle() & GWS_COMBO_BOX))
			continue;
		Int x, y, width, height;
		rectOf( w, x, y, width, height );
		if (y >= cy + ch || y + height <= cy)
			continue;		// not in its row
		const Int gap = dx > 0 ? x - (cx + cw) : cx - (x + width);
		if (gap < 0)
			continue;		// not that way
		if (best == NULL || gap < bestGap)
		{
			best = w;
			bestGap = gap;
		}
	}
	return best;
}

/// The pad's highlight in a combo box's open list, begun on the combo box's choice the first time it is asked
Int padListRow( GameWindow *combo )
{
	if (thePadList.comboId != combo->winGetWindowId())
	{
		Int pos = -1;
		GadgetComboBoxGetSelectedPos( combo, &pos );
		thePadList.comboId = combo->winGetWindowId();
		thePadList.highlight = pos < 0 ? 0 : pos;
	}
	return thePadList.highlight;
}

/// "GAMEPAD LIST: <combo> <what>, highlight N, choice N of N", and while the list is open where the highlight's row
/// is ("at x,y wxh"), which a check's mouse clicks to make the same pick
void padListLog( GameWindow *combo, const char *what )
{
	Int pos = -1;
	GadgetComboBoxGetSelectedPos( combo, &pos );
	GameWindow *list = openList( combo );
	Int x = 0, y = 0, width = 0, height = 0;
	if (list != NULL && rowRect( list, thePadList.highlight, x, y, width, height ))
		DEBUG_LOG(( "GAMEPAD LIST: %s %s, highlight %d, choice %d of %d, at %d,%d %dx%d\n", nameOf( combo ), what,
			thePadList.highlight, pos, GadgetComboBoxGetLength( combo ), x, y, width, height ));
	else
		DEBUG_LOG(( "GAMEPAD LIST: %s %s, highlight %d, choice %d of %d\n", nameOf( combo ), what, thePadList.highlight, pos,
			GadgetComboBoxGetLength( combo ) ));
}

/// One of the hint bar's buttons: its glyph, then its word - a label from the string table, or the text of the
/// button that is its owner
struct HintItem { Int button; const char *label; GameWindow *owner; };
const Int MAX_HINTS = 8;

/// The hint bar: the buttons that do something here, right to left from the bottom right corner
void drawHints( const HintItem *items, Int count )
{
	static DisplayString *glyphs[ MAX_HINTS ] = { NULL }, *words[ MAX_HINTS ] = { NULL };
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
			: (TheGameText != NULL && items[i].label[0] != 0 ? TheGameText->fetch( items[i].label ) : UnicodeString::TheEmptyString);
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
		x -= (i > 0 && items[i - 1].owner == NULL && items[i - 1].label[0] == 0) ? 4 : 18;	// a pair's first glyph sits close
	}
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
	// pressed as the transition ends, or HOLD_MS after the press at the latest: the main menu starts a side's logo
	// transition as its pane's ends, so the handler can stay busy past the moment a click works again (seen on
	// an M3 Pro: the held B pressed at the cap went through).  Wall time, not frames: the Steam Deck draws 60 to 90 frames a
	// second, where 45 frames would come before EA's second-long lock ends
	// ...and HOLD_FRAMES too, whichever comes later: the shell's transitions step once a frame, so on a loaded or
	// slow machine the lock outlasts 1.5 s (seen on a loaded Linux worker: pressed at 1.5 s, 34 frames, dropped)
	const Bool running = TheTransitionHandler != NULL && !TheTransitionHandler->isFinished();
	if (running && (now - theHeld.atMs < (UnsignedInt)HOLD_MS || clientFrame() - theHeld.atFrame < (UnsignedInt)HOLD_FRAMES))
		return;
	const Action action = theHeld.action;
	theHeld.held = FALSE;
	DEBUG_LOG(( "GAMEPAD HELD: %s pressed now, %u frames (%u ms) after the press, %s\n", actionName( action ),
		clientFrame() - theHeld.atFrame, now - theHeld.atMs, running ? "at the cap" : "the transition over" ));
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
	static UnsignedInt lastSignature = 0, sameSince = 0, sameSinceFrame = 0;
	Screen screen;
	UnsignedInt signature = 2166136261u;		// FNV-1a over the screen, the focus, and each widget's id and rectangle
	std::vector<GameWindow *> widgets;
	// a screen with nothing to focus yet (the shell still loading, a pane not yet shown) is not a menu to press on
	// the cheat panel: its keys as drawn, and the focused one's click
	std::vector<Box> keys;
	std::vector<std::string> clicks;
	const Bool shown = screenNow( screen );
	const Bool up = shown && (screen.panel ? panelKeys( keys, clicks ) >= 0 : (focusables( screen, widgets ), !widgets.empty()));
	if (up)
	{
		for (size_t i = 0; i < screen.key.size(); ++i)
			signature = (signature ^ (UnsignedInt)(unsigned char)screen.key[i]) * 16777619u;
		signature = (signature ^ (UnsignedInt)theFocusId) * 16777619u;
		for (size_t i = 0; i < thePanelFocus.size() && screen.panel; ++i)
			signature = (signature ^ (UnsignedInt)(unsigned char)thePanelFocus[i]) * 16777619u;
		for (size_t i = 0; i < widgets.size(); ++i)
		{
			Int x, y, width, height;
			rectOf( widgets[i], x, y, width, height );
			const Int parts[5] = { widgets[i]->winGetWindowId(), x, y, width, height };
			for (Int k = 0; k < 5; ++k)
				signature = (signature ^ (UnsignedInt)parts[k]) * 16777619u;
		}
		for (size_t i = 0; i < keys.size(); ++i)
		{
			const Int parts[4] = { keys[i].left, keys[i].top, keys[i].right, keys[i].bottom };
			for (Int k = 0; k < 4; ++k)
				signature = (signature ^ (UnsignedInt)parts[k]) * 16777619u;
		}
	}
	const UnsignedInt now = Clock_Milliseconds();
	// a press still held for the transition's end has not been answered yet: the menu is not standing still (at
	// 10 frames a second the hold's 45 frames outlast a script's 2-second wait, and a press made then was dropped)
	if (!up || signature != lastSignature || theHeld.held)
	{
		lastSignature = signature;
		sameSince = now;
		sameSinceFrame = clientFrame();
		return FALSE;
	}
	// the menus ignore a press while a transition runs (MainMenu.cpp's dontAllowTransitions), even with every
	// widget in its place; one pane's transition never reports finished, so a long enough stillness stands in
	// counted in frames as well as time: the shell's transitions step once a frame, so a slow machine (or a loaded
	// worker) stands still in wall time between their steps (seen at 5 frames a second: presses dropped after a
	// second of stillness)
	const UnsignedInt STILL_FRAMES = 30, TRANSITION_GIVE_UP_FRAMES = 90;
	const UnsignedInt frames = clientFrame() - sameSinceFrame;
	const Bool moving = (isMainMenu( screen ) ? !MainMenuTakesPresses()
			: (TheTransitionHandler != NULL && !TheTransitionHandler->isFinished()))
		|| (TheShell != NULL && !TheShell->isAnimFinished());
	if (moving)
		return frames >= TRANSITION_GIVE_UP_FRAMES;
	return now - sameSince >= stillMs && frames >= STILL_FRAMES;
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

Int GamepadFocus::pickNeighbourBox( const Box *boxes, Int count, const Box &from, Int dx, Int dy, Int lane )
{
	const Int fromX = (from.left + from.right) / 2, fromY = (from.top + from.bottom) / 2;
	const Bool across = dx != 0;		// left or right: the lane is a row's y; up or down, a column's x
	Int best = -1, bestScore = 0, bestTie = 0;
	for (Int i = 0; i < count; ++i)
	{
		const Box &b = boxes[i];
		if (b.left == from.left && b.top == from.top && b.right == from.right && b.bottom == from.bottom)
			continue;		// the box it starts from
		// wholly beyond the start's centre that way: a box beside it, overlapping, is up or down of it, not left
		const Bool beyond = dx > 0 ? b.left >= fromX : dx < 0 ? b.right <= fromX : dy > 0 ? b.top >= fromY : b.bottom <= fromY;
		if (!beyond)
			continue;
		const Int x = (b.left + b.right) / 2, y = (b.top + b.bottom) / 2;
		const Int along = across ? (x - fromX) * dx : (y - fromY) * dy;
		const Int edges = dx > 0 ? b.left - from.right : dx < 0 ? from.left - b.right : dy > 0 ? b.top - from.bottom : from.top - b.bottom;
		const Int gap = edges < 0 ? 0 : edges;
		// how far the box is beside the way: 0 when it overlaps the start's own span (it is straight that way) or the
		// lane passes through it; else the nearer of the two.  The cone: no further aside than the box is along
		const Int low = across ? b.top : b.left, high = across ? b.bottom : b.right;
		const Int fromLow = across ? from.top : from.left, fromHigh = across ? from.bottom : from.right;
		const Int offLane = lane < low ? low - lane : (lane > high ? lane - high : 0);
		const Int offSpan = high < fromLow ? fromLow - high : (low > fromHigh ? low - fromHigh : 0);
		const Int off = offLane < offSpan ? offLane : offSpan;
		if (off > along)
			continue;		// outside the 45 degree cone
		if (off > 0 && edges < 0)
			continue;		// out of line and overlapping the focus along the way: above or below it, not beside (or the reverse)
		// a box in line wins over every box beside the line, however much nearer: up and down keep the column and
		// left and right the row, across a gap (a command card's empty place) or down to the page's buttons below a
		// shorter column (Options' Controls page: from its middle column's last box down to Cancel, not across to
		// the next column's last), and only where nothing is in line does the step go aside, a step aside costing
		// three along; among boxes as good, the lane's (the remembered column or row) first, then the nearest to it
		const Int NOT_IN_LINE = 1 << 20;
		const Int score = gap + 3 * off + (off > 0 ? NOT_IN_LINE : 0);
		const Int sideways = abs( (across ? y : x) - lane );
		const Int tie = offLane * 65536 + sideways;
		if (best < 0 || score < bestScore || (score == bestScore && tie < bestTie))
		{
			best = i;
			bestScore = score;
			bestTie = tie;
		}
	}
	return best;
}

Int GamepadFocus::pickNeighbour( const ICoord2D *centres, Int count, Int x, Int y, Int dx, Int dy )
{
	std::vector<Box> boxes( count > 0 ? count : 0 );
	for (Int i = 0; i < count; ++i)
	{
		boxes[i].left = boxes[i].right = centres[i].x;
		boxes[i].top = boxes[i].bottom = centres[i].y;
	}
	const Box from = { x, y, x, y };
	return count > 0 ? pickNeighbourBox( &boxes[0], count, from, dx, dy, dx != 0 ? y : x ) : -1;
}

Bool GamepadFocus::act( Action action )
{
	Screen screen;
	if (!screenNow( screen ))
		return FALSE;
	if (screen.panel)
		return actOnPanel( action );
	if ((action == ACCEPT_DOWN || action == BACK) && !getenv( "ZH_TEST_NO_HOLD" ) && shellLocked())
	{
		theHeld.held = TRUE;
		theHeld.action = action;
		theHeld.screen = screen.key;
		theHeld.atFrame = clientFrame();
		theHeld.atMs = Clock_Milliseconds();
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
	if (!dropdownOpen)
		thePadList.comboId = 0;		// a list shut (by a pick, B, the mouse, or the screen going) begins afresh when opened

	switch (action)
	{
		case NAV_UP: case NAV_DOWN: case NAV_LEFT: case NAV_RIGHT:
		{
			const Bool vertical = action == NAV_UP || action == NAV_DOWN;
			const UnsignedByte key = action == NAV_UP ? KEY_UP : action == NAV_DOWN ? KEY_DOWN : action == NAV_LEFT ? KEY_LEFT : KEY_RIGHT;
			// an open combo's list is the pad's: up and down move its highlight (clamped, the list scrolled to it), and
			// left and right do nothing, the list staying open; nothing is chosen until A
			if (dropdownOpen)
			{
				if (vertical)
				{
					const Int length = GadgetComboBoxGetLength( focus );
					Int row = padListRow( focus ) + (action == NAV_DOWN ? 1 : -1);
					row = row < 0 ? 0 : (row >= length ? length - 1 : row);
					thePadList.highlight = row;
					showRow( dropdown, row );
					padListLog( focus, "moved" );
				}
				return TRUE;
			}
			// a list, a slider: the direction is theirs while it moves something
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
			// a closed combo box: left and right step its choice, as a player's pick from its list does (GCM_SELECTED) -
			// unless another combo box stands beside it in its row (skirmish setup's player, colour, army and team), where
			// they move along the row, and A's list changes it: else no D-pad could reach the rest of that row
			const Bool isCombo = focus != NULL && (focus->winGetStyle() & GWS_COMBO_BOX);
			if (isCombo && !vertical && !dropdownOpen && comboBeside( widgets, focus, action == NAV_RIGHT ? 1 : -1 ) == NULL)
			{
				Int pos = -1;
				GadgetComboBoxGetSelectedPos( focus, &pos );
				const Int next = pos + (action == NAV_RIGHT ? 1 : -1);
				if (next >= 0 && next < GadgetComboBoxGetLength( focus ))
				{
					GadgetComboBoxSetSelectedPos( focus, next );
					DEBUG_LOG(( "GAMEPAD LIST: %s stepped, choice %d of %d\n", nameOf( focus ), next, GadgetComboBoxGetLength( focus ) ));
				}
				return TRUE;
			}
			if (focus == NULL)
				return TRUE;
			std::vector<ICoord2D> centres;
			std::vector<Box> boxes;
			for (size_t i = 0; i < widgets.size(); ++i)
			{
				centres.push_back( centreOf( widgets[i] ) );
				Int x, y, width, height;
				rectOf( widgets[i], x, y, width, height );
				const Box box = { x, y, x + width - 1, y + height - 1 };
				boxes.push_back( box );
			}
			const ICoord2D from = centreOf( focus );
			Int fx, fy, fw, fh;
			rectOf( focus, fx, fy, fw, fh );
			const Box fromBox = { fx, fy, fx + fw - 1, fy + fh - 1 };
			const Int dx = action == NAV_LEFT ? -1 : action == NAV_RIGHT ? 1 : 0;
			const Int dy = action == NAV_UP ? -1 : action == NAV_DOWN ? 1 : 0;
			// the lanes: the column kept while going up and down, the row while going left and right, remembered
			// through a shorter row or column, and taken afresh from the focus when it moved by other means
			if (focus->winGetWindowId() != theLaneFocus)
			{
				theLaneX = from.x;
				theLaneY = from.y;
			}
			Int next = pickNeighbourBox( &boxes[0], (Int)boxes.size(), fromBox, dx, dy, vertical ? theLaneX : theLaneY );
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
			{
				setFocus( widgets[ next ] );
				if (vertical)
					theLaneY = centres[ next ].y;
				else
					theLaneX = centres[ next ].x;
				theLaneFocus = widgets[ next ]->winGetWindowId();
			}
			return TRUE;
		}

		case ACCEPT_DOWN:
		case ACCEPT_UP:
		{
			if (focus == NULL)
				return TRUE;
			if (dropdownOpen)
			{
				// the pick: the highlight chosen as a click on its row chooses it (GLM_SELECTED, the screen's GCM_SELECTED),
				// which shuts the list; the choice it already had only shuts it
				if (action == ACCEPT_DOWN)
				{
					const Int row = padListRow( focus );
					Int pos = -1;
					GadgetComboBoxGetSelectedPos( focus, &pos );
					if (row != pos)
						GadgetComboBoxSetSelectedPos( focus, row );
					else
						toggleList( focus );
					padListLog( focus, "picked" );
					thePadList.comboId = 0;
				}
				return TRUE;
			}
			if (isList)
			{
				if (action == ACCEPT_DOWN)
					keyTo( focus, KEY_ENTER );
				return TRUE;
			}
			if (focus->winGetStyle() & GWS_COMBO_BOX)
			{
				// a shut combo box: A opens its list, the highlight on its choice
				if (action == ACCEPT_DOWN)
				{
					toggleList( focus );
					GameWindow *opened = openList( focus );
					if (opened != NULL)
						showRow( opened, padListRow( focus ) );
					padListLog( focus, "opened" );
				}
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
				toggleList( focus );		// shut, and the choice is the one it had
				padListLog( focus, "shut, nothing chosen" );
				thePadList.comboId = 0;
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
			if (dropdownOpen)
			{
				// in an open list the shoulders page, a screenful of rows, as the triggers do: the tabs wait
				act( action == TAB_PREV ? PAGE_UP : PAGE_DOWN );
				return TRUE;
			}
			std::vector<GameWindow *> tabs;
			const Int chosen = tabButtons( screen, tabs );
			if (tabs.empty())
				return FALSE;
			Int next = chosen < 0 ? 0 : chosen + (action == TAB_NEXT ? 1 : -1);
			next = next < 0 ? 0 : (next >= (Int)tabs.size() ? (Int)tabs.size() - 1 : next);
			press( tabs[ next ] );
			theFocusId = 0;		// the new page's own first focus, found on the next action
			theLastFocus.erase( screen.key );
			theScreenKey.clear();
			// and not before the page has changed: the tab's press takes effect later, and a default picked now
			// would be the old page's first widget
			theAwaitingPage = TRUE;
			theAwaitingSignature = widgetSignature( widgets );
			theAwaitingSince = Clock_Milliseconds();
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
			if (dropdownOpen)
			{
				const Int length = GadgetComboBoxGetLength( focus );
				Int page = GadgetListBoxGetBottomVisibleEntry( dropdown ) - GadgetListBoxGetTopVisibleEntry( dropdown ) + 1;
				page = page < 1 ? 1 : page;
				Int row = padListRow( focus ) + (action == PAGE_DOWN ? page : -page);
				row = row < 0 ? 0 : (row >= length ? length - 1 : row);
				thePadList.highlight = row;
				showRow( dropdown, row );
				padListLog( focus, "paged" );
				return TRUE;
			}
			GameWindow *list = isList ? focus : NULL;
			for (Int i = 0; list != NULL && i < 8; ++i)
				keyTo( list, action == PAGE_UP ? KEY_UP : KEY_DOWN );
			return list != NULL;
		}
	}
	return FALSE;
}

void GamepadFocus::draw( void )
{
	if (TheGameLogic != NULL && TheGameLogic->isInGame() && TheGameLogic->isGamePaused())
		++thePausedFrames;
	if (!thePadDriving || TheDisplay == NULL)
		return;
	Screen screen;
	if (!screenNow( screen ) || screen.panel)
		return;		// the cheat panel's frame and hints go over the panel (drawOverConsole)
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

	// an open combo box's list: the pad's highlight on its row (the list box's own selection is the choice, unchanged)
	GameWindow *list = openList( focus );
	if (list == NULL)
		thePadList.comboId = 0;
	Int rx, ry, rw, rh;
	if (list != NULL && rowRect( list, padListRow( focus ), rx, ry, rw, rh ))
	{
		TheDisplay->drawFillRect( rx, ry, rw, rh, GameMakeColor( 255, 210, 60, 70 ) );
		TheDisplay->drawOpenRect( rx, ry, rw, rh, 1.0f, GameMakeColor( 255, 210, 60, 255 ) );
	}
	// and no tooltip over it: the pad's pointer rests on the combo box, whose tooltip covered the list's rows (the
	// mouse draws it after this, from what is set now)
	if (list != NULL && TheMouse != NULL)
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
	// a closed combo box whose left and right change it (none beside it in its row): the prompt says so
	const Bool closedCombo = list == NULL && focus != NULL && (focus->winGetStyle() & GWS_COMBO_BOX)
		&& comboBeside( widgets, focus, 1 ) == NULL && comboBeside( widgets, focus, -1 ) == NULL;

	// the hint bar: the buttons that do something here, bottom right
	HintItem items[ MAX_HINTS ];
	Int count = 0;
	// a closed combo box: left and right change it in place (the word after the pair of glyphs)
	if (closedCombo)
	{
		items[ count++ ] = { GAMEPAD_BUTTON_DPAD_LEFT, "", NULL };
		items[ count++ ] = { GAMEPAD_BUTTON_DPAD_RIGHT, "GUI:GamepadChange", NULL };
	}
	items[ count++ ] = { GAMEPAD_BUTTON_SOUTH, "GUI:GamepadSelect", NULL };
	items[ count++ ] = { GAMEPAD_BUTTON_EAST, "GUI:GamepadBack", NULL };
	// an open list: only its own two, a pick and a way out
	GameWindow *xButton = list == NULL ? secondaryButton( widgets, FALSE ) : NULL;
	GameWindow *yButton = list == NULL ? secondaryButton( widgets, TRUE ) : NULL;
	if (xButton != NULL)
		items[ count++ ] = { GAMEPAD_BUTTON_WEST, NULL, xButton };
	if (yButton != NULL)
		items[ count++ ] = { GAMEPAD_BUTTON_NORTH, NULL, yButton };
	if (list == NULL && byNameTail( widgets, theStartNames, GWS_PUSH_BUTTON ) != NULL)
		items[ count++ ] = { GAMEPAD_BUTTON_START, "GUI:GamepadStart", NULL };
	std::vector<GameWindow *> tabs;
	tabButtons( screen, tabs );
	if (list == NULL && !tabs.empty())
		items[ count++ ] = { GAMEPAD_BUTTON_RIGHT_SHOULDER, "GUI:GamepadTabs", NULL };

	drawHints( items, count );
}

void GamepadFocus::drawOverConsole( void )
{
	if (!thePadDriving || TheDisplay == NULL)
		return;
	Screen screen;
	if (!screenNow( screen ) || !screen.panel)
		return;
	std::vector<Box> keys;
	std::vector<std::string> clicks;
	const Int focus = panelKeys( keys, clicks );
	if (focus >= 0)
	{
		const Box &box = keys[ focus ];
		TheDisplay->drawOpenRect( box.left - 3, box.top - 3, box.right - box.left + 7, box.bottom - box.top + 7, 2.0f,
			GameMakeColor( 255, 210, 60, 255 ) );
	}
	const HintItem items[] = { { GAMEPAD_BUTTON_SOUTH, "GUI:GamepadSelect", NULL }, { GAMEPAD_BUTTON_EAST, "GUI:GamepadBack", NULL } };
	drawHints( items, 2 );
}
