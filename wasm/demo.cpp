/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
 * A standalone WebAssembly target that drives the renderer seam and nothing else. It
 * exists to prove two things independently of the engine's own progress: that
 * bgfxbackend presents a 16 bit 565 frame onto a page's canvas, and that the frame loop
 * a browser can actually run is the one docs/WASM-PORT.md describes.
 *
 * A browser tab runs the page on the same thread that services input, layout, and
 * paint, so a function that never returns starves all three. There is no way to sleep
 * out the rest of a frame and no way to spin waiting for anything to arrive. Everything
 * below is shaped by that: the browser owns the loop, one call advances the demo by at
 * most a bounded number of frames and returns, and the pacing that Sync_Delay does with
 * a sleep is a predicate here instead. That is the pattern the engine's own loop has to
 * reach, so this is written as a model of it rather than as demo scaffolding.
 */

#include "bgfxbackend.h"

#include <emscripten/console.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdlib>
#include <cstring>


// The engine's native frame. Presenting at this size and letting the backend fit it to
// whatever the canvas measures is what puts the scaling path under test; a canvas sized
// to match would only ever exercise a one to one blit.
static const int GAME_WIDTH = 640;
static const int GAME_HEIGHT = 480;

// bgfx names its drawing surface with a CSS selector, and the shell page gives the
// canvas this id.
static char const * const CANVAS_SELECTOR = "#canvas";

// The simulation rate. Presentation is deliberately not tied to it: the browser decides
// when a frame is composited, and returning without drawing would leave the compositor a
// cleared drawing buffer.
static const double FRAME_INTERVAL = 1000.0 / 60.0;

// How far the simulation may catch up in one callback. A tab that was throttled comes
// back with an arbitrarily large arrears, and working through all of it would be the
// blocking loop this design exists to avoid.
static const int MAX_CATCHUP_FRAMES = 4;

static const int SINE_STEPS = 1024;
static const int SINE_MASK = SINE_STEPS - 1;
static const int SINE_SCALE = 1024;


enum FrameResult {
	FRAME_ADVANCED,
	FRAME_NOT_DUE,
	FRAME_SUSPENDED,
};


// What a blocking engine would hold on the C++ stack lives here instead. Nothing in the
// loop below reads anything that is not either in this block or supplied by the browser.
static unsigned short * _Frame = NULL;
static bool _Running = false;

static double _NextFrameDue = 0.0;
static unsigned int _Tick = 0;
static unsigned int _PresentCount = 0;

static int _CanvasWidth = 0;
static int _CanvasHeight = 0;
static int _DestX = 0;
static int _DestY = 0;
static int _DestWidth = 0;
static int _DestHeight = 0;

static BackendScaleMode _ScaleMode = BACKEND_SCALE_PIXELART;

static short _SineTable[SINE_STEPS];
static unsigned short _Palette[256];
static unsigned short * _DistTable = NULL;
static short _ColumnTerm[GAME_WIDTH];
static short _RowTerm[GAME_HEIGHT];
static short _DiagonalTerm[GAME_WIDTH + GAME_HEIGHT];


/// <summary>
/// Packs an eight bit per channel color into the 16 bit 565 the engine's surfaces hold.
/// </summary>
static unsigned short Pack_565(int red, int green, int blue)
{
	return((unsigned short)(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)));
}


/// <summary>
/// Fills the sine, palette, and radial distance tables the pattern is drawn from, so
/// that a frame costs table lookups rather than transcendental functions.
/// </summary>
static bool Build_Tables(void)
{
	for (int step = 0; step < SINE_STEPS; step++) {
		_SineTable[step] = (short)(sin((double)step * 2.0 * M_PI / (double)SINE_STEPS) * (double)SINE_SCALE);
	}

	for (int entry = 0; entry < 256; entry++) {
		double phase = (double)entry * 2.0 * M_PI / 256.0;
		int red = (int)(sin(phase) * 127.0 + 128.0);
		int green = (int)(sin(phase + 2.0 * M_PI / 3.0) * 127.0 + 128.0);
		int blue = (int)(sin(phase + 4.0 * M_PI / 3.0) * 127.0 + 128.0);
		_Palette[entry] = Pack_565(red, green, blue);
	}

	_DistTable = (unsigned short *)malloc(sizeof(unsigned short) * GAME_WIDTH * GAME_HEIGHT);
	if (_DistTable == NULL) {
		return(false);
	}

	for (int y = 0; y < GAME_HEIGHT; y++) {
		for (int x = 0; x < GAME_WIDTH; x++) {
			double dx = (double)(x - GAME_WIDTH / 2);
			double dy = (double)(y - GAME_HEIGHT / 2);
			_DistTable[y * GAME_WIDTH + x] = (unsigned short)((int)(sqrt(dx * dx + dy * dy) * 3.0) & SINE_MASK);
		}
	}

	return(true);
}


