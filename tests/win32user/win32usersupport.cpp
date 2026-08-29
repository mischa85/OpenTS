/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What the window manager reaches outside itself, for a harness that runs it on its own.
//
// win32user.cpp asks the page where the pointer is, asks the video layer how the frame is
// scaled onto the canvas, and asks USER32 who holds the mouse capture. In the engine those
// answers come from browser.cpp, vidscale.cpp and win32window.cpp, none of which stands up
// without the renderer behind it. Here they are the quiet answers a harness that drives
// the window manager directly needs: no pointer, no scaling, no capture.
//
// This file is outside code/ so that the recursive glob building OpenTS cannot pick it up.

#if defined(__EMSCRIPTEN__)

#include "win32compat.h"

#include <cstdarg>
#include <cstdio>


void Browser_Yield(void) {}
bool Browser_Yield_If_Due(void) { return(false); }
bool Browser_Yield_Is_Available(void) { return(false); }

unsigned short Browser_Key_Modifiers(void) { return(0); }
bool Browser_Key_Is_Down(unsigned short) { return(false); }
char Browser_Key_To_ASCII(unsigned short) { return('\0'); }

int Browser_Mouse_X(void) { return(0); }
int Browser_Mouse_Y(void) { return(0); }

void Game_Point_To_Window(POINT &) {}


// The capture is nobody's, which is what makes every hit test in the harness a hit test.
HWND SetCapture(HWND) { return(nullptr); }
BOOL ReleaseCapture(void) { return(FALSE); }
HWND GetCapture(void) { return(nullptr); }


void DebugString(char const * format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

#endif	// __EMSCRIPTEN__
