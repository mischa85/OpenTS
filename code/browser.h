/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The page the WebAssembly build runs inside, seen from the engine. A tab has no window,
// no message queue, and no thread the engine may keep, so this stands in for all three:
// the canvas is the drawing target, the page's own event callbacks fill the keyboard and
// mouse queues, and Browser_Yield hands the thread back so the page stays alive.
//
// Only browser.cpp reaches for Emscripten's headers, in the way bgfxbackend.cpp is the
// only translation unit that sees bgfx, so nothing else in the tree needs them.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "win.h"

class Mouse;


// How the page was asked to size the game's frame. A browser build has no settings file
// that survives the tab, so the query string is where it is configured: "?display=native"
// -- the default -- makes the window's own size the resolution, "?display=scaled" keeps
// the configured resolution and lets the presenter scale it into the window, and
// "?display=1024x768" pins that resolution and scales it.
enum BrowserDisplayPolicy {
	BROWSER_DISPLAY_NATIVE,
	BROWSER_DISPLAY_SCALED,
};

BrowserDisplayPolicy Browser_Display_Policy(void);

// The resolution "?display=WIDTHxHEIGHT" named, or zero when the query string named none.
int Browser_Display_Width(void);
int Browser_Display_Height(void);


// The canvas the page lays out for the game, named the way the renderer and the event
// callbacks both want it.
char const * Browser_Canvas_Selector(void);

bool Browser_Init(void);

// Picks up whatever the page has changed since the last pass -- the canvas size, the
// visibility, and the input events that arrived while the engine was elsewhere -- and
// delivers it to the engine. Cheap enough to call from every wait the engine has.
//
// The touch gestures are carried here too, so this has to be reached regularly rather than
// only when something arrives: a finger resting still reports nothing at all, and the press
// that opens a rubber band is measured against a clock this reads.
void Browser_Service(void);

// Taps the events Browser_Service drains, before they reach the engine's keyboard buffer.
// The position is the one the event carried, in the game's frame, and means nothing for a
// key. A hook that answers true has delivered the event itself and the keyboard buffer does
// not see it; anything it leaves is posted as it always was. Only the platform layer sets
// one, and only so that a press and a release arriving between two engine passes are both
// carried rather than read back from the state they leave behind.
typedef bool (*BrowserEventHook)(unsigned short key, int x, int y, bool is_mouse, bool is_release);
void Browser_Set_Event_Hook(BrowserEventHook hook);

/*
 * The canvas measured two ways. The drawing buffer is in device pixels, which is what the
 * renderer draws into and what a window position means; the laid out box is in CSS pixels,
 * which is the size the page and the player see. They differ by the device pixel ratio,
 * and the game's frame is sized in the second of them: a fixed width sidebar measured in
 * device pixels would halve on the display that has two of them per CSS pixel.
 */
int Browser_Canvas_Width(void);
int Browser_Canvas_Height(void);
int Browser_Canvas_CSS_Width(void);
int Browser_Canvas_CSS_Height(void);

// The display the page is on, in CSS pixels, or zero when the page will not say. It bounds
// what the display options offer, since a mode larger than the screen cannot be shown.
int Browser_Screen_Width(void);
int Browser_Screen_Height(void);

// Is the page not being shown? The browser stops compositing a hidden tab and stops
// scheduling animation frames for it, which is its own expression of GameInFocus.
bool Browser_Is_Hidden(void);

// Hands the thread back to the page and returns when it schedules the engine again.
// Browser_Yield always waits; Browser_Yield_If_Due waits only once an animation frame's
// worth of time has passed, so that a routine reached thousands of times between frames
// does not cost a frame each time.
void Browser_Yield(void);
bool Browser_Yield_If_Due(void);

// Counts the waits the engine has reached that only the yield scaffold carries. It is the
// number docs/WASM-PORT.md D.4 asks be published and driven down, so a build without the
// scaffold still counts them rather than pretending they are gone.
unsigned int Browser_Blocking_Wait_Count(void);
bool Browser_Yield_Is_Available(void);

// Rises once per animation frame the engine has been given, so that presentation happens
// at most once per frame however often the engine asks.
unsigned int Browser_Frame_Serial(void);

// The keyboard state Windows would keep for the engine. The browser reports the modifiers
// with each event instead, so the platform layer holds them.
unsigned short Browser_Key_Modifiers(void);
bool Browser_Key_Is_Down(unsigned short vk_key);
char Browser_Key_To_ASCII(unsigned short key);

// Where the cursor is, in the game's own frame. A page has no cursor to ask after, so the
// position is whatever the last event over the canvas reported.
int Browser_Mouse_X(void);
int Browser_Mouse_Y(void);

// The mouse the engine drives on a page. It is a WWMouseClass that reads its position
// from the canvas rather than from an operating system that has one.
Mouse * Browser_Create_Mouse(HWND window);

#endif	// __EMSCRIPTEN__
