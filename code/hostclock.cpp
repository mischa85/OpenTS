/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "hostclock.h"

#include <chrono>


uint32_t Host_Milliseconds(void)
{
	using Clock = std::chrono::steady_clock;

	// Counting from the first reading rather than from the host's own epoch keeps the
	// value small, so the wrap the callers guard against stays a theoretical one.
	static Clock::time_point const origin = Clock::now();

	return((uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin).count());
}
