/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>

/*
 * The operating system surface the portable code builds on. On Windows this is <windows.h>
 * itself. Everywhere else the same surface is answered by the compatibility layer in
 * win32compat.h, which the WebAssembly and native POSIX targets share; docs/PORTING.md
 * records the plan.
 */

#ifdef _WIN32

#include <windows.h>

#define PATH_SEP_CHAR '\\'
#define PATH_SEP_STR "\\"

#else

#include "win32compat.h"

#define PATH_SEP_CHAR '/'
#define PATH_SEP_STR "/"

/*
 * The brace and hyphen form CLSIDFromString and StringFromCLSID exchange, without the wide
 * character plumbing those carry, because game data writes class identifiers in this form.
 */
bool Parse_GUID_Text(char const * text, GUID & guid);
void Compose_GUID_Text(GUID const & guid, char * buffer, size_t size);

#endif


/*
 * The full path of the running program, which every target can answer for itself. False
 * leaves the buffer unspecified.
 */
bool Platform_Executable_Path(char * buffer, size_t size);

/*
 * The command line already split the way the host split it, with the program itself at
 * index zero. The array and the strings last as long as the process.
 */
char const * const * Platform_Command_Line_Arguments(int * argc);

/*
 * How much physical memory the machine has, in bytes. Zero when the host will not say.
 */
uint64_t Platform_Physical_Memory(void);

/*
 * The process's own identifier, for naming something that another copy of the program
 * must not collide with.
 */
uint32_t Platform_Process_Id(void);
