/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "rect.h"

/// <summary>
/// How a screen of a given size is divided between the world and the interface.
/// </summary>
/// <remarks>
/// The sidebar takes a column of the screen and the tab strip a row of it, and the tactical
/// view is what is left. Every surface a scenario is drawn into is sized from that division,
/// so the sites that reallocate those surfaces ask for it here rather than each repeating
/// the arithmetic.
/// </remarks>
struct ScreenLayout
{
	/*
	 * The world viewport, in screen pixels. The tab strip is above it and the sidebar
	 * beside it.
	 */
	Rect Tactical;

	/*
	 * The shell surface, which is the whole screen. Menus, briefings and score screens
	 * have no world to leave room for.
	 */
	Rect Hidden;

	/*
	 * The in-game frame, and the cached terrain layer beneath it. A pan swaps the two, so
	 * they are the same size. Both keep the full screen height: the tab strip is drawn
	 * into the top of the frame rather than being cut from it.
	 */
	Rect Composite;
	Rect Tile;

	/*
	 * The interface column.
	 */
	Rect Sidebar;
};

ScreenLayout Compute_Screen_Layout(Rect const & visible);
