/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The renderer's private interface. Only bgfxbackend.cpp includes bgfx, so no bgfx type
// appears here and no other translation unit needs the library's headers or its build
// settings. video.cpp is the engine's only caller.

#pragma once

#ifdef __EMSCRIPTEN__

// A page has no window handle. bgfx names the canvas it draws into with a CSS selector
// instead, so the target the renderer starts on is whatever the platform can name.
using BackendWindow = char const *;

#else

#ifdef _WIN32
#include <windows.h>
#else
#include "win32compat.h"
#endif

using BackendWindow = HWND;

#endif


enum BackendRenderer {
	BACKEND_RENDERER_AUTO,
	BACKEND_RENDERER_D3D11,
	BACKEND_RENDERER_D3D12,
	BACKEND_RENDERER_VULKAN,
	BACKEND_RENDERER_OPENGL,

	// WebGL 2 is what OpenGL ES 3 becomes in a browser. Appended so that the value the
	// Renderer configuration key stores keeps its meaning.
	BACKEND_RENDERER_OPENGLES,
};


enum BackendScaleMode {
	BACKEND_SCALE_NEAREST,
	BACKEND_SCALE_LINEAR,
	BACKEND_SCALE_PIXELART,
};


bool Backend_Init(BackendWindow window, int windowwidth, int windowheight, BackendRenderer renderer, bool vsync);
void Backend_Shutdown(void);

bool Backend_Set_Frame_Size(int width, int height);
void Backend_On_Resize(int windowwidth, int windowheight);

// Uploads the frame and presents it. The pixels are 16 bit 565 and stay owned by the
// caller; they are consumed before this returns.
void Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode);

char const * Backend_Renderer_Name(void);
