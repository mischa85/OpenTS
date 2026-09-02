/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#ifdef _WIN32

#include "hostclock.h"
#include "hosttimer.h"
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


void Host_Wait(uint32_t milliseconds)
{
	::Sleep((DWORD)milliseconds);
}


/*
** The multimedia timer calls back on a thread of its own, and its accuracy follows the
** timer resolution in force, so the finest one is held for as long as the timer is armed.
** timeSetEvent wants a callback shape of its own, so each armed timer carries the portable
** one it stands for.
*/
namespace {

struct ArmedTimerType
{
	MMRESULT Id;
	HostTimerCallbackType Callback;
	void * User;
};

ArmedTimerType _ArmedTimers[8];


void CALLBACK Deliver(UINT id, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
	for (ArmedTimerType const & armed : _ArmedTimers) {
		if (armed.Id == id && armed.Callback != nullptr) {
			armed.Callback((uint32_t)armed.Id, armed.User);
			return;
		}
	}
}

}


uint32_t Host_Timer_Arm(uint32_t period, HostTimerCallbackType callback, void * user)
{
	if (callback == nullptr || period == 0) {
		return(0);
	}

	for (ArmedTimerType & armed : _ArmedTimers) {
		if (armed.Id != 0) continue;

		armed.Callback = callback;
		armed.User = user;

		timeBeginPeriod(1);
		armed.Id = timeSetEvent((UINT)period, 1, Deliver, 0, TIME_PERIODIC);

		if (armed.Id == 0) {
			timeEndPeriod(1);
			armed.Callback = nullptr;
			return(0);
		}
		return((uint32_t)armed.Id);
	}

	return(0);
}


void Host_Timer_Disarm(uint32_t timer)
{
	if (timer == 0) return;

	for (ArmedTimerType & armed : _ArmedTimers) {
		if (armed.Id != (MMRESULT)timer) continue;

		timeKillEvent(armed.Id);
		timeEndPeriod(1);
		armed.Id = 0;
		armed.Callback = nullptr;
		return;
	}
}

#endif
