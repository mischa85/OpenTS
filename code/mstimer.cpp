/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"
#include "hostclock.h"

#include "mstimer.h"

#include "win.h"


MillisecondSystemTimerClass::MillisecondSystemTimerClass(void)
{
}


MillisecondSystemTimerClass::~MillisecondSystemTimerClass(void)
{
}


/// <summary>
/// Fetches the current millisecond reading of the system clock.
/// This is the sampling routine that the timer templates call whenever they need to
/// know how much time has passed.
/// </summary>
/// <returns>Returns with the number of milliseconds elapsed since the clock's first reading.</returns>
int MillisecondSystemTimerClass::operator () (void) const
{
	return(Host_Milliseconds());
}


/// <summary>
/// Converts the timer into its current millisecond reading.
/// This routine lets the timer object be used wherever a plain time value is expected.
/// </summary>
/// <returns>Returns with the number of milliseconds elapsed since the clock's first reading.</returns>
MillisecondSystemTimerClass::operator int (void) const
{
	return(Host_Milliseconds());
}
