/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The engine's side of the presenter. The game draws its frame into the visible surface
// as it always has; this decides when that frame reaches the screen and where in the
// window it lands, and hands it to the renderer behind video.h.

#include "always.h"

#include "video.h"

#include "_map.h"
#include "_rect.h"
#include "_surface.h"
#include "bgfxbackend.h"
#include "browser.h"
#include "dbgprint.h"
#include "dsurface.h"
#include "globals.h"
#include "goptions.h"
#include "gscreen.h"
#include "init.h"
#include "mainopt.h"
#include "misc.h"
#include "movies.h"
#include "msengine.h"
#include "ownrdraw.h"
#include "surface.h"
#include "wincursor.h"
#include "windlg.h"

#include <cstdlib>


/*
 * The size of the frame the game renders into. It is not tied to the window, which may be
 * any size, nor to the desktop, whose mode the game no longer changes.
 */
int VideoModeWidth = 0;
int VideoModeHeight = 0;

/*
 * Is the game running in a framed, resizable window rather than in a borderless one
 * covering the whole screen? The display mode is never changed either way.
 */
bool WindowedMode = false;

// The window the frame is presented into. A browser has none: there is a canvas, whose
// size the page decides, so the Emscripten paths below never consult this.
static HWND _Window = NULL;
static bool _Initialized = false;
static VideoScaleInfo _ScaleInfo;

// Set whenever the visible surface is written to, and cleared once that frame has been
// presented. A frame that is skipped for pacing stays marked, so the next present shows
// the newest content rather than a stale one.
static bool _FrameIsDirty = false;
static unsigned int _LastPresentTime = 0;
static unsigned int _PresentInterval = 16;

// Presents can nest, because a dialog repainting itself presents from inside the paint
// that the engine's own present provoked.
static bool _Presenting = false;

#if !defined(_WIN32)
// The animation frame the last present went out on. A page composites once per frame
// whatever the engine does, and the engine reaches its present hook many times between
// two of them, so this is what a refresh rate stands in for here.
static unsigned int _LastPresentSerial = ~0u;

/*
 * The frame the canvas has asked for but the engine has not taken yet, and when it was
 * asked for. Dragging a window edge asks once per animation frame; taking each of those
 * would throw away and rebuild every drawing surface sixty times a second, so the request
 * has to stop moving first.
 */
static int _RequestedWidth = 0;
static int _RequestedHeight = 0;
static unsigned int _RequestedAt = 0;
static bool _ChangingMode = false;

/*
 * Is the frame the window's own size, or a resolution the player chose that the presenter
 * scales into the window? The page decides which the game starts on, and picking a
 * resolution out of the display options moves it: choosing the size the window already is
 * asks for the frame to keep following it, and choosing any other size pins it.
 */
static bool _FollowWindow = false;

// How long the canvas has to hold still before the frame is resized to match it.
static const unsigned int RESIZE_SETTLE_TIME = 250;

// The bounds the frame is kept within. Below the lower pair the sidebar and the tab bar no
// longer fit; above VIDEO_FOLLOW_MAX the window is filled by scaling instead.
static const int FRAME_MIN_WIDTH = 640;
static const int FRAME_MIN_HEIGHT = 400;
#endif


/// <summary>
/// Works out the shortest sensible gap between presents from the display's refresh rate.
/// </summary>
static void Update_Present_Interval(void)
{
#if !defined(_WIN32)
	/*
	 * The page decides when a frame is composited, and there is no device context to ask
	 * how often that is. Presentation is paced against the animation frame instead.
	 */
	_PresentInterval = 0;
	return;
#else

	int refresh = 0;
	HDC dc = GetDC(_Window);

	if (dc != NULL) {
		refresh = GetDeviceCaps(dc, VREFRESH);
		ReleaseDC(_Window, dc);
	}

	if (refresh <= 1) {
		refresh = 60;
	}

	_PresentInterval = (unsigned int)(1000 / refresh);
	if (_PresentInterval < 3) {
		_PresentInterval = 3;
	}
	if (_PresentInterval > 100) {
		_PresentInterval = 100;
	}
#endif
}


