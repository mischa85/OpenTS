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
#include "_surface.h"
#include "globals.h"
#include "goptions.h"
#include "sidebar.h"
#include "surface.h"

#include "color.hh"

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


/*
 * The size the screen in front lays itself out at, or an empty size when it lays itself out
 * against the frame. A size is kept rather than a rectangle because a mode change moves
 * where the design space sits without changing how big the artwork is.
 */
static Point2D _ShellSize(0, 0);


void Set_Shell_Size(Point2D const & size)
{
	_ShellSize = size;
}


/// <summary>
/// The largest rectangle of a picture's shape that a frame holds, centered in it.
/// </summary>
/// <param name="size">The size of the picture to fit.</param>
/// <param name="frame">The rectangle to fit it into.</param>
/// <returns>Returns with the fitted rectangle, or the frame itself if the size is
/// empty.</returns>
Rect Fit_Centered(Point2D const & size, Rect const & frame)
{
	if (size.X <= 0 || size.Y <= 0) {
		return(frame);
	}

	int width = frame.Width;
	int height = size.Y * width / size.X;

	if (height > frame.Height) {
		height = frame.Height;
		width = size.X * height / size.Y;
	}

	return(Rect(frame.X + (frame.Width - width) / 2, frame.Y + (frame.Height - height) / 2, width, height));
}


/// <summary>
/// The rectangle the shell design space occupies in the surfaces the shell draws into.
/// </summary>
/// <returns>Returns with the design rectangle, centered in the shell's surfaces.</returns>
/// <remarks>A shell screen centers its artwork on the surface it draws into rather than on
/// the screen, and the two are not always the same size, so the design rectangle is placed
/// against the surface. The hidden and alternate surfaces are the pair the shell uses and
/// they are always made together at one size. The surface is looked up here rather than
/// kept, because a mode change destroys and rebuilds it. Artwork larger than the surface
/// keeps the surface's size, since that is all of it there is to copy.</remarks>
Rect Shell_Rect(void)
{
	Rect const surface = (HiddenSurface != NULL) ? HiddenSurface->Get_Rect() : VisibleRect;

	if (_ShellSize.X <= 0 || _ShellSize.Y <= 0) {
		return(surface);
	}

	int const width = std::min(_ShellSize.X, surface.Width);
	int const height = std::min(_ShellSize.Y, surface.Height);

	return(Rect((surface.Width - width) / 2, (surface.Height - height) / 2, width, height));
}


/// <summary>
/// The screen rectangle the shell design space is magnified into.
/// </summary>
/// <returns>Returns with the largest rectangle of the design space's shape that the frame
/// holds, centered in it.</returns>
/// <remarks>A screen that has claimed no design space is left exactly where it was: the
/// design rectangle is handed back unchanged, which makes every mapping here the identity
/// and every copy a plain one. The surfaces are not always the size of the frame, so
/// fitting one to the other would move such a screen rather than leave it alone.</remarks>
static Rect Shell_Screen_Rect(void)
{
	Rect const design = Shell_Rect();

	if (_ShellSize.X <= 0 || _ShellSize.Y <= 0) {
		return(design);
	}

	return(Fit_Centered(Point2D(design.Width, design.Height), VisibleRect));
}


/// <summary>
/// Converts a rectangle of the shell design space into the screen rectangle it is
/// magnified into.
/// </summary>
/// <param name="rect">The rectangle in shell design coordinates.</param>
/// <returns>Returns with the same area expressed in screen pixels.</returns>
/// <remarks>The two edges are mapped rather than the corner and the size, so that
/// neighboring rectangles meet exactly and a rectangle lying within the design space maps
/// to one lying within the screen rectangle. Blit_Shell relies on the second of those:
/// Blit_Clip trims a scaled blit's two rectangles independently, which changes the
/// magnification unless what it is handed already fits.</remarks>
Rect Shell_To_Screen(Rect const & rect)
{
	Rect const design = Shell_Rect();
	Rect const screen = Shell_Screen_Rect();

	int const left = screen.X + (rect.X - design.X) * screen.Width / design.Width;
	int const right = screen.X + (rect.X + rect.Width - design.X) * screen.Width / design.Width;
	int const top = screen.Y + (rect.Y - design.Y) * screen.Height / design.Height;
	int const bottom = screen.Y + (rect.Y + rect.Height - design.Y) * screen.Height / design.Height;

	return(Rect(left, top, right - left, bottom - top));
}


