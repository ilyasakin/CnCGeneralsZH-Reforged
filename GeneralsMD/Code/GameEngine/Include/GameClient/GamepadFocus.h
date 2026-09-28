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

// GamepadFocus.h: menus on a gamepad, console-style (G1, docs/mac-port/tasks/G1-gamepad-screens.md).
//
// While a menu screen, a popup or a box is up, the pad does not move a cursor: it moves a focus from widget to
// widget, and acts on the focused one.
//   - The screen is the top of the modal stack, or else the shell's top layout.
//   - Its focusable widgets are its shown, enabled push buttons, check boxes, radio buttons, combo boxes,
//     sliders, list boxes and text entries.
//   - The D-pad (and the left stick, with repeat) moves to the nearest of them that way.  A list or a slider
//     takes the direction first when it can use it (the next row, the next step), as it would a key.
//   - Focusing puts the game's pointer on the widget, where the cursor is not drawn: so the widget shows its
//     hover state and its tooltip as for a mouse.  A presses there, as a left click does.  Lists take Enter.
//   - B presses the screen's Back or Cancel (or No), else it is Escape, which the shell's menus take as back.
//     Start presses the screen's go button (Start, Accept, OK...).  LB and RB press the tab buttons left and
//     right of the chosen one.
//   - A frame around the focus and a hint bar along the bottom (the pad's own glyphs) are drawn over the GUI.
// All of this is GUI only: nothing here reaches the game logic but what a click or a key already sends.
// The device layer (SdlGamepad) feeds it through act() and gives it the pointer and the keys through Hooks.

#pragma once

#ifndef __GAMEPADFOCUS_H
#define __GAMEPADFOCUS_H

#include "Lib/BaseType.h"

class GameWindow;

class GamepadFocus
{
public:
	enum Action
	{
		NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
		ACCEPT_DOWN, ACCEPT_UP,		///< A: pressed and released
		BACK, START, TAB_PREV, TAB_NEXT, PAGE_UP, PAGE_DOWN,
		ALT_X, ALT_Y		///< X and Y: the screen's secondary buttons (delete, defaults, save, host...)
	};

	/// What the device does for the focus: put the pointer on a pixel, press or release the left button there,
	/// press or release a key
	struct Hooks
	{
		void (*pointTo)( Int x, Int y );
		void (*leftButton)( Bool down );
		void (*key)( UnsignedByte dik, Bool down );
	};
	static void setHooks( const Hooks &hooks );

	/// TRUE while a menu screen, a popup or a box is up: the pad navigates it instead of playing
	static Bool isActive( void );

	/// One of the pad's menu actions; TRUE when it was taken
	static Bool act( Action action );

	/** A pressed or B pressed while the shell is in a transition it began under HOLD_MS ago (EA's menus drop a
		* press then: a pane's buttons scaling in, and MainMenu.cpp's dontAllowTransitions, about a second after a pane
		* opens) is held, and pressed when the transition ends, or HOLD_MS after the press at the latest; dropped
		* if the screen changes first or the pad moves the focus meanwhile.  Only the pad's presses: a mouse and the keys keep EA's behaviour.  The
		* pad's layer calls this every update while a menu has the pad. */
	static void update( void );
	enum { HOLD_MS = 1500 };

	/// The pad drives the menus (TRUE) or a hand does (FALSE): the frame and the hints show, and the cursor
	/// is not drawn, only while a pad does
	static void setPadDriving( Bool driving );
	static Bool hidesCursor( void );

	/// The focus frame and the hint bar, over the GUI and under the cursor (W3DDisplay::draw)
	static void draw( void );

	/// The focused widget, or NULL: for the tests
	static GameWindow *getFocus( void );

	/** TRUE once a menu is up and its screen, its focus and every focusable widget's place have stood still for
		* stillMs: the input script's "s" lines wait for it, so a scripted press follows what the menu has done,
		* not the clock (a pane still sliding in, or a focus change not yet made, under a loaded machine) */
	static Bool isSettled( UnsignedInt stillMs );

	/// How many times the focus has moved (each "GAMEPAD FOCUS" line): the input script sees whether a press took
	static UnsignedInt focusChanges( void );

	/// A widget's rectangle on the screen, edges inclusive
	struct Box { Int left, top, right, bottom; };

	/** Where the D-pad goes from the box from, in the direction (dx, dy) (one of them 0, the other 1 or -1): of the
		* boxes wholly beyond its centre that way and inside the direction's 45 degree cone, the nearest, edge to edge,
		* with each pixel the lane passes beside a box counting three - the lane being the remembered column's x for
		* up and down, the row's y for left and right.  -1 when none.  So grids go by rows and columns, a box in the
		* lane is never skipped for a diagonal one as near, a shorter row's wide box a short way off is passed
		* through, and nothing outside the cone is picked. */
	static Int pickNeighbourBox( const Box *boxes, Int count, const Box &from, Int dx, Int dy, Int lane );

	/// pickNeighbourBox over points (the command bar's centres), the lane through (x, y)
	static Int pickNeighbour( const ICoord2D *centres, Int count, Int x, Int y, Int dx, Int dy );
};

#endif // __GAMEPADFOCUS_H