/// <summary>
/// Draws one hard edged rectangle, so that the scale modes have something with an edge
/// to filter. A plasma alone is smooth enough to hide the difference between them.
/// </summary>
static void Draw_Box(unsigned short * frame, int left, int top, int width, int height, unsigned short fill, unsigned short border)
{
	for (int y = top; y < top + height; y++) {
		if (y < 0 || y >= GAME_HEIGHT) {
			continue;
		}

		unsigned short * row = frame + y * GAME_WIDTH;
		bool edgerow = (y == top || y == top + height - 1);

		for (int x = left; x < left + width; x++) {
			if (x < 0 || x >= GAME_WIDTH) {
				continue;
			}
			row[x] = (edgerow || x == left || x == left + width - 1) ? border : fill;
		}
	}
}


/// <summary>
/// Renders the synthetic frame for one tick into the 565 buffer the backend presents.
/// </summary>
static void Render_Pattern(unsigned short * frame, unsigned int tick)
{
	int phasea = (int)(tick * 3) & SINE_MASK;
	int phaseb = (int)(tick * 2) & SINE_MASK;
	int phasec = (int)(tick * 5) & SINE_MASK;
	int phased = (int)(tick * 4) & SINE_MASK;

	for (int x = 0; x < GAME_WIDTH; x++) {
		_ColumnTerm[x] = _SineTable[(x * 3 + phasea) & SINE_MASK];
	}
	for (int y = 0; y < GAME_HEIGHT; y++) {
		_RowTerm[y] = _SineTable[(y * 5 + phaseb) & SINE_MASK];
	}
	for (int d = 0; d < GAME_WIDTH + GAME_HEIGHT; d++) {
		_DiagonalTerm[d] = _SineTable[(d * 2 + phasec) & SINE_MASK];
	}

	for (int y = 0; y < GAME_HEIGHT; y++) {
		unsigned short * row = frame + y * GAME_WIDTH;
		unsigned short const * distance = _DistTable + y * GAME_WIDTH;
		int rowterm = _RowTerm[y];

		for (int x = 0; x < GAME_WIDTH; x++) {
			int sum = _ColumnTerm[x] + rowterm + _DiagonalTerm[x + y] + _SineTable[(distance[x] + phased) & SINE_MASK];
			row[x] = _Palette[((sum + 4 * SINE_SCALE) >> 5) & 0xFF];
		}
	}

	// A one pixel lattice. Point sampling keeps these lines whole at any magnification;
	// linear filtering blurs them, which is the difference the scale modes exist to make.
	const unsigned short white = Pack_565(255, 255, 255);
	for (int y = 0; y < GAME_HEIGHT; y += 32) {
		unsigned short * row = frame + y * GAME_WIDTH;
		for (int x = 0; x < GAME_WIDTH; x++) {
			row[x] = white;
		}
	}
	for (int x = 0; x < GAME_WIDTH; x += 32) {
		for (int y = 0; y < GAME_HEIGHT; y++) {
			frame[y * GAME_WIDTH + x] = white;
		}
	}

	int span = GAME_WIDTH - 96;
	int travel = (int)(tick % (unsigned int)(span * 2));
	int boxx = (travel < span) ? travel : (span * 2 - travel);
	Draw_Box(frame, boxx, GAME_HEIGHT / 2 - 32, 96, 64, Pack_565(16, 16, 24), white);

	// The frame's own corners, so that a wrong destination rectangle is visible rather
	// than merely suspected.
	const unsigned short marker = Pack_565(255, 0, 0);
	Draw_Box(frame, 0, 0, 16, 16, marker, white);
	Draw_Box(frame, GAME_WIDTH - 16, 0, 16, 16, marker, white);
	Draw_Box(frame, 0, GAME_HEIGHT - 16, 16, 16, marker, white);
	Draw_Box(frame, GAME_WIDTH - 16, GAME_HEIGHT - 16, 16, 16, marker, white);
}


