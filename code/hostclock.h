/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <chrono>
#include <cstdint>

/*
 * The engine's coarse clock, in milliseconds from a clock that only ever moves forward.
 * Every caller measures an interval with it and none depends on where it starts. The
 * reading wraps roughly every forty nine days, so compare differences and not the
 * readings themselves.
 */
inline uint32_t Host_Milliseconds(void)
{
	return((uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
}

/*
 * Waits at least the requested number of milliseconds, keeping the host alive while it
 * waits: on a target whose window and timers are driven by the engine's own thread, this
 * is the only wait that does not stop them. Zero gives the host a turn without waiting.
 * The wait rounds up to whatever gap the host puts between two frames.
 */
void Host_Wait(uint32_t milliseconds);
