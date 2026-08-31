/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The native defaults for the yield trio the browser page supplies on the WebAssembly
// target. A wait here really waits, one scheduler quantum at a time, so a cooperative
// sleep behaves like an ordinary one; a host with a window replaces these with a pump
// of its own by defining the three symbols itself.

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

#endif