/// <summary>
/// Fits the game frame inside the canvas, centered and in proportion, the way video.cpp
/// fits it inside the window's client area.
/// </summary>
static void Update_Scale_Info(void)
{
	if (_CanvasWidth <= 0 || _CanvasHeight <= 0) {
		_DestX = 0;
		_DestY = 0;
		_DestWidth = 0;
		_DestHeight = 0;
		return;
	}

	double scalex = (double)_CanvasWidth / (double)GAME_WIDTH;
	double scaley = (double)_CanvasHeight / (double)GAME_HEIGHT;
	double scale = (scalex < scaley) ? scalex : scaley;

	_DestWidth = (int)((double)GAME_WIDTH * scale);
	_DestHeight = (int)((double)GAME_HEIGHT * scale);
	_DestX = (_CanvasWidth - _DestWidth) / 2;
	_DestY = (_CanvasHeight - _DestHeight) / 2;
}


/// <summary>
/// Picks up a canvas that the page has laid out at a new size and tells the renderer.
/// </summary>
/// <remarks>
/// A page resizes for reasons the engine never sees, so the drawable size is read back
/// each pass rather than tracked. The layout size is in CSS pixels and the drawing buffer
/// is in device pixels; conflating them is what makes a browser frame look soft.
/// </remarks>
static void Service_Canvas_Size(void)
{
	double csswidth = 0.0;
	double cssheight = 0.0;

	if (emscripten_get_element_css_size(CANVAS_SELECTOR, &csswidth, &cssheight) != EMSCRIPTEN_RESULT_SUCCESS) {
		return;
	}

	double ratio = emscripten_get_device_pixel_ratio();
	int width = (int)(csswidth * ratio + 0.5);
	int height = (int)(cssheight * ratio + 0.5);

	if (width <= 0 || height <= 0 || (width == _CanvasWidth && height == _CanvasHeight)) {
		return;
	}

	_CanvasWidth = width;
	_CanvasHeight = height;
	Backend_On_Resize(width, height);
	Update_Scale_Info();
}


/// <summary>
/// Is the page showing us? A hidden tab is not composited and its callback may not run
/// at all, which is the browser's own expression of the engine's GameInFocus spin.
/// </summary>
static bool Is_Suspended(void)
{
	EmscriptenVisibilityChangeEvent visibility;

	if (emscripten_get_visibility_status(&visibility) != EMSCRIPTEN_RESULT_SUCCESS) {
		return(false);
	}
	return(visibility.hidden != 0);
}


/// <summary>
/// Has enough wall clock passed for the next simulation step? This is the half of
/// Sync_Delay that survives the port; the half that sleeps does not.
/// </summary>
static bool Frame_Is_Due(double now)
{
	return(now >= _NextFrameDue);
}


/// <summary>
/// Advances the demo by exactly one step and returns.
/// </summary>
static void Advance(void)
{
	_Tick++;
	_NextFrameDue += FRAME_INTERVAL;
	Render_Pattern(_Frame, _Tick);
}


/// <summary>
/// Runs one pass of the demo: service the platform, advance as far as the clock allows,
/// and present. It always returns, and it is the only thing the browser calls.
/// </summary>
/// <returns>FrameResult; What the pass did, so a caller can tell a paced skip from a
/// suspended page.</returns>
static FrameResult Demo_Frame(void)
{
	Service_Canvas_Size();

	if (Is_Suspended()) {
		return(FRAME_SUSPENDED);
	}

	double now = emscripten_get_now();

	// A tab that was throttled or a machine that stalled leaves the clock arbitrarily
	// far ahead. The simulation catches up to a bound and then resynchronizes, because
	// working through the whole arrears is the blocking loop being designed out.
	int advanced = 0;
	while (Frame_Is_Due(now) && advanced < MAX_CATCHUP_FRAMES) {
		Advance();
		advanced++;
	}
	if (Frame_Is_Due(now)) {
		_NextFrameDue = now + FRAME_INTERVAL;
	}

	// Presentation is unconditional. The engine's loop is not paced by it, and a browser
	// that composites a frame we did not draw into shows a cleared buffer.
	Backend_Present(_Frame, GAME_WIDTH * (int)sizeof(unsigned short), _DestX, _DestY, _DestWidth, _DestHeight, _ScaleMode);
	_PresentCount++;

	return(advanced > 0 ? FRAME_ADVANCED : FRAME_NOT_DUE);
}


