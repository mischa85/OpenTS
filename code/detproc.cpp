/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The processor probes the engine consults before it picks a code path. They once ran
// CPUID and reported what the host chip could do. The engine has no processor specific
// path left for them to select, so they describe a plain machine and every caller keeps
// to its portable route.

#include "always.h"

#include "misc.h"

/*
**	getcpu.h declares these with C linkage but describes VendorID as a single char, so
**	this file cannot include it and states the interface itself.
*/
extern "C" {
	bool __cdecl Detect_MMX_Availability(void);
	bool __cdecl Detect_CMOV_Availability(void);
}

/*
**	The millisecond timer gates the cycle counter on a family above four and the scenario
**	timing estimate gates a tweak on a family above five, so the reported family has to
**	clear both. Six is what every processor the game has ever run on reported.
*/
static char const REPORTED_FAMILY = 6;

/*
**	VendorID is consumed as a NUL terminated string.
*/
extern "C" {
	char CPUType = REPORTED_FAMILY;
	char VendorID[20] = "Not available";
}


/// <summary>
/// Reports whether the multimedia extensions are available and records the processor
/// family for the callers that read it directly.
/// </summary>
/// <returns>Returns false. Nothing in the engine has an MMX path left to take.</returns>
bool __cdecl Detect_MMX_Availability(void)
{
	CPUType = REPORTED_FAMILY;

	return(false);
}


/// <summary>
/// Reports whether the conditional move instructions are available.
/// </summary>
/// <returns>Returns false. Nothing in the engine has a conditional move path left to
/// take.</returns>
bool __cdecl Detect_CMOV_Availability(void)
{
	return(false);
}


/// <summary>
/// Reports how far the processor can be interrogated about itself. Zero meant an 80386,
/// one an 80486 that could not run CPUID, and two a processor that answered it.
/// </summary>
/// <returns>Returns two, which is what every processor the game runs on would have
/// answered.</returns>
WORD __cdecl Processor(void)
{
	return(2);
}