/// <summary>
/// Works out where the game's frame sits inside the window.
/// The frame keeps its shape, so it is grown by whichever of the two axes runs out first
/// and centered in what is left over.
/// </summary>
static void Update_Scale_Info(void)
{
	RECT client;

	_ScaleInfo.GameWidth = VideoModeWidth;
	_ScaleInfo.GameHeight = VideoModeHeight;

#if !defined(_WIN32)
	client.left = 0;
	client.top = 0;
	client.right = Browser_Canvas_Width();
	client.bottom = Browser_Canvas_Height();

	if (client.right <= 0 || client.bottom <= 0) {
		client.right = VideoModeWidth;
		client.bottom = VideoModeHeight;
	}
#else
	if (_Window == NULL || !GetClientRect(_Window, &client)) {
		client.left = 0;
		client.top = 0;
		client.right = VideoModeWidth;
		client.bottom = VideoModeHeight;
	}
#endif

	_ScaleInfo.WindowWidth = client.right - client.left;
	_ScaleInfo.WindowHeight = client.bottom - client.top;

	if (_ScaleInfo.GameWidth <= 0 || _ScaleInfo.GameHeight <= 0 || _ScaleInfo.WindowWidth <= 0 || _ScaleInfo.WindowHeight <= 0) {
		_ScaleInfo.DestX = 0;
		_ScaleInfo.DestY = 0;
		_ScaleInfo.DestWidth = _ScaleInfo.WindowWidth;
		_ScaleInfo.DestHeight = _ScaleInfo.WindowHeight;
		_ScaleInfo.ScaleX = 1.0f;
		_ScaleInfo.ScaleY = 1.0f;
		return;
	}

	double scalex = (double)_ScaleInfo.WindowWidth / (double)_ScaleInfo.GameWidth;
	double scaley = (double)_ScaleInfo.WindowHeight / (double)_ScaleInfo.GameHeight;
	double scale = (scalex < scaley) ? scalex : scaley;

	if (Options.IntegerScaling && scale >= 1.0) {
		scale = (double)(int)scale;
	}

	_ScaleInfo.DestWidth = (int)((double)_ScaleInfo.GameWidth * scale);
	_ScaleInfo.DestHeight = (int)((double)_ScaleInfo.GameHeight * scale);
	_ScaleInfo.DestX = (_ScaleInfo.WindowWidth - _ScaleInfo.DestWidth) / 2;
	_ScaleInfo.DestY = (_ScaleInfo.WindowHeight - _ScaleInfo.DestHeight) / 2;
	_ScaleInfo.ScaleX = (float)((double)_ScaleInfo.DestWidth / (double)_ScaleInfo.GameWidth);
	_ScaleInfo.ScaleY = (float)((double)_ScaleInfo.DestHeight / (double)_ScaleInfo.GameHeight);
}


#if !defined(_WIN32)

/// <summary>
/// Brings a size the page asked for into a frame the engine can render.
/// </summary>
/// <remarks>
/// The dimensions are taken down to a multiple of four so that no surface in the engine is
/// handed an awkward stride. What that trims is at most three pixels, which the presenter
/// then fills with the same black it puts beside a frame of a different shape.
/// </remarks>
void Video_Clamp_Frame_Size(int & width, int & height)
{
	if (width > VIDEO_FOLLOW_MAX_WIDTH) width = VIDEO_FOLLOW_MAX_WIDTH;
	if (height > VIDEO_FOLLOW_MAX_HEIGHT) height = VIDEO_FOLLOW_MAX_HEIGHT;

	width &= ~3;
	height &= ~3;

	if (width < FRAME_MIN_WIDTH) width = FRAME_MIN_WIDTH;
	if (height < FRAME_MIN_HEIGHT) height = FRAME_MIN_HEIGHT;
}


