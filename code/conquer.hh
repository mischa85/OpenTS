/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

/*
** What one pass of the outer game loop did. The pass either carried the scenario forward,
** did nothing at all because the game is parked, or found the scenario finished. Only the
** last of the three ends the loop; a parked pass is still a pass.
*/
enum GameFrameType {
	GAME_FRAME_ADVANCED,		// A frame was played.
	GAME_FRAME_SUSPENDED,		// The game is parked, so no frame was played.
	GAME_FRAME_FINISHED			// The scenario is over.
};
