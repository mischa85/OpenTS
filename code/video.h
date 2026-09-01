/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "win.h"


// How the presented frame is filtered when the window is larger than it.
enum VideoScaleMode {
	VIDEO_SCALE_NEAREST,
	VIDEO_SCALE_LINEAR,
	VIDEO_SCALE_PIXELART,
};


// Where the game's frame lands inside the window. The frame keeps its aspect ratio, so
// the destination is centered and the window may show bars on two of its sides.
struct VideoScaleInfo
{
	int GameWidth;
	int GameHeight;
	int WindowWidth;
	int WindowHeight;
	int DestX;
	int DestY;
	int DestWidth;
	int DestHeight;
	float ScaleX;
	float ScaleY;
};


bool Video_Init(HWND window);
void Video_Shutdown(void);

bool Video_Set_Mode(int width, int height);
void Video_On_Resize(int width, int height);
void Video_On_Display_Change(void);

#if !defined(_WIN32)

/*
 * A page sizes the canvas itself and resizes it whenever it likes, so the frame follows
 * the canvas rather than the other way about. Noticing that is cheap and happens wherever
 * the canvas is read; acting on it destroys and rebuilds every drawing surface the engine
 * owns, so the two are separated. Video_Service_Display is the only place the frame is
 * actually resized, and it belongs at the bottom of the message pump: the movie player and
 * the dialog loops both keep a surface across the waits they run, and neither comes
 * through there.
 */
void Video_Request_Frame_Size(int width, int height);
void Video_Service_Display(void);

// Brings a size the page reports into one the engine can render at. What the display
// options offer for the window's own size has to come through here too, or the entry
// standing for the window would not match the frame the window produces.
void Video_Clamp_Frame_Size(int & width, int & height);

// The largest frame the window is followed to where the host cannot say how large the
// display is. A host that can say is believed instead: a frame wider or taller than the
// display uploads pixels nothing can show, and every frame is a full upload of the
// surface to the renderer.
enum {
	VIDEO_FOLLOW_MAX_WIDTH = 2560,
	VIDEO_FOLLOW_MAX_HEIGHT = 1600,
};

// Narrows the frame to an aspect the shell screens were laid out for, and gives it back.
// A shell screen places its artwork once and never again, so it cannot be handed a frame
// wider than it was drawn against. Called either side of the screen counting itself up.
void Video_Enter_Shell_Frame(void);
void Video_Leave_Shell_Frame(void);

#endif

void Video_Mark_Dirty(void);
void Video_Present(void);
void Video_Present_If_Dirty(void);

VideoScaleInfo const & Video_Get_Scale_Info(void);

int * EnumDisplayModes(int minwidth, int minheight, int maxwidth, int maxheight);