/// <summary>
/// Converts a screen point into the shell design pixel drawn beneath it.
/// </summary>
/// <param name="point">The point in screen pixels.</param>
/// <returns>Returns with the point in shell design coordinates. A point in the black
/// beside the picture lands outside the design rectangle.</returns>
Point2D Screen_To_Shell(Point2D const & point)
{
	Rect const design = Shell_Rect();
	Rect const screen = Shell_Screen_Rect();

	return(Point2D(
		design.X + (point.X - screen.X) * design.Width / screen.Width,
		design.Y + (point.Y - screen.Y) * design.Height / screen.Height
	));
}


/// <summary>
/// Fills whatever the magnified shell screen does not cover with black.
/// </summary>
static void Fill_Shell_Surround(void)
{
	Rect const screen = Shell_Screen_Rect();

	Rect const bands[] = {
		Rect(0, 0, VisibleRect.Width, screen.Y),
		Rect(0, screen.Y + screen.Height, VisibleRect.Width, VisibleRect.Height - (screen.Y + screen.Height)),
		Rect(0, screen.Y, screen.X, screen.Height),
		Rect(screen.X + screen.Width, screen.Y, VisibleRect.Width - (screen.X + screen.Width), screen.Height)
	};

	for (Rect const & band : bands) {
		if (band.Is_Valid()) {
			VisibleSurface->Fill_Rect(VisibleSurface->Get_Rect(), band, TBLACK);
		}
	}
}


/// <summary>
/// Copies a region of a shell surface onto the visible screen, magnified.
/// </summary>
/// <param name="surface">The surface holding the drawn shell screen.</param>
/// <param name="rect">The region to copy, in shell design coordinates.</param>
/// <remarks>The region is trimmed to the design space before it is mapped, so that the
/// scaled blit is handed rectangles that already fit and Blit_Clip has nothing left to
/// trim. A region reaching past the design space is one that covered the whole surface,
/// which is also when the black around the picture is owed a repaint.</remarks>
void Blit_Shell(Surface & surface, Rect const & rect)
{
	Rect const design = Shell_Rect();
	Rect const source = Intersect(design, rect);

	if (_ShellSize.X > 0 && _ShellSize.Y > 0 && rect.Is_Valid() && Intersect(rect, design) != rect) {
		Fill_Shell_Surround();
	}

	if (source.Is_Valid()) {
		VisibleSurface->Blit_From(Shell_To_Screen(source), surface, source);
	}
}


/// <summary>
/// Carries a point placed against a centered picture over to the same picture filled out.
/// </summary>
/// <param name="point">The point, placed against the picture centered at its own size.</param>
/// <param name="size">The size of the picture.</param>
/// <param name="frame">The rectangle the picture was fitted into.</param>
/// <returns>Returns with where that point has moved to.</returns>
Point2D Fit_Point(Point2D const & point, Point2D const & size, Rect const & frame)
{
	if (size.X <= 0 || size.Y <= 0) {
		return(point);
	}

	Rect const centered(frame.X + (frame.Width - size.X) / 2, frame.Y + (frame.Height - size.Y) / 2, size.X, size.Y);
	Rect const filled = Fit_Centered(size, frame);

	return(Point2D(
		filled.X + (point.X - centered.X) * filled.Width / size.X,
		filled.Y + (point.Y - centered.Y) * filled.Height / size.Y
	));
}
