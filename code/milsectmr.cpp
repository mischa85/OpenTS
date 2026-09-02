/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "milsectmr.h"

#include "dbgprint.h"
#include "getcpu.h"
#include "hostclock.h"
#include "mpu.h"
#include "win.h"

#define PERIOD_RESOLUTION 1 /// Use 1-millisecond target resolution.

/// Microsoft's macros for widening a large integer into a double.
#define ULi2Double(x) ((double)((x).u.HighPart) * 4.294967296E9 + (double)((x).u.LowPart))
#define Li2Double(x) ((double)((x).HighPart) * 4.294967296E9 + (double)((x).LowPart))

/// The same conversion, but taking the two halves of the value separately.
#define LI_TO_DBL(dh, dl) ((double)((double)dh * 4.294967296E9 + (double)dl))


/// <summary>
/// Creates the millisecond timer and works out how to drive it.
/// This routine will ask the processor for its clock rate so that the cycle counter can be
/// scaled into milliseconds. Machines that will not report a rate fall back to the Windows
/// multimedia timer, whose resolution is raised to one millisecond for the life of the timer.
/// </summary>
MillisecondTimerClass::MillisecondTimerClass(void)
{
	unsigned int high = 0;
	Frequency = 1.0;
	unsigned int low = Get_CPU_Rate(high);

	if (low == 0 && high == 0) {
#ifdef _WIN32
		timeBeginPeriod(PERIOD_RESOLUTION);
#endif

	} else {
		double dl = low;
		double dh = high;

		DebugString("MillisecondTimerClass low = %u, high = %u\n", low, high);

		Frequency = LI_TO_DBL(dh, dl) / 1000; // 1000 = rate.

	}
}


/// <summary>
/// Releases the millisecond timer.
/// If this timer had to raise the system timer resolution in order to work, the resolution
/// is dropped back here so that the rest of the system is not left paying for it.
/// </summary>
MillisecondTimerClass::~MillisecondTimerClass(void)
{
	// The constructor leaves Frequency at 1.0 exactly when it took the multimedia timer
	// path, which is the case that raised the resolution.
	if (Frequency == 1.0) {
#ifdef _WIN32
		timeEndPeriod(PERIOD_RESOLUTION);
#endif
	}
}


/// <summary>
/// Fetches the current time, expressed in milliseconds.
/// This routine is used every time the timer is read. The performance counter supplies the
/// value when the machine has one, since it is finer grained than the system timer.
/// Otherwise the Windows multimedia timer is consulted instead.
/// </summary>
/// <returns>Returns with the current time in milliseconds.</returns>
MillisecondTimerClass::operator double () const
{
	static int cpu_type = -1;
	static bool has_mmx = false;

	if (cpu_type == -1) {
		Get_CPU_Type(cpu_type, has_mmx, NULL, 0);
	}

	/*
	 * The family check is left over from the cycle counter this once read, which an 80486
	 * and older did not have. The performance counter carries no such requirement.
	 */
	if (Frequency != 1.0 && cpu_type > 4) {
		unsigned int high;
		unsigned int low;

		low = Get_CPU_Clock(high);
		double dl = low;
		double dh = high;

		return(LI_TO_DBL(dh, dl) / Frequency);
	}

	return(Host_Milliseconds());
}
