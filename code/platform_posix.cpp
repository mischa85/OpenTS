/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#ifndef _WIN32

#include "platform.h"

#include <cstdio>
#include <cstring>

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif


bool Parse_GUID_Text(char const * text, GUID & guid)
{
	unsigned int data1;
	unsigned int data2;
	unsigned int data3;
	unsigned int data4[8];

	if (sscanf(text, "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",
					&data1, &data2, &data3,
					&data4[0], &data4[1], &data4[2], &data4[3],
					&data4[4], &data4[5], &data4[6], &data4[7]) != 11) {
		return(false);
	}

	guid.Data1 = data1;
	guid.Data2 = (unsigned short)data2;
	guid.Data3 = (unsigned short)data3;
	for (int index = 0; index < 8; index++) {
		guid.Data4[index] = (unsigned char)data4[index];
	}
	return(true);
}


void Compose_GUID_Text(GUID const & guid, char * buffer, size_t size)
{
	snprintf(buffer, size, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
				(unsigned int)guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}


bool Platform_Executable_Path(char * buffer, size_t size)
{
#ifdef __APPLE__
	uint32_t capacity = (uint32_t)size;
	return(_NSGetExecutablePath(buffer, &capacity) == 0);
#else
	ssize_t const length = readlink("/proc/self/exe", buffer, size - 1);
	if (length <= 0) {
		return(false);
	}
	buffer[length] = '\0';
	return(true);
#endif
}


char const * const * Platform_Command_Line_Arguments(int * argc)
{
#ifdef __APPLE__
	*argc = *_NSGetArgc();
	return((char const * const *)*_NSGetArgv());
#else
	static char storage[8192];
	static char * arguments[256];
	static int count = -1;

	if (count < 0) {
		count = 0;

		FILE * file = fopen("/proc/self/cmdline", "rb");
		if (file != NULL) {
			size_t const length = fread(storage, 1, sizeof(storage) - 1, file);
			fclose(file);
			storage[length] = '\0';

			size_t offset = 0;
			while (offset < length && count < (int)(sizeof(arguments) / sizeof(arguments[0]))) {
				arguments[count++] = storage + offset;
				offset += strlen(storage + offset) + 1;
			}
		}
	}

	*argc = count;
	return((char const * const *)arguments);
#endif
}

#endif
