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

class Surface;

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


/// <summary>
/// Says what size the screen coming up lays itself out at.
/// </summary>
/// <param name="size">The size of the screen's artwork, or an empty size to leave the
/// frame unmagnified.</param>
/// <remarks>A screen that has been converted to the design space sets this as it comes up
/// and clears it as it goes away. A size rather than a rectangle is kept, because a mode
/// change moves where the design space sits without changing how big it is.</remarks>
void Set_Shell_Size(Point2D const & size);

/// <summary>
/// The rectangle the shell design space occupies in the surfaces the shell draws into.
/// </summary>
/// <returns>Returns with the design rectangle, centered in the surfaces the shell draws
/// itself into. The whole of one of those surfaces is returned when no screen has claimed a
/// design space.</returns>
Rect Shell_Rect(void);

/// <summary>
/// Converts a rectangle of the shell design space into the screen rectangle it is
/// magnified into.
/// </summary>
/// <param name="rect">The rectangle in shell design coordinates.</param>
/// <returns>Returns with the same area expressed in screen pixels.</returns>
Rect Shell_To_Screen(Rect const & rect);

/// <summary>
/// Converts a screen point into the shell design pixel drawn beneath it.
/// </summary>
/// <param name="point">The point in screen pixels.</param>
/// <returns>Returns with the point in shell design coordinates.</returns>
Point2D Screen_To_Shell(Point2D const & point);

/// <summary>
/// Copies a region of a shell surface onto the visible screen, magnified.
/// </summary>
/// <param name="surface">The surface holding the drawn shell screen.</param>
/// <param name="rect">The region to copy, in shell design coordinates.</param>
void Blit_Shell(Surface & surface, Rect const & rect);

/// <summary>
/// The largest rectangle of a picture's shape that a frame holds, centered in it.
/// </summary>
/// <param name="size">The size of the picture to fit.</param>
/// <param name="frame">The rectangle to fit it into.</param>
/// <returns>Returns with the fitted rectangle, or the frame itself if the size is
/// empty.</returns>
Rect Fit_Centered(Point2D const & size, Rect const & frame);

/// <summary>
/// Carries a point placed against a centered picture over to the same picture filled out.
/// </summary>
/// <param name="point">The point, placed against the picture centered at its own size.</param>
/// <param name="size">The size of the picture.</param>
/// <param name="frame">The rectangle the picture was fitted into.</param>
/// <returns>Returns with where that point has moved to.</returns>
/// <remarks>The loading screens place their captions against artwork centered at the size
/// it was drawn. Filling the artwork out moves the slot the caption belongs in, and this is
/// what moves the caption with it.</remarks>
Point2D Fit_Point(Point2D const & point, Point2D const & size, Rect const & frame);
