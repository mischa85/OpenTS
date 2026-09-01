/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The native defaults for the host contract browser.h states, for a process with no
// window: the harnesses, and any headless run. A wait here really waits, one scheduler
// quantum at a time, so a cooperative sleep behaves like an ordinary one, and every
// measurement answers zero. A host with a window supplies the same symbols itself and
// is linked instead of this.

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include <ctime>


void Browser_Yield(void)
{
	struct timespec wait;
	wait.tv_sec = 0;
	wait.tv_nsec = 1000000L;
	nanosleep(&wait, nullptr);
}


bool Browser_Yield_If_Due(void)
{
	Browser_Yield();
	return(true);
}


bool Browser_Yield_Is_Available(void)
{
	return(true);
}


int Browser_Mouse_X(void) { return(0); }
int Browser_Mouse_Y(void) { return(0); }
bool Browser_Mouse_Is_Hovering(void) { return(false); }
unsigned short Browser_Key_Modifiers(void) { return(0); }
bool Browser_Key_Is_Down(unsigned short) { return(false); }
char Browser_Key_To_ASCII(unsigned short) { return('\0'); }
void Browser_Set_Window_Mode(bool, int, int) {}
int Browser_Canvas_Width(void) { return(0); }
int Browser_Canvas_Height(void) { return(0); }
int Browser_Canvas_CSS_Width(void) { return(0); }
int Browser_Canvas_CSS_Height(void) { return(0); }
int Browser_Screen_Width(void) { return(0); }
int Browser_Screen_Height(void) { return(0); }
char const * Browser_Canvas_Selector(void) { return(""); }
void Host_Apply_Cursor(unsigned int const *, int, int, int, int) {}
void * Host_Native_Window_Handle(void) { return(nullptr); }
void * Host_Native_Display_Handle(void) { return(nullptr); }

#endif