/// <summary>
/// Can every drawing surface in the engine be destroyed and rebuilt at this moment?
/// </summary>
/// <remarks>
/// A scenario part way through loading is filling surfaces that are about to be replaced, a
/// movie holds the surface it draws into until it is torn down, a shell screen laid itself
/// out against the surfaces it came up on, and a paint is drawing into surfaces it would
/// finish drawing into after they had gone.
/// </remarks>
static bool Mode_Change_Is_Safe(void)
{
	if (!_Initialized || _Presenting || _ChangingMode) {
		return(false);
	}

	if (VisibleSurface == NULL || HiddenSurface == NULL) {
		return(false);
	}

	if (ScenarioInit != 0) {
		return(false);
	}

	// A movie keeps the surface it was created on for as long as it lives, and pulling that
	// surface out from under it traps in the player's own lock callback.
	if (Movie_Holds_A_Surface()) {
		return(false);
	}

	// The shell menus, the map selections and the score screens place their artwork against
	// the size the screen came up at, and the loop each of them runs draws neither that
	// artwork nor the backdrop beneath it again.
	if (MSEngine::Is_Screen_Up()) {
		return(false);
	}

	// A dialog and its controls paint into the game's own surfaces rather than into their
	// windows, so a paint part way through would finish into surfaces that had gone.
	if (OwnerDraw::Is_Painting()) {
		return(false);
	}

	return(true);
}


/// <summary>
/// Draws the frame again after it has been resized underneath an open dialog.
/// </summary>
/// <param name="oldwidth">The frame width the dialogs were placed against.</param>
/// <param name="oldheight">The frame height the dialogs were placed against.</param>
/// <remarks>
/// The mode change replaced every drawing surface the engine owns, and the loop a dialog
/// runs while it is up draws neither the dialog nor what is behind it. So the frame is put
/// together again from what the engine still holds, choosing what to draw the same way the
/// main window does when Windows asks it to repaint.
/// </remarks>
static void Rebuild_Screen_Under_Dialogs(int oldwidth, int oldheight)
{
	OwnerDraw::Relayout_Dialogs(oldwidth, oldheight);

	if (ScenarioActive) {
		Map.Flag_To_Redraw(GS_REDRAW_ALL);
		Map.Render();
	} else {
		Title_Screen_Restore(true);
	}

	Heal_Dialog_Controls();
}


/// <summary>
/// Records the frame size the canvas is asking the engine to render at.
/// </summary>
/// <param name="width">The canvas width, in CSS pixels.</param>
/// <param name="height">The canvas height, in CSS pixels.</param>
void Video_Request_Frame_Size(int width, int height)
{
	if (!_FollowWindow || width <= 0 || height <= 0) {
		return;
	}

	Video_Clamp_Frame_Size(width, height);

	if (width == VideoModeWidth && height == VideoModeHeight) {
		_RequestedWidth = 0;
		_RequestedHeight = 0;
		return;
	}

	_RequestedWidth = width;
	_RequestedHeight = height;

	// Restarted on every report, so a drag is one mode change at the end rather than one
	// per animation frame along the way.
	_RequestedAt = timeGetTime();
}


