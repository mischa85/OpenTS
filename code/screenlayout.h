/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "point.h"
#include "rect.h"

/*
 * The largest interface magnification the game will use. Beyond this the sidebar takes more
 * of the screen than the world it belongs to.
 */
int const UI_SCALE_MAX = 4;

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
	 * The interface column, in the sidebar surface's own coordinates rather than in screen
	 * pixels. The surface keeps the interface's historical metrics whatever the screen is,
	 * and Blit_Sidebar magnifies it by the interface scale on its way to the screen.
	 */
	Rect Sidebar;
};

ScreenLayout Compute_Screen_Layout(Rect const & visible);

/// <summary>
/// How many screen pixels the interface is drawn at for each pixel of its own artwork.
/// </summary>
/// <param name="framewidth">The width of the frame the interface has to fit into.</param>
/// <param name="frameheight">The height of the frame the interface has to fit into.</param>
/// <returns>Returns with the magnification, between one and UI_SCALE_MAX.</returns>
/// <remarks>A configured scale of zero asks for one that follows the frame, and one the frame
/// is too small to carry is stepped back down to one it can.</remarks>
int UI_Scale(int framewidth, int frameheight);
int UI_Scale(void);

/// <summary>
/// Converts a rectangle of the sidebar surface into the screen rectangle it is magnified into.
/// </summary>
/// <param name="rect">The rectangle in the sidebar surface's own coordinates.</param>
/// <returns>Returns with the same area expressed in screen pixels.</returns>
Rect Sidebar_To_Screen(Rect const & rect);

/// <summary>
/// Converts a screen point into the sidebar surface pixel drawn beneath it.
/// </summary>
/// <param name="point">The point in screen pixels.</param>
/// <returns>Returns with the point in the sidebar surface's own coordinates.</returns>
Point2D Screen_To_Sidebar(Point2D const & point);
