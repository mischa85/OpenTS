/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the WebAssembly multimedia timer without the engine, a page, or game data.
// What is covered is the contract win32timer.h states: a callback is delivered from the
// service pass and never before its deadline, a periodic one that was missed for several
// periods is delivered once rather than in a burst, a one shot disarms itself, and a
// callback may kill a timer or wait from inside itself.
//
// The clock and the yield are supplied here rather than by the platform, so the test steps
// time instead of waiting on it. That is also what makes the wrap of the millisecond clock
// testable at all: it comes around every forty nine days on a real one.

#include "win32timer.h"
#include "hostclock.h"

#include <cstdio>

namespace {

int Failures = 0;

// The clock win32timer.cpp reads. The test moves it by hand.
DWORD Now = 0;

int Yields = 0;
int PacedYields = 0;

// What the callbacks record.
int Calls[4];
UINT LastId = 0;
DWORD_PTR LastUser = 0;
UINT KillOnCall = 0;
bool ServiceFromCallback = false;
int ReentryDepth = 0;
int MaxReentryDepth = 0;


void Report(char const * name, bool ok)
{
	std::printf("%-70s %s\n", name, ok ? "ok" : "FAILED");
	if (!ok) {
		Failures++;
	}
}


void Report_Value(char const * name, long actual, long expected)
{
	bool ok = (actual == expected);
	std::printf("%-70s %s", name, ok ? "ok\n" : "");
	if (!ok) {
		std::printf("FAILED (got %ld, expected %ld)\n", actual, expected);
		Failures++;
	}
}


void Reset(void)
{
	for (int index = 0; index < 4; index++) {
		Calls[index] = 0;
	}

	Yields = 0;
	PacedYields = 0;
	LastId = 0;
	LastUser = 0;
	KillOnCall = 0;
	ServiceFromCallback = false;
	ReentryDepth = 0;
	MaxReentryDepth = 0;
}


void CALLBACK Counting_Callback(UINT id, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR)
{
	ReentryDepth++;
	if (ReentryDepth > MaxReentryDepth) {
		MaxReentryDepth = ReentryDepth;
	}

	LastId = id;
	LastUser = user;

	if (user < 4) {
		Calls[user]++;
	}

	if (KillOnCall != 0) {
		timeKillEvent(KillOnCall);
	}

	if (ServiceFromCallback) {
		Win32_Timer_Service();
	}

	ReentryDepth--;
}


void Kill_All(void)
{
	/*
	**	Identifiers are handed out in sequence and never reused, so sweeping a generous
	**	range leaves the table empty whatever the preceding test armed.
	*/
	for (UINT id = 1; id < 256; id++) {
		timeKillEvent(id);
	}
}


void Test_Device_Caps(void)
{
	TIMECAPS caps;

	Report("timeGetDevCaps rejects a null structure", timeGetDevCaps(nullptr, sizeof(caps)) == TIMERR_NOCANDO);
	Report("timeGetDevCaps rejects a short structure", timeGetDevCaps(&caps, sizeof(caps) - 1) == TIMERR_NOCANDO);

	caps.wPeriodMin = 0;
	caps.wPeriodMax = 0;

	Report("timeGetDevCaps answers", timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR);
	Report_Value("timeGetDevCaps reports the clock's own resolution as the minimum", (long)caps.wPeriodMin, 1);
	Report("timeGetDevCaps reports a usable range", caps.wPeriodMin < caps.wPeriodMax);
}


void Test_Arming(void)
{
	Reset();

	Report("timeSetEvent refuses a null callback", timeSetEvent(16, 1, nullptr, 0, TIME_PERIODIC) == 0);
	Report("timeSetEvent refuses a zero period", timeSetEvent(0, 1, Counting_Callback, 0, TIME_PERIODIC) == 0);
	Report("timeSetEvent refuses a callback kind it cannot deliver", timeSetEvent(16, 1, Counting_Callback, 0, 0x0010) == 0);

	UINT first = timeSetEvent(16, 1, Counting_Callback, 0, TIME_PERIODIC);
	UINT second = timeSetEvent(16, 1, Counting_Callback, 1, TIME_PERIODIC);

	Report("an armed timer has a non zero identifier", first != 0 && second != 0);
	Report("two armed timers are told apart", first != second);

	Report("timeKillEvent accepts an armed timer", timeKillEvent(first) == TIMERR_NOERROR);
	Report("timeKillEvent refuses a killed timer", timeKillEvent(first) == TIMERR_NOCANDO);
	Report("timeKillEvent refuses a zero identifier", timeKillEvent(0) == TIMERR_NOCANDO);

	timeKillEvent(second);
	Kill_All();
}


void Test_Table_Is_Bounded(void)
{
	Reset();

	int armed = 0;
	while (timeSetEvent(16, 1, Counting_Callback, 0, TIME_PERIODIC) != 0) {
		armed++;
		if (armed > 64) break;
	}

	Report("the timer table is bounded and a request past it fails", armed > 0 && armed <= 64);

	Kill_All();
	Report("a full table takes new timers once it is emptied", timeSetEvent(16, 1, Counting_Callback, 0, TIME_PERIODIC) != 0);
	Kill_All();
}


void Test_Periodic_Delivery(void)
{
	Reset();
	Now = 1000;

	UINT id = timeSetEvent(16, 1, Counting_Callback, 2, TIME_PERIODIC);

	Win32_Timer_Service();
	Report_Value("a timer does not run before its deadline", Calls[2], 0);

	Now += 15;
	Win32_Timer_Service();
	Report_Value("a timer does not run a millisecond early", Calls[2], 0);

	Now += 1;
	Win32_Timer_Service();
	Report_Value("a timer runs on its deadline", Calls[2], 1);
	Report("the callback is told which timer it is", LastId == id);
	Report_Value("the callback is handed its own user value", (long)LastUser, 2);

	Win32_Timer_Service();
	Report_Value("a serviced timer does not run again in the same millisecond", Calls[2], 1);

	Now += 16;
	Win32_Timer_Service();
	Report_Value("a periodic timer runs again a period later", Calls[2], 2);

	timeKillEvent(id);
	Now += 160;
	Win32_Timer_Service();
	Report_Value("a killed timer stops running", Calls[2], 2);

	Kill_All();
}


void Test_Missed_Periods_Coalesce(void)
{
	Reset();
	Now = 5000;

	UINT id = timeSetEvent(16, 1, Counting_Callback, 3, TIME_PERIODIC);

	// The engine was away for ten periods, which is what a long frame looks like here.
	Now += 160;
	Win32_Timer_Service();
	Report_Value("ten missed periods are delivered as one call, not as a burst", Calls[3], 1);

	Now += 16;
	Win32_Timer_Service();
	Report_Value("the period is measured from the delivery, not from the missed deadline", Calls[3], 2);

	timeKillEvent(id);
	Kill_All();
}


void Test_One_Shot(void)
{
	Reset();
	Now = 100;

	UINT id = timeSetEvent(20, 1, Counting_Callback, 0, TIME_ONESHOT);

	Now += 20;
	Win32_Timer_Service();
	Report_Value("a one shot runs when it comes due", Calls[0], 1);

	Now += 200;
	Win32_Timer_Service();
	Report_Value("a one shot does not run twice", Calls[0], 1);
	Report("a fired one shot is no longer armed", timeKillEvent(id) == TIMERR_NOCANDO);

	Kill_All();
}


void Test_Callback_May_Kill_And_Service(void)
{
	Reset();
	Now = 200;

	UINT id = timeSetEvent(16, 1, Counting_Callback, 1, TIME_PERIODIC);
	KillOnCall = id;

	Now += 16;
	Win32_Timer_Service();
	Report_Value("a callback that kills its own timer still runs once", Calls[1], 1);

	KillOnCall = 0;
	Now += 160;
	Win32_Timer_Service();
	Report_Value("a timer killed from inside itself stays killed", Calls[1], 1);

	Reset();
	Now += 16;
	id = timeSetEvent(16, 1, Counting_Callback, 1, TIME_PERIODIC);
	ServiceFromCallback = true;

	Now += 16;
	Win32_Timer_Service();
	Report_Value("servicing from inside a callback does not run it again", Calls[1], 1);
	Report_Value("servicing from inside a callback does not recurse", MaxReentryDepth, 1);

	ServiceFromCallback = false;
	timeKillEvent(id);
	Kill_All();
}


void Test_Clock_Wrap(void)
{
	Reset();
	Now = 0xFFFFFFF0;

	UINT id = timeSetEvent(32, 1, Counting_Callback, 0, TIME_PERIODIC);

	Win32_Timer_Service();
	Report_Value("a deadline past the clock's wrap is not mistaken for one in the past", Calls[0], 0);

	Now += 32;
	Win32_Timer_Service();
	Report_Value("a deadline is honoured across the clock's wrap", Calls[0], 1);

	timeKillEvent(id);
	Kill_All();
}


void Test_Wait(void)
{
	Reset();
	Now = 10000;

	Host_Wait(0);
	Report_Value("a zero wait does not cost a frame", Yields, 0);
	Report_Value("a zero wait still offers the host a turn", PacedYields, 1);

	Reset();
	DWORD start = Now;
	Host_Wait(50);

	Report("a wait lasts at least as long as it was asked to", (DWORD)(Now - start) >= 50);
	Report("a wait hands the thread back rather than spinning", Yields > 0);

	Reset();
	start = Now;
	UINT id = timeSetEvent(16, 1, Counting_Callback, 0, TIME_PERIODIC);
	Host_Wait(100);

	Report("a waiting caller does not starve the timers it armed", Calls[0] > 0);
	Report_Value("the timer runs once per yield the wait takes", Calls[0], Yields);

	timeKillEvent(id);
	Kill_All();
}

}	// namespace