/// <summary>
/// Names the scale mode for the page's status line.
/// </summary>
static char const * Scale_Mode_Name(void)
{
	switch (_ScaleMode) {
		case BACKEND_SCALE_NEAREST:
			return("NEAREST");

		case BACKEND_SCALE_LINEAR:
			return("LINEAR");

		default:
			return("PIXELART");
	}
}


/// <summary>
/// The browser's entry into the demo. Everything it needs is already in place; it does
/// one pass and hands the thread back.
/// </summary>
static void Demo_Main_Loop(void)
{
	if (!_Running) {
		return;
	}

	Demo_Frame();

	// The status line is read back by the verification pass, and updating it is a call
	// into JavaScript, so it happens on a slow cadence rather than every frame.
	if ((_PresentCount % 30) == 0) {
		EM_ASM({
			if (typeof OpenTS_Status === "function") {
				OpenTS_Status(UTF8ToString($0), UTF8ToString($1), $2, $3, $4, $5, $6, $7);
			}
		}, Backend_Renderer_Name(), Scale_Mode_Name(), _CanvasWidth, _CanvasHeight,
			_DestX, _DestY, _DestWidth, _DestHeight);
	}
}


extern "C" {


/// <summary>
/// Selects the filter the frame is scaled with. The page calls this to exercise all
/// three without a rebuild.
/// </summary>
EMSCRIPTEN_KEEPALIVE void Demo_Set_Scale_Mode(int mode)
{
	switch (mode) {
		case 0:
			_ScaleMode = BACKEND_SCALE_NEAREST;
			break;

		case 1:
			_ScaleMode = BACKEND_SCALE_LINEAR;
			break;

		default:
			_ScaleMode = BACKEND_SCALE_PIXELART;
			break;
	}
}


/// <summary>
/// Reports how many frames have reached the canvas, which is what tells an automated
/// check that the loop is running rather than merely started.
/// </summary>
EMSCRIPTEN_KEEPALIVE int Demo_Present_Count(void)
{
	return((int)_PresentCount);
}


/// <summary>
/// Reports the renderer bgfx settled on.
/// </summary>
EMSCRIPTEN_KEEPALIVE char const * Demo_Renderer_Name(void)
{
	return(Backend_Renderer_Name());
}


}


int main(void)
{
	if (!Build_Tables()) {
		return(1);
	}

	_Frame = (unsigned short *)malloc(sizeof(unsigned short) * GAME_WIDTH * GAME_HEIGHT);
	if (_Frame == NULL) {
		return(1);
	}
	memset(_Frame, 0, sizeof(unsigned short) * GAME_WIDTH * GAME_HEIGHT);

	double csswidth = 0.0;
	double cssheight = 0.0;
	emscripten_get_element_css_size(CANVAS_SELECTOR, &csswidth, &cssheight);

	double ratio = emscripten_get_device_pixel_ratio();
	_CanvasWidth = (int)(csswidth * ratio + 0.5);
	_CanvasHeight = (int)(cssheight * ratio + 0.5);
	if (_CanvasWidth <= 0 || _CanvasHeight <= 0) {
		_CanvasWidth = GAME_WIDTH;
		_CanvasHeight = GAME_HEIGHT;
	}

	// WebGL 2 is the only thing a page has, and bgfx reaches it through its OpenGL ES
	// renderer. Vsync is the browser's to decide, so nothing is asked of it here.
	if (!Backend_Init(CANVAS_SELECTOR, _CanvasWidth, _CanvasHeight, BACKEND_RENDERER_OPENGLES, false)) {
		emscripten_console_error("Backend_Init failed.");
		return(1);
	}

	if (!Backend_Set_Frame_Size(GAME_WIDTH, GAME_HEIGHT)) {
		emscripten_console_error("Backend_Set_Frame_Size failed.");
		Backend_Shutdown();
		return(1);
	}

	Update_Scale_Info();

	_NextFrameDue = emscripten_get_now();
	_Running = true;

	// Zero asks for the browser's own animation cadence, and the loop is registered
	// without simulating an infinite one so that main returns to the browser the way
	// every entry point into wasm has to.
	emscripten_set_main_loop(Demo_Main_Loop, 0, false);
	return(0);
}
