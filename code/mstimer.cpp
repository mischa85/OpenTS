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


/// <summary>
/// Asks Windows for one millisecond timer resolution.
/// The reading itself no longer needs this -- hostclock.h answers that from a clock of its
/// own -- but the request is process wide, and every wait the game paces itself with is
/// rounded up to the resolution in force.
/// </summary>
MillisecondSystemTimerClass::MillisecondSystemTimerClass(void)
{
	timeBeginPeriod(1);
}


/// <summary>
/// Returns the system timer to its normal resolution.
/// This routine undoes the resolution request made when the timer was created, so that
/// the rest of the system is not left paying for the finer granularity.
/// </summary>
MillisecondSystemTimerClass::~MillisecondSystemTimerClass(void)
{
	timeEndPeriod(1);
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
