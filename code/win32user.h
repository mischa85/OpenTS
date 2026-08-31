/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The window manager the WebAssembly target runs on. A page has no USER32, and the engine
// wants one: the front end in ownrdraw.cpp is built out of window classes, window
// procedures and messages, and the main window is what the rest of the engine passes
// around to say the game exists. So the windows here are real objects with real message
// dispatch, drawn by whoever owns them onto the engine's own surfaces rather than by any
// device context.
//
// win32compat.h declares the entry points; win32user.cpp implements them. What is declared
// here is only the part of the window manager that other platform sources need.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "win32compat.h"


// Turns what the page has reported since the last pass into window messages and queues
// them. Windows_Message_Handler calls this before it pumps the queue.
void Win32_User_Service(void);

// Where the registry has a window. The rectangles are in the same space the engine puts
// its controls in -- window pixels, with no non-client frame -- and a window with no
// parent is at its own screen position.
//
// GetWindowRect, GetClientRect, ClientToScreen and ScreenToClient are implemented in
// win32window.cpp against the canvas. They must answer out of these for any handle the
// registry knows, because the registry is the only thing that knows where a child window
// is; a canvas-sized answer makes every control cover the whole frame.
bool Win32_User_Window_Rect(HWND window, RECT * rect);
bool Win32_User_Client_Rect(HWND window, RECT * rect);
bool Win32_User_Client_Origin(HWND window, POINT * origin);

#endif	// __EMSCRIPTEN__
