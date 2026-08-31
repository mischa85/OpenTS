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

#include "globals.h"
#include "goptions.h"
#include "sidebar.h"


/*
 * Height of the tab strip along the top of the screen. TabClass draws the strip itself and
 * carries its own measurement of the artwork; this is the room the tactical view gives up
 * to it.
 */
static int const TAB_HEIGHT = 16;


/// <summary>
/// Divides a screen between the tactical view and the interface.
/// </summary>
/// <param name="visible">The rectangle of the whole screen.</param>
/// <returns>Returns with the rectangles the display and its surfaces are built from.</returns>
ScreenLayout Compute_Screen_Layout(Rect const & visible)
{
	ScreenLayout layout;

	layout.Tactical = visible;
	layout.Tactical.X = ((Options.IsSidebarOnRight || Debug_Map) ? 0 : SidebarClass::SIDE_WIDTH);
	layout.Tactical.Y = TAB_HEIGHT;
	layout.Tactical.Width -= SidebarClass::SIDE_WIDTH;
	layout.Tactical.Height -= TAB_HEIGHT;

	layout.Hidden = visible;
	layout.Composite = Rect(0, 0, layout.Tactical.Width, visible.Height);
	layout.Tile = layout.Composite;
	layout.Sidebar = Rect(0, 0, SidebarClass::SIDE_WIDTH, visible.Height);

	return(layout);
}
