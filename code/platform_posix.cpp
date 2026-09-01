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
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif


#if defined(__EMSCRIPTEN__)

// The wasm binary the browser fetched is not a file in the filesystem the engine reads, so
// no name here can be opened. Every caller wants the directory rather than the file.
static char const PROGRAM_FILE_NAME[] = "OpenTS.wasm";

/*
** The host's argument list, which is the whole of this target's command line: the page
** builds it from its query string and hands it over as Module.arguments, and a shell
** running the module under node passes its own arguments through the same array. The list
** does not carry the program itself, which is supplied here to match every other target.
**
** The internal name is read first and the incoming property second, so that a host which
** supplied arguments some other way is still answered; a build that renames the internal
** falls back rather than reporting nothing.
*/
EM_JS(int, Process_Argument_Count, (void), {
	var args = (typeof programArgs !== "undefined" && programArgs) ||
		(typeof Module !== "undefined" && Module["arguments"]) || [];
	return args.length;
});

EM_JS(int, Process_Argument, (int index, char * buffer, int size), {
	var args = (typeof programArgs !== "undefined" && programArgs) ||
		(typeof Module !== "undefined" && Module["arguments"]) || [];
	var text = (index >= 0 && index < args.length) ? "" + args[index] : "";

	var count = 0;
	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[buffer + count] = (code > 127) ? 63 : code;
		count++;
	}
	HEAPU8[buffer + count] = 0;
	return count;
});

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
#if defined(__EMSCRIPTEN__)
	char working[MAX_PATH];

	if (getcwd(working, sizeof(working)) == nullptr) {
		return(false);
	}

	size_t const length = strlen(working);
	char const * const separator = (length > 0 && working[length - 1] == '/') ? "" : "/";

	int const written = snprintf(buffer, size, "%s%s%s", working, separator, PROGRAM_FILE_NAME);
	return(written > 0 && (size_t)written < size);
#elif defined(__APPLE__)
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
#if defined(__EMSCRIPTEN__)
	static std::vector<std::string> arguments;
	static std::vector<char const *> pointers;

	if (pointers.empty()) {
		char program[MAX_PATH];

		arguments.push_back(Platform_Executable_Path(program, sizeof(program)) ? program : PROGRAM_FILE_NAME);

		int const count = Process_Argument_Count();
		for (int index = 0; index < count; index++) {
			char text[1024];

			Process_Argument(index, text, sizeof(text));
			arguments.push_back(text);
		}

		for (std::string const & argument : arguments) {
			pointers.push_back(argument.c_str());
		}
	}

	*argc = (int)pointers.size();
	return(pointers.data());
#elif defined(__APPLE__)
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


uint64_t Platform_Physical_Memory(void)
{
#if defined(__APPLE__)
	uint64_t bytes = 0;
	size_t size = sizeof(bytes);

	if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) != 0) {
		return(0);
	}
	return(bytes);
#else
	long const pages = sysconf(_SC_PHYS_PAGES);
	long const page_size = sysconf(_SC_PAGESIZE);

	if (pages <= 0 || page_size <= 0) {
		return(0);
	}
	return((uint64_t)pages * (uint64_t)page_size);
#endif
}


uint32_t Platform_Process_Id(void)
{
	return((uint32_t)getpid());
}

#endif
