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
struct Win32TimerEventType
{
	UINT Id;					// Zero when the slot is free. Never zero for an armed timer.
	LPTIMECALLBACK Callback;
	DWORD_PTR User;
	UINT Period;				// Milliseconds between calls.
	DWORD Due;					// The timeGetTime reading the next call is owed at.
	bool IsPeriodic;
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


static const int WIN32_TIMER_EVENTS = 8;

static Win32TimerEventType _TimerEvents[WIN32_TIMER_EVENTS];

// Identifiers are handed out in sequence rather than as slot numbers, so that a stale
// handle names a timer that no longer exists instead of naming whatever took its place.
static UINT _NextTimerID = 1;

// A callback is free to wait, and a wait services the timer; without this a periodic
// callback would be re-entered from inside itself.
static bool _Servicing = false;

/*
** The clock is timeGetTime, so a period finer than its millisecond is not expressible.
** The upper bound is Windows' own, which is what the callers were written against.
*/
static const UINT WIN32_TIMER_PERIOD_MIN = 1;
static const UINT WIN32_TIMER_PERIOD_MAX = 1000000;


/// <summary>
/// Reports the range of timer periods this target accepts.
/// </summary>
/// <param name="caps">Filled in with the shortest and longest period timeSetEvent takes.</param>
/// <param name="size">Size of the structure, as Windows requires it be passed.</param>
/// <returns>MMRESULT; TIMERR_NOERROR, or TIMERR_NOCANDO for a request that cannot be answered.</returns>
/// <remarks>The period is what the dispatcher measures against the clock. How closely it is
/// kept is a separate question that win32timer.h answers: delivery waits on the engine.</remarks>
MMRESULT timeGetDevCaps(LPTIMECAPS caps, UINT size)
{
	if (caps == nullptr || size < sizeof(TIMECAPS)) {
		return(TIMERR_NOCANDO);
	}

	caps->wPeriodMin = WIN32_TIMER_PERIOD_MIN;
	caps->wPeriodMax = WIN32_TIMER_PERIOD_MAX;
	return(TIMERR_NOERROR);
}


/// <summary>
/// Arms a callback to be run once, or every period, until it is killed.
/// </summary>
/// <param name="delay">Milliseconds until the first call, and between calls when periodic.</param>
/// <param name="resolution">The accuracy Windows was asked for. There is none to give here.</param>
/// <param name="callback">The routine to run. It runs on the engine's thread.</param>
/// <param name="user">Passed back to the callback untouched.</param>
/// <param name="flags">TIME_ONESHOT or TIME_PERIODIC.</param>
/// <returns>MMRESULT; the timer's identifier, or zero if it could not be armed.</returns>
MMRESULT timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags)
{
	(void)resolution;

	if (callback == nullptr || delay < WIN32_TIMER_PERIOD_MIN || delay > WIN32_TIMER_PERIOD_MAX) {
		return(0);
	}

	/*
	**	Windows can also signal an event or pulse one instead of calling back. Neither has a
	**	meaning on a target with one thread, so the request fails rather than silently
	**	becoming a callback the caller never asked for.
	*/
	if ((flags & ~(UINT)TIME_PERIODIC) != 0) {
		return(WIN32_UNSUPPORTED("timeSetEvent with anything but a function callback", 0));
	}

	for (int index = 0; index < WIN32_TIMER_EVENTS; index++) {
		Win32TimerEventType * event = &_TimerEvents[index];

		if (event->Id != 0) continue;

		if (_NextTimerID == 0) _NextTimerID = 1;

		event->Id = _NextTimerID++;
		event->Callback = callback;
		event->User = user;
		event->Period = delay;
		event->Due = Timer_Now() + delay;
		event->IsPeriodic = ((flags & TIME_PERIODIC) != 0);
		return(event->Id);
	}

	return(0);
}


/// <summary>
/// Disarms a timer. A callback may kill its own timer from inside itself.
/// </summary>
/// <param name="id">The identifier timeSetEvent returned.</param>
/// <returns>MMRESULT; TIMERR_NOERROR, or TIMERR_NOCANDO if no such timer is armed.</returns>
MMRESULT timeKillEvent(UINT id)
{
	if (id == 0) {
		return(TIMERR_NOCANDO);
	}

	for (int index = 0; index < WIN32_TIMER_EVENTS; index++) {
		if (_TimerEvents[index].Id == id) {
			_TimerEvents[index].Id = 0;
			_TimerEvents[index].Callback = nullptr;
			return(TIMERR_NOERROR);
		}
	}

	return(TIMERR_NOCANDO);
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

	DWORD now = Timer_Now();

	for (int index = 0; index < WIN32_TIMER_EVENTS; index++) {
		Win32TimerEventType * event = &_TimerEvents[index];

		if (event->Id == 0) continue;

		/*
		**	The clock wraps every forty nine days, so the comparison is on the signed
		**	difference rather than on the readings themselves.
		*/
		if ((LONG)(now - event->Due) < 0) continue;

		UINT id = event->Id;
		LPTIMECALLBACK callback = event->Callback;
		DWORD_PTR user = event->User;

		/*
		**	Rearm from now, not from the deadline that just passed: the engine may have been
		**	away for many periods, and a caller that wanted one call per period is better
		**	served by one late call than by a burst of them.
		*/
		if (event->IsPeriodic) {
			event->Due = now + event->Period;
		} else {
			event->Id = 0;
			event->Callback = nullptr;
		}

		callback(id, 0, user, 0, 0);
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
