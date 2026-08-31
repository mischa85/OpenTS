/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The stock control classes the WebAssembly target's dialogs are built out of.
//
// ownrdraw.cpp registers exactly one window class of its own; every other control in the
// front end is a stock Windows class that it subclasses and paints over. What it supplies
// is paint, not state: it asks a real trackbar where its thumb belongs, a real button
// whether it is checked, and a real list box what its items say. So the classes here are
// the models behind that paint -- item lists, selections, check states and ranges, driven
// by the same messages Windows drives them with -- and they draw nothing themselves.
//
// win32user.cpp owns the windows and the message dispatch these run on, and creates them
// from dialog templates.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "win32compat.h"


// Registers every stock control class. Later calls do nothing, and the dialog manager
// calls this before it builds a dialog.
void Win32_Register_Stock_Controls(void);

// The class name a dialog template's ordinal names, or NULL when the ordinal names no
// stock class. A template writes the six original control classes as ordinals rather than
// as strings.
char const * Win32_Stock_Control_Class(unsigned int ordinal);

#endif	// __EMSCRIPTEN__
