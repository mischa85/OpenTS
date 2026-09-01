/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What is left of the process API for a target that is not a Windows process. The module
// table, the loader, the locks, and the command line have each been answered by a portable
// call at their call sites instead; platform.h carries the two that survived as an API.
// These remain because the callers that want them are Windows-only paths that still have
// to compile, or because the honest answer is a constant.

#include "always.h"

#include "win32compat.h"

#if !defined(_WIN32)

// A stub returns what its Win32 original returns on failure, after naming itself once.

HANDLE GetCurrentProcess(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
HANDLE GetCurrentThread(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
DWORD GetCurrentThreadId(void) { return(1); }
DWORD GetCurrentProcessId(void) { return(WIN32_STUB(0)); }
void GlobalMemoryStatus(LPMEMORYSTATUS) { WIN32_STUB_VOID(); }


#endif	// !_WIN32