/// <summary>
/// Resizes the frame to the canvas if one has been asked for and the engine can take it.
/// </summary>
void Video_Service_Display(void)
{
	/*
	 * A scenario sizes its own surfaces from the options rather than from the presenter, and
	 * the settings are read again well after the frame was first matched to the canvas. So
	 * while the window is the resolution, it is also what the options say the resolution is.
	 */
	if (_FollowWindow && VideoModeWidth > 0 && VideoModeHeight > 0) {
		Options.ScreenWidth = VideoModeWidth;
		Options.ScreenHeight = VideoModeHeight;
	}

	if (_RequestedWidth <= 0 || _RequestedHeight <= 0) {
		return;
	}

	if (_RequestedWidth == VideoModeWidth && _RequestedHeight == VideoModeHeight) {
		_RequestedWidth = 0;
		_RequestedHeight = 0;
		return;
	}

	if ((timeGetTime() - _RequestedAt) < RESIZE_SETTLE_TIME) {
		return;
	}

	if (!Mode_Change_Is_Safe()) {
		return;
	}

	int width = _RequestedWidth;
	int height = _RequestedHeight;

	_RequestedWidth = 0;
	_RequestedHeight = 0;

	int oldwidth = Options.ScreenWidth;
	int oldheight = Options.ScreenHeight;

	int framewidth = VideoModeWidth;
	int frameheight = VideoModeHeight;
	bool underdialog = (WS_Top_Window() != NULL);

	Options.ScreenWidth = width;
	Options.ScreenHeight = height;

	_ChangingMode = true;
	bool changed = Change_Display_Mode(width, height);
	_ChangingMode = false;

	if (!changed) {
		Options.ScreenWidth = oldwidth;
		Options.ScreenHeight = oldheight;
		return;
	}

	if (underdialog) {
		Rebuild_Screen_Under_Dialogs(framewidth, frameheight);
	}
}

#endif	// not _WIN32


/// <summary>
/// Converts the configured filter into the one the renderer names.
/// </summary>
static BackendScaleMode Backend_Scale_Mode(void)
{
	switch (Options.ScaleMode) {
		case VIDEO_SCALE_LINEAR:
			return(BACKEND_SCALE_LINEAR);

		case VIDEO_SCALE_NEAREST:
			return(BACKEND_SCALE_NEAREST);

		default:
			return(BACKEND_SCALE_PIXELART);
	}
}


/// <summary>
/// Starts the presenter on the game's window.
/// </summary>
/// <param name="window">The main window. Its client area receives the frame.</param>
/// <returns>bool; Did the presenter start? A false return is fatal to the game.</returns>
bool Video_Init(HWND window)
{
	RECT client;

	if (_Initialized) {
		return(true);
	}

#if !defined(_WIN32)
	/*
	 * There is no window. The drawing target is the page's canvas, whose size the page
	 * decides and reports back, so the handle the caller passed carries nothing.
	 */
	(void)window;

	client.left = 0;
	client.top = 0;
	client.right = Browser_Canvas_Width();
	client.bottom = Browser_Canvas_Height();

	if (client.right <= 0 || client.bottom <= 0) {
		return(false);
	}

	/*
	 * The game starts at the size the page laid the canvas out at rather than at whatever
	 * resolution the settings last held, because on a page the window is the one thing the
	 * player did choose. "?display=WIDTHxHEIGHT" overrides that with a fixed frame, and
	 * "?display=scaled" leaves the configured resolution alone.
	 */
	int startwidth = 0;
	int startheight = 0;

	if (Browser_Display_Width() > 0 && Browser_Display_Height() > 0) {
		startwidth = Browser_Display_Width();
		startheight = Browser_Display_Height();
	} else if (Browser_Display_Policy() == BROWSER_DISPLAY_NATIVE) {
		startwidth = Browser_Canvas_CSS_Width();
		startheight = Browser_Canvas_CSS_Height();
		_FollowWindow = true;
	}

	if (startwidth > 0 && startheight > 0) {
		Video_Clamp_Frame_Size(startwidth, startheight);
		VideoModeWidth = startwidth;
		VideoModeHeight = startheight;
		Options.ScreenWidth = startwidth;
		Options.ScreenHeight = startheight;
		VisibleRect = Rect(0, 0, startwidth, startheight);
	}
#else
	if (window == NULL || !GetClientRect(window, &client)) {
		return(false);
	}

	_Window = window;
#endif

	BackendRenderer renderer = (BackendRenderer)Options.Renderer;

#if defined(__EMSCRIPTEN__)
	// A browser has no window handle to present onto, so the renderer names the page's
	// canvas by CSS selector instead. The id is fixed by the shell page the build ships.
	BackendWindow target = Browser_Canvas_Selector();
#else
	BackendWindow target = window;
#endif

	if (!Backend_Init(target, client.right - client.left, client.bottom - client.top, renderer, Options.VSync)) {
		return(false);
	}

	DebugString("Video: renderer is %s\n", Backend_Renderer_Name());

	_Initialized = true;

	if (!Backend_Set_Frame_Size(VideoModeWidth, VideoModeHeight)) {
		Backend_Shutdown();
		_Initialized = false;
		return(false);
	}

	Update_Scale_Info();
	Update_Present_Interval();
	return(true);
}


