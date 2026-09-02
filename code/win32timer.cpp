/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// win32timer.h states what the timer does and does not promise. What follows is how the
// promise is kept: a small table of armed callbacks, a deadline apiece taken from the same
// millisecond clock timeGetTime reads, and a service routine the engine's waits reach.
//
// Host_Wait belongs here for the same reason. A page owns the thread the engine borrows,
// so a wait that keeps it is a wait that stops the page; the only wait a tab can perform is
// to hand the thread back until the time has passed, which is what browser.h's yield does.
// Servicing the timer on the way through is what keeps a waiting caller from starving the
// callbacks it armed.

#include "always.h"

#include "hostclock.h"
#include "hosttimer.h"
#include "win32timer.h"

#if !defined(_WIN32)

#if defined(__EMSCRIPTEN__)
#include "browser.h"
#include "hostclock.h"
#else
// The native host supplies the same yield trio browser.h declares for the page; the
// defaults in hostyield.cpp wait with the operating system's own clock.
void Browser_Yield(void);
bool Browser_Yield_If_Due(void);
bool Browser_Yield_Is_Available(void);
#endif


/*
** One armed callback. The engine arms two at most -- the sound driver's maintenance pass
** and the movie player's audio refill -- so the table is small on purpose, and a request
** it cannot hold fails the way Windows fails one it cannot hold.
*/
struct HostTimerType
{
	uint32_t Id;				// Zero when the slot is free. Never zero for an armed timer.
	HostTimerCallbackType Callback;
	void * User;
	uint32_t Period;			// Milliseconds between calls.
	uint32_t Due;				// The clock reading the next call is owed at.
};

// Null everywhere but under a test, which steps time by hand instead of waiting for it.
static uint32_t (*_Clock)(void) = nullptr;


static uint32_t Timer_Now(void)
{
	return(_Clock != nullptr ? _Clock() : Host_Milliseconds());
}


void Win32_Timer_Set_Clock(uint32_t (*clock)(void))
{
	_Clock = clock;
}


static const int HOST_TIMERS = 8;

static HostTimerType _Timers[HOST_TIMERS];

// Identifiers are handed out in sequence rather than as slot numbers, so that a stale
// handle names a timer that no longer exists instead of naming whatever took its place.
static UINT _NextTimerID = 1;

// A callback is free to wait, and a wait services the timer; without this a periodic
// callback would be re-entered from inside itself.
static bool _Servicing = false;

/// <summary>
/// Arms a callback to run every period until it is disarmed.
/// </summary>
/// <returns>The timer's handle, or zero when no slot is free or the period is zero.</returns>
/// <remarks>The callback runs on the engine's thread, from inside a wait. This target has
/// no thread to deliver it on, so a caller that never waits never sees it.</remarks>
uint32_t Host_Timer_Arm(uint32_t period, HostTimerCallbackType callback, void * user)
{
	if (callback == nullptr || period == 0) {
		return(0);
	}

	for (int index = 0; index < HOST_TIMERS; index++) {
		HostTimerType * timer = &_Timers[index];

		if (timer->Id != 0) continue;

		if (_NextTimerID == 0) _NextTimerID = 1;

		timer->Id = _NextTimerID++;
		timer->Callback = callback;
		timer->User = user;
		timer->Period = period;
		timer->Due = Timer_Now() + period;
		return(timer->Id);
	}

	return(0);
}


/// <summary>
/// Disarms a timer. A callback may disarm its own timer from inside itself.
/// </summary>
void Host_Timer_Disarm(uint32_t timer)
{
	if (timer == 0) return;

	for (int index = 0; index < HOST_TIMERS; index++) {
		if (_Timers[index].Id == timer) {
			_Timers[index].Id = 0;
			_Timers[index].Callback = nullptr;
			return;
		}
	}
}


/// <summary>
/// Runs every armed callback whose deadline has passed.
/// </summary>
void Win32_Timer_Service(void)
{
	if (_Servicing) {
		return;
	}

	_Servicing = true;

	uint32_t const now = Timer_Now();

	for (int index = 0; index < HOST_TIMERS; index++) {
		HostTimerType * timer = &_Timers[index];

		if (timer->Id == 0) continue;

		/*
		**	The clock wraps every forty nine days, so the comparison is on the signed
		**	difference rather than on the readings themselves.
		*/
		if ((int32_t)(now - timer->Due) < 0) continue;

		HostTimerCallbackType const callback = timer->Callback;
		uint32_t const id = timer->Id;
		void * const user = timer->User;

		/*
		**	Rearm from now, not from the deadline that just passed: the engine may have been
		**	away for many periods, and a caller that wanted one call per period is better
		**	served by one late call than by a burst of them.
		*/
		timer->Due = now + timer->Period;

		callback(id, user);
	}

	_Servicing = false;
}


/// <summary>
/// Waits the requested number of milliseconds, handing the thread back while it waits.
/// </summary>
/// <param name="milliseconds">How long to wait. Zero gives the host a turn without waiting.</param>
/// <remarks>The wait is at least as long as asked and rounds up to whatever gap the host
/// puts between two animation frames, so a request shorter than a frame costs a frame. A
/// hidden tab is not given frames at all and its waits are as long as the browser's
/// throttling makes them.</remarks>
void Host_Wait(uint32_t milliseconds)
{
	/*
	**	Without the yield scaffold nothing carries a wait, and spinning here would keep the
	**	thread the page needs in order to ever let go of it. Report and return instead.
	*/
	if (!Browser_Yield_Is_Available()) {
		WIN32_STUB_VOID();
		return;
	}

	Win32_Timer_Service();

	if (milliseconds == 0) {
		Browser_Yield_If_Due();
		return;
	}

	DWORD start = Timer_Now();

	do {
		Browser_Yield();
		Win32_Timer_Service();
	} while ((DWORD)(Timer_Now() - start) < milliseconds);
}

#endif	// __EMSCRIPTEN__
