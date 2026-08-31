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
#include "win32user.h"

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


/*
** Somewhere to type. In the engine this raises and dismisses the page's own keyboard;
** here it is a flag, because whether the window manager asks at the right moment is the
** part a harness can hold it to.
*/
static bool _TextInputWanted = false;

void Browser_Begin_Text_Input(void) { _TextInputWanted = true; }
void Browser_End_Text_Input(void) { _TextInputWanted = false; }

bool Test_Text_Input_Wanted(void) { return(_TextInputWanted); }


void Game_Point_To_Window(POINT &) {}


/*
** The dialog manager asks the resource layer how long a template it was handed is, so that
** a walk over one stops where the resource does. A harness builds its templates in memory
** rather than fetching them, so there is no resource and no length to report.
*/
void const * Fetch_Resource(LPCSTR, LPCSTR, unsigned int * ressize)
{
	if (ressize != nullptr) {
		*ressize = 0;
	}

	return(nullptr);
}


unsigned int Fetch_Resource_Size(void const *)
{
	return(0);
}


/*
** Where a window is. win32window.cpp answers these out of the manager's registry and falls
** back to the canvas for a handle the registry does not know; there is no canvas here, so
** an unknown handle is simply nowhere.
*/
BOOL GetClientRect(HWND window, LPRECT rect)
{
	if (rect == nullptr) {
		return(FALSE);
	}

	return(Win32_User_Client_Rect(window, rect) ? TRUE : FALSE);
}


BOOL GetWindowRect(HWND window, LPRECT rect)
{
	if (rect == nullptr) {
		return(FALSE);
	}

	return(Win32_User_Window_Rect(window, rect) ? TRUE : FALSE);
}


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
