/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "screenlayout.h"

#include "_rect.h"
#include "globals.h"
#include "goptions.h"
#include "sidebar.h"

#include <algorithm>


/*
 * Height of the tab strip along the top of the screen. TabClass draws the strip itself and
 * carries its own measurement of the artwork; this is the room the tactical view gives up
 * to it. The strip is not magnified with the rest of the interface, because it is drawn into
 * the composite, sidebar and tile surfaces alike and only one of the three is scaled.
 */
static int const TAB_HEIGHT = 16;


/*
 * The frame height one step of magnification is worth. A frame shorter than twice this keeps
 * the interface at the size its artwork was drawn.
 */
static int const UI_SCALE_STEP = 540;


/*
 * The shortest frame the interface has ever been laid out in. Magnifying past the point where
 * the sidebar's own surface would be shorter than this leaves the radar pane and the build
 * strips without the room their artwork needs, so the scale is brought back down instead.
 */
static int const UI_SCALE_MIN_HEIGHT = 400;


/// <summary>
/// Divides a screen between the tactical view and the interface.
/// </summary>
/// <param name="visible">The rectangle of the whole screen.</param>
/// <returns>Returns with the rectangles the display and its surfaces are built from.</returns>
ScreenLayout Compute_Screen_Layout(Rect const & visible)
{
	ScreenLayout layout;

	int const scale = UI_Scale(visible.Width, visible.Height);
	int const sidewidth = SidebarClass::SIDE_WIDTH * scale;

	layout.Tactical = visible;
	layout.Tactical.X = ((Options.IsSidebarOnRight || Debug_Map) ? 0 : sidewidth);
	layout.Tactical.Y = TAB_HEIGHT;
	layout.Tactical.Width -= sidewidth;
	layout.Tactical.Height -= TAB_HEIGHT;

	layout.Hidden = visible;
	layout.Composite = Rect(0, 0, layout.Tactical.Width, visible.Height);
	layout.Tile = layout.Composite;
	layout.Sidebar = Rect(0, 0, SidebarClass::SIDE_WIDTH, visible.Height / scale);

	return(layout);
}


/// <summary>
/// Works out how far the interface is magnified on its way to the screen.
/// </summary>
/// <param name="framewidth">The width of the frame the interface has to fit into.</param>
/// <param name="frameheight">The height of the frame the interface has to fit into.</param>
/// <returns>Returns with the magnification, between one and UI_SCALE_MAX.</returns>
int UI_Scale(int framewidth, int frameheight)
{
	int scale = Options.UIScale;

	if (scale <= 0) {
		scale = frameheight / UI_SCALE_STEP;
	}

	scale = std::clamp(scale, 1, UI_SCALE_MAX);

	/*
	 * A magnification the frame cannot carry is stepped back down rather than obeyed. The
	 * sidebar has to leave the world at least half of the width and to keep enough height of
	 * its own for the artwork it stacks.
	 */
	while (scale > 1
			&& (frameheight / scale < UI_SCALE_MIN_HEIGHT
				|| SidebarClass::SIDE_WIDTH * scale * 2 > framewidth)) {
		scale--;
	}

	return(scale);
}


int UI_Scale(void)
{
	return(UI_Scale(VisibleRect.Width, VisibleRect.Height));
}


/// <summary>
/// Converts a rectangle of the sidebar surface into the screen rectangle it is magnified into.
/// </summary>
/// <param name="rect">The rectangle in the sidebar surface's own coordinates.</param>
/// <returns>Returns with the same area expressed in screen pixels.</returns>
Rect Sidebar_To_Screen(Rect const & rect)
{
	int const scale = UI_Scale();

	return(Rect(SidebarRect.X + rect.X * scale, rect.Y * scale, rect.Width * scale, rect.Height * scale));
}


/// <summary>
/// Converts a screen point into the sidebar surface pixel drawn beneath it.
/// </summary>
/// <param name="point">The point in screen pixels.</param>
/// <returns>Returns with the point in the sidebar surface's own coordinates.</returns>
Point2D Screen_To_Sidebar(Point2D const & point)
{
	int const scale = UI_Scale();

	return(Point2D((point.X - SidebarRect.X) / scale, point.Y / scale));
}
