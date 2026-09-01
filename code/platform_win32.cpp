/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#ifdef _WIN32

#include "platform.h"


bool Platform_Executable_Path(char * buffer, size_t size)
{
	return(GetModuleFileNameA(NULL, buffer, (DWORD)size) > 0);
}

#endif
