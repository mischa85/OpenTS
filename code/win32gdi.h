/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What the WebAssembly target answers the dialog code's GDI calls with. This is not a GDI
// port: the engine already carries its own fonts, and the front end already paints
// everything else through them, so a device context here is a small record of the font, the
// text color and the background mode a caller selected, and the text it draws goes through
// the engine's remapped bitmap font.
//
// win32compat.h declares the entry points; win32gdi.cpp implements them. What is declared
// here is the one thing that has no Win32 spelling: which surface a context draws on.

#pragma once

#if !defined(_WIN32)

#include "win32compat.h"

class Surface;


/// <summary>
/// Fetches a device context that draws onto one of the engine's surfaces.
/// </summary>
/// <remarks>
/// DSurface::GetDC answers with nothing on this target, because a DSurface is backed by a
/// GDI section only where there is a GDI. A caller that wants text on a surface asks for
/// one of these instead and releases it with DeleteDC. Coordinates are the surface's own
/// pixels, which is what the caller was already passing to TextOut.
/// </remarks>
HDC Win32_GDI_Surface_DC(Surface & surface);

#endif	// __EMSCRIPTEN__
