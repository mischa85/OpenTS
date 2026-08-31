/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The window, the pointer, and the code page, answered for a page.
//
// A tab has no window and no cursor of its own, but it has a canvas: browser.cpp knows how
// big it is, and its CSS is where a pointer is expressed. That is enough for the Win32
// geometry and cursor calls the engine makes, so those are implemented here rather than
// stubbed in win32compat.cpp. The code page conversions have no browser in them at all and
// are ordinary host work.
//
// Only the cursor builder is named here. Everything else this file defines is a Win32
// entry point that win32compat.h already declares.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "win.h"


// Builds a cursor from the pixels the game drew, each 0xAARRGGBB and in rows from the top.
// The result is a handle SetCursor and DestroyCursor accept like any other.
HCURSOR Win32_Window_Create_Cursor(unsigned long const * pixels, int width, int height, int hotx, int hoty);

// The largest cursor a browser will draw. Past it the page shows no pointer at all rather
// than a clipped one, so a caller scales its image down to fit instead.
int Win32_Window_Max_Cursor_Size(void);

// The cursor that draws nothing. The null cursor cannot say this here: with no window class
// behind the canvas it falls back to the page's own pointer, so asking for no pointer at all
// needs a cursor of its own.
HCURSOR Win32_Window_Blank_Cursor(void);

#endif	// __EMSCRIPTEN__
