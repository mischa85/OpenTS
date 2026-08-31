/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The stub reporting the Win32 substitute funnels through. It lives in its own
// translation unit so a harness that overrides one platform function does not drag the
// whole substitute in beside its own definition.

#include "win32compat.h"

#if !defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
** Reporting. Each entry point names itself the first time it is reached; a stub inside
** a frame loop would otherwise bury everything else in the log.
*/
static char const * ReportedFunctions[512];
static int ReportedCount = 0;


static bool Already_Reported(char const * function)
{
	for (int index = 0; index < ReportedCount; index++) {
		if (ReportedFunctions[index] == function || strcmp(ReportedFunctions[index], function) == 0) {
			return(true);
		}
	}

	if (ReportedCount < (int)(sizeof(ReportedFunctions) / sizeof(ReportedFunctions[0]))) {
		ReportedFunctions[ReportedCount++] = function;
	}
	return(false);
}


void Win32_Stub_Reached(char const * function)
{
	if (Already_Reported(function)) return;
	fprintf(stderr, "OpenTS: unimplemented Win32 entry point %s reached; it reports failure.\n", function);
	fflush(stderr);
}


void Win32_Unsupported_Reached(char const * description)
{
	if (Already_Reported(description)) return;
	fprintf(stderr, "OpenTS: %s is not implemented on this target; the call reports failure.\n", description);
	fflush(stderr);
}


void Win32_Stub_Fatal(char const * function)
{
	fprintf(stderr, "OpenTS: unimplemented Win32 entry point %s reached, and it has no way to "
		"report failure to its caller. Stopping rather than continuing on a result that was "
		"never produced.\n", function);
	fflush(stderr);
	abort();
}


#endif