/// <summary>
/// Stops the presenter and releases the renderer.
/// </summary>
void Video_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	Win_Cursor_Shutdown();
	Backend_Shutdown();
	_Initialized = false;
	_Window = NULL;
	_FrameIsDirty = false;
}


/// <summary>
/// Moves the game to a different render resolution.
/// The caller replaces the surfaces afterwards; this only resizes what the frame is
/// presented from and leaves the previous mode untouched when it fails.
/// </summary>
/// <param name="width">The new frame width.</param>
/// <param name="height">The new frame height.</param>
/// <returns>bool; Was the mode changed?</returns>
bool Video_Set_Mode(int width, int height)
{
	if (!_Initialized || width <= 0 || height <= 0) {
		return(false);
	}

	if (!Backend_Set_Frame_Size(width, height)) {
		return(false);
	}

#if !defined(_WIN32)
	/*
	 * A resolution the player chose out of the display options settles whether the frame
	 * goes on following the window. The size the window already is means yes, since that
	 * entry in the list is the window; any other size is a resolution the player wants kept
	 * whatever the window then does.
	 */
	if (!_ChangingMode) {
		int canvaswidth = Browser_Canvas_CSS_Width();
		int canvasheight = Browser_Canvas_CSS_Height();

		Video_Clamp_Frame_Size(canvaswidth, canvasheight);
		_FollowWindow = (width == canvaswidth && height == canvasheight);

		_RequestedWidth = 0;
		_RequestedHeight = 0;
	}
#endif

	VideoModeWidth = width;
	VideoModeHeight = height;

	Update_Scale_Info();
	Update_Present_Interval();
	Win_Cursor_Refresh();
	_FrameIsDirty = true;
	return(true);
}


/// <summary>
/// Tells the presenter the window's client area changed size.
/// </summary>
void Video_On_Resize(int width, int height)
{
	if (!_Initialized || width <= 0 || height <= 0) {
		return;
	}

#if !defined(_WIN32)
	/*
	 * The frame is presented onto the canvas, never onto the window the engine believes it
	 * has, so a size reported by that window means nothing here.
	 */
	width = Browser_Canvas_Width();
	height = Browser_Canvas_Height();

	if (width <= 0 || height <= 0) {
		return;
	}
#endif

	Backend_On_Resize(width, height);
	Update_Scale_Info();
	Update_Present_Interval();
	Win_Cursor_Refresh();
	Video_Mark_Dirty();
}


/// <summary>
/// Tells the presenter the desktop's display settings changed.
/// The window may now be on a monitor that refreshes at a different rate.
/// </summary>
void Video_On_Display_Change(void)
{
	if (!_Initialized) {
		return;
	}

	Update_Present_Interval();
	Video_Mark_Dirty();
}


/// <summary>
/// Records that the visible surface has been drawn to since the last present.
/// </summary>
void Video_Mark_Dirty(void)
{
	_FrameIsDirty = true;
}


/// <summary>
/// Puts the visible surface on the screen whatever its state.
/// </summary>
void Video_Present(void)
{
	if (!_Initialized || _Presenting || VisibleSurface == NULL) {
		return;
	}

	DSurface * surface = (DSurface *)VisibleSurface;
	void * pixels = surface->Get_Buffer();

	if (pixels == NULL) {
		return;
	}

	_Presenting = true;
	Backend_Present(pixels, surface->Stride(), _ScaleInfo.DestX, _ScaleInfo.DestY, _ScaleInfo.DestWidth, _ScaleInfo.DestHeight, Backend_Scale_Mode());
	_Presenting = false;

	_FrameIsDirty = false;
	_LastPresentTime = timeGetTime();
}


