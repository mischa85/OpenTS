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

#include <shellapi.h>

#include <string>
#include <vector>


bool Platform_Executable_Path(char * buffer, size_t size)
{
	return(GetModuleFileNameA(NULL, buffer, (DWORD)size) > 0);
}


char const * const * Platform_Command_Line_Arguments(int * argc)
{
	static std::vector<std::string> arguments;
	static std::vector<char const *> pointers;

	if (pointers.empty()) {
		char program[MAX_PATH];

		arguments.push_back(Platform_Executable_Path(program, sizeof(program)) ? program : "");

		int wide_count = 0;
		LPWSTR * const wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_count);

		if (wide_argv != NULL) {
			// Index zero names the executable, which the module already answered for.
			for (int index = 1; index < wide_count; index++) {
				int const length = WideCharToMultiByte(CP_ACP, 0, wide_argv[index], -1, NULL, 0, NULL, NULL);
				if (length <= 1) continue;

				std::string argument(length - 1, '\0');
				WideCharToMultiByte(CP_ACP, 0, wide_argv[index], -1, argument.data(), length, NULL, NULL);
				arguments.push_back(argument);
			}

			LocalFree(wide_argv);
		}

		for (std::string const & argument : arguments) {
			pointers.push_back(argument.c_str());
		}
	}

	*argc = (int)pointers.size();
	return(pointers.data());
}


uint64_t Platform_Physical_Memory(void)
{
	MEMORYSTATUS status;

	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	return((uint64_t)status.dwTotalPhys);
}


uint32_t Platform_Process_Id(void)
{
	return((uint32_t)GetCurrentProcessId());
}

#endif
