/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The periodic callback for a target with no thread to deliver it on. hosttimer.h declares
// what it promises; this is where it is kept, and it exists apart from win32compat.cpp
// because the implementation needs a service routine of its own rather than one more stub.
//
// What a page does not have is the timer thread. Windows arms a callback
// that arrives asynchronously, interrupting whatever the engine was doing; here the engine
// is the only thread there is, so a registered callback is not delivered until the engine
// asks for it. Win32_Timer_Service is that ask, and every guarantee the timer offers is a
// consequence of it:
//
//   - A callback runs on the engine's own stack, inside the Win32_Timer_Service call that
//     found it due. It never interrupts anything.
//   - A period is a lower bound on the gap between two calls, not a rate. The callback is
//     late by however long the engine went without servicing.
//   - Missed periods coalesce. A periodic callback owed three intervals is called once and
//     rearmed from the current time; it never fires a burst to catch up.
//
// A caller that needs a hard cadence -- a 40 Hz mixer interrupt, say -- does not get one.
// What it gets is a callback serviced as often as the engine passes through a wait or a
// service point of its own. For the movie player that is once per iteration of its playback
// loop, and once per frame advanced for a movie the game steps alongside itself.

#pragma once

#if !defined(_WIN32)

#include "win32compat.h"

#include <cstdint>


// Runs every armed callback whose deadline has passed, and rearms the periodic ones. Cheap
// enough to call from every wait the engine has; reentrant calls return without doing
// anything, so a callback may service, wait, or kill a timer without recursing.
void Win32_Timer_Service(void);

// Replaces the clock deadlines are measured against, so that a test can step time by hand
// rather than wait for it. Null restores the host clock.
void Win32_Timer_Set_Clock(uint32_t (*clock)(void));

#endif	// __EMSCRIPTEN__