/// <summary>
/// Puts the visible surface on the screen if it has changed and the display is ready for
/// another frame.
/// A skipped present leaves the frame marked, so the next one shows the newest content.
/// This never waits: the game loop is not paced by presentation.
/// </summary>
void Video_Present_If_Dirty(void)
{
	if (!_FrameIsDirty) {
		return;
	}

#if !defined(_WIN32)
	/*
	 * One present per animation frame. The engine reaches here from every wait it has,
	 * which is many times more often than the page composites, and each present is a
	 * texture upload and a draw whether or not anything sees it.
	 */
	if (Browser_Frame_Serial() == _LastPresentSerial) {
		return;
	}
	_LastPresentSerial = Browser_Frame_Serial();
#else

	unsigned int now = timeGetTime();
	if ((now - _LastPresentTime) < _PresentInterval) {
		return;
	}
#endif

	Video_Present();
}


/// <summary>
/// Reports where the game's frame is drawn inside the window.
/// </summary>
VideoScaleInfo const & Video_Get_Scale_Info(void)
{
	return(_ScaleInfo);
}


/// <summary>
/// Compares two display modes by width and then height.
/// </summary>
static int __cdecl Compare_Modes(void const * left, void const * right)
{
	int const * lhs = (int const *)left;
	int const * rhs = (int const *)right;

	if (lhs[0] != rhs[0]) {
		return(lhs[0] - rhs[0]);
	}
	return(lhs[1] - rhs[1]);
}


/// <summary>
/// Collects the display resolutions that fall within the given bounds.
/// Only the sizes matter; the desktop decides the color depth, and duplicates that differ
/// only by refresh rate are reported once.
/// </summary>
/// <param name="minwidth">The narrowest mode to report.</param>
/// <param name="minheight">The shortest mode to report.</param>
/// <param name="maxwidth">The widest mode to report.</param>
/// <param name="maxheight">The tallest mode to report.</param>
/// <returns>A caller owned array of width and height pairs ending in a zero pair, or NULL
/// when nothing matched.</returns>
int * EnumDisplayModes(int minwidth, int minheight, int maxwidth, int maxheight)
{
	DEVMODE devmode;
	int count = 0;
	int capacity = 0;
	int * modes = NULL;

	for (int pass = 0; pass < 2; pass++) {

		count = 0;

		for (int index = 0; ; index++) {
			memset(&devmode, 0, sizeof(devmode));
			devmode.dmSize = sizeof(devmode);

			if (!EnumDisplaySettings(NULL, index, &devmode)) {
				break;
			}

			int width = (int)devmode.dmPelsWidth;
			int height = (int)devmode.dmPelsHeight;

			if (width < minwidth || width > maxwidth || height < minheight || height > maxheight) {
				continue;
			}

			if (modes != NULL) {
				// The list is being filled from a second enumeration; should it have
				// grown since the one that sized the array, the extra modes are dropped.
				if (count >= capacity) {
					break;
				}
				modes[count * 2] = width;
				modes[count * 2 + 1] = height;
			}
			count++;
		}

		if (modes != NULL) {
			break;
		}

		if (count == 0) {
			return(NULL);
		}

		capacity = count;
		modes = new int[(count + 1) * 2];
	}

	qsort(modes, count, sizeof(int) * 2, Compare_Modes);

	// The same size is listed once per refresh rate and color depth it supports.
	int unique = 0;
	for (int index = 0; index < count; index++) {
		if (unique == 0 || modes[unique * 2 - 2] != modes[index * 2] || modes[unique * 2 - 1] != modes[index * 2 + 1]) {
			modes[unique * 2] = modes[index * 2];
			modes[unique * 2 + 1] = modes[index * 2 + 1];
			unique++;
		}
	}

	modes[unique * 2] = 0;
	modes[unique * 2 + 1] = 0;
	return(modes);
}