/*
** The platform underneath win32timer.cpp, supplied by the test. The clock is a variable
** the test steps and the yield is a fixed sixteen milliseconds, which is what an animation
** frame costs a real one.
*/
static uint32_t Test_Clock(void)
{
	return(Now);
}


void Browser_Yield(void)
{
	Yields++;
	Now += 16;
}


bool Browser_Yield_If_Due(void)
{
	PacedYields++;
	return(true);
}


bool Browser_Yield_Is_Available(void)
{
	return(true);
}


void Win32_Stub_Reached(char const * function)
{
	std::printf("stub reached: %s\n", function);
}


void Win32_Unsupported_Reached(char const * description)
{
	std::printf("unsupported: %s\n", description);
}


int main(void)
{
	Win32_Timer_Set_Clock(Test_Clock);

	Test_Device_Caps();
	Test_Arming();
	Test_Table_Is_Bounded();
	Test_Periodic_Delivery();
	Test_Missed_Periods_Coalesce();
	Test_One_Shot();
	Test_Callback_May_Kill_And_Service();
	Test_Clock_Wrap();
	Test_Wait();

	std::printf("\n%s\n", (Failures == 0) ? "All timer tests passed." : "Timer tests FAILED.");
	return((Failures == 0) ? 0 : 1);
}
