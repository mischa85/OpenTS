/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// win32window.h states what this stands in for; what follows is why it is shaped this way.
//
// Geometry. The canvas is the screen: nothing sits around it, so the window frame metrics
// are genuinely zero and a position on it is a screen position. Where a window sits within
// it is the window manager's to say, so win32user.h answers for every handle its registry
// knows and the canvas answers for the rest -- the null handle among them. Reversing that
// order is what makes every control cover the whole frame and swallow the clicks meant for
// the map behind it.
//
// The pointer. Windows composites a cursor over the frame and so does a page, through the
// canvas's CSS cursor property. The game's shape becomes an image the page draws, which
// keeps the property wincursor.h relies on -- the cursor never touches a game surface, and
// moving it costs no frame. Setting that image is also what hides the browser's own arrow:
// the two are the same property, so the page cannot show both at once and no second
// pointer can appear beside the game's.
//
// The code page. The engine's bytes are Windows-1252 and its wide characters are UTF-16.
// Both are tables, the host has everything needed to apply them, and neither has a browser
// anywhere in it, so these are implemented rather than stubbed.

#include "always.h"

#include "win32window.h"

#if defined(__EMSCRIPTEN__)

#include "browser.h"
#include "misc.h"
#include "vidscale.h"
#include "video.h"
#include "win.h"
#include "win32user.h"
#include "wincursor.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>


/*
** Windows-1252 is Latin-1 apart from the range 0x80 to 0x9F, which this table spells out.
** The five positions the code page leaves undefined carry the C1 control of the same
** value, which is what the Windows table does with them and what makes every byte a round
** trip. peresource.cpp holds the same mappings for the one direction it needs; it has no
** byte to return for a wide character outside them and answers with a question mark, which
** is what this does too.
*/
static unsigned short const _HighRange[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

// A browser refuses to draw a cursor image larger than this, and shows nothing at all
// rather than a clipped one.
static int const MAX_CURSOR_SIZE = 128;

// The block of resource identifiers Windows keeps for the cursors, icons, and bitmaps it
// supplies itself. The IDC_ names are numbers inside it, and nothing else is a system
// cursor.
static ULONG_PTR const FIRST_SYSTEM_CURSOR = 32512;
static ULONG_PTR const LAST_SYSTEM_CURSOR = 32767;


/*
** A cursor, once it is a thing the page can be handed. The pixels are not kept: the CSS
** value they were encoded into is the whole of what SetCursor needs, and it is built once
** because the encoding is the expensive part of changing pointers.
*/
struct Win32CursorClass
{
	std::string Css;
	bool IsShared;
};

static std::vector<Win32CursorClass *> _Cursors;
static Win32CursorClass * _Current = nullptr;

/*
** The display count Windows keeps. It starts at zero because a page always has a pointing
** device, and the engine reads it back -- Capture_Mouse raises it until it is no longer
** negative -- so it has to count rather than answer a constant.
*/
static int _DisplayCount = 0;

static HWND _Capture = nullptr;


/*
** ---------------------------------------------------------------------------------------
** Window geometry.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Fetches the canvas as a rectangle.
/// </summary>
/// <returns>bool; Has the page laid the canvas out yet?</returns>
static bool Canvas_Rect(LPRECT rect)
{
	int width = Browser_Canvas_Width();
	int height = Browser_Canvas_Height();

	if (width <= 0 || height <= 0) {
		SetRectEmpty(rect);
		return(false);
	}

	rect->left = 0;
	rect->top = 0;
	rect->right = width;
	rect->bottom = height;
	return(true);
}


/// <summary>
/// Fetches the size of a window's client area.
/// </summary>
/// <remarks>
/// The window manager is asked first, because it is the only thing that knows where a
/// control is; the canvas answers for a handle it has never heard of, which is the whole
/// screen the engine draws on.
/// </remarks>
BOOL GetClientRect(HWND window, LPRECT rect)
{
	if (rect == nullptr) {
		return(FALSE);
	}

	if (Win32_User_Client_Rect(window, rect)) {
		return(TRUE);
	}

	return(Canvas_Rect(rect) ? TRUE : FALSE);
}


/// <summary>
/// Fetches the rectangle a window occupies on the desktop.
/// </summary>
/// <remarks>
/// There is no desktop and no non-client frame, so a window's own rectangle is where the
/// manager put it, and a handle it does not know covers the canvas.
/// </remarks>
BOOL GetWindowRect(HWND window, LPRECT rect)
{
	if (rect == nullptr) {
		return(FALSE);
	}

	if (Win32_User_Window_Rect(window, rect)) {
		return(TRUE);
	}

	return(Canvas_Rect(rect) ? TRUE : FALSE);
}


/// <summary>
/// Moves a position from a window's client area onto the screen.
/// </summary>
/// <remarks>
/// The canvas is the screen, so the whole of the conversion is where the window's client
/// area begins on it. A window the manager does not know begins at the origin.
/// </remarks>
BOOL ClientToScreen(HWND window, LPPOINT point)
{
	if (point == nullptr) {
		return(FALSE);
	}

	POINT origin;

	if (Win32_User_Client_Origin(window, &origin)) {
		point->x += origin.x;
		point->y += origin.y;
	}

	return(TRUE);
}


BOOL ScreenToClient(HWND window, LPPOINT point)
{
	if (point == nullptr) {
		return(FALSE);
	}

	POINT origin;

	if (Win32_User_Client_Origin(window, &origin)) {
		point->x -= origin.x;
		point->y -= origin.y;
	}

	return(TRUE);
}


/// <summary>
/// Grows a client rectangle by the window frame that would surround it.
/// </summary>
/// <remarks>
/// A canvas carries no border, caption, or menu, so the rectangle it needs to hold a
/// client area of a given size is that client area.
/// </remarks>
BOOL AdjustWindowRect(LPRECT rect, DWORD, BOOL menu)
{
	if (rect == nullptr || menu != FALSE) {
		return(FALSE);
	}

	return(TRUE);
}


BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD)
{
	return(AdjustWindowRect(rect, style, menu));
}


/// <summary>
/// Answers one of the system's measurements.
/// </summary>
/// <remarks>
/// The screen is the canvas, and the window furniture whose sizes the rest of these
/// report is absent rather than unknown, so zero is its measurement.
/// </remarks>
int GetSystemMetrics(int index)
{
	switch (index) {
		case SM_CXSCREEN:
		case SM_CXFULLSCREEN:
			return(Browser_Canvas_Width());

		case SM_CYSCREEN:
		case SM_CYFULLSCREEN:
			return(Browser_Canvas_Height());

		case SM_CXVSCROLL:
		case SM_CYHSCROLL:
		case SM_CYCAPTION:
		case SM_CXFIXEDFRAME:
		case SM_CYFIXEDFRAME:
		case SM_CXSIZEFRAME:
		case SM_CYSIZEFRAME:
			return(0);

		case SM_CXBORDER:
		case SM_CYBORDER:
			return(1);

		/*
		 * How far a press may wander before it is a drag, and how far two presses may be
		 * apart and still be a double click. Windows makes both configurable; a page has
		 * nowhere to read a preference from, so these are its defaults.
		 */
		case SM_CXDRAG:
		case SM_CYDRAG:
			return(4);

		case SM_CXDOUBLECLK:
		case SM_CYDOUBLECLK:
			return(4);

		/*
		 * A swapped pair reaches the page already swapped: the button numbers in a mouse
		 * event are the ones the user's setting produced.
		 */
		case SM_SWAPBUTTON:
			return(0);

		default:
			return(WIN32_UNSUPPORTED("GetSystemMetrics: a measurement with no counterpart on a page", 0));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The pointer.
** ---------------------------------------------------------------------------------------
*/


int Win32_Window_Max_Cursor_Size(void)
{
	return(MAX_CURSOR_SIZE);
}


/// <summary>
/// Puts the selected cursor onto the canvas.
/// </summary>
/// <remarks>
/// The null cursor falls back to the page's own pointer rather than to nothing. Windows
/// leaves the screen without one only until the next mouse move, when WM_SETCURSOR puts the
/// window class's cursor back up; a page raises no such message and registers no window
/// class, so drawing nothing here would leave the player with nothing to point with. The
/// blank cursor is how a caller asks for no pointer at all.
/// </remarks>
static void Apply_Cursor(void)
{
	static std::string _applied;

	char const * css = (_Current != nullptr) ? _Current->Css.c_str() : "default";

	if (_applied == css) {
		return;
	}

	_applied = css;

	// The document is looked for rather than assumed, because this also builds into a test
	// harness, which runs on a WebAssembly host that has no page at all.
	EM_ASM({
		var element = (typeof document === "undefined") ? null : document.querySelector(UTF8ToString($0));
		if (element) {
			element.style.cursor = UTF8ToString($1);
		}
	}, Browser_Canvas_Selector(), css);
}


/// <summary>
/// Finds the cursor a handle names.
/// </summary>
/// <returns>The cursor, or NULL for the null handle and for one this port did not
/// build.</returns>
static Win32CursorClass * Find_Cursor(HCURSOR cursor)
{
	if (cursor == nullptr) {
		return(nullptr);
	}

	for (unsigned index = 0; index < _Cursors.size(); index++) {
		if ((HCURSOR)_Cursors[index] == cursor) {
			return(_Cursors[index]);
		}
	}

	return(nullptr);
}


static void Append_Big_Endian(std::vector<unsigned char> & out, unsigned long value)
{
	out.push_back((unsigned char)((value >> 24) & 0xFF));
	out.push_back((unsigned char)((value >> 16) & 0xFF));
	out.push_back((unsigned char)((value >> 8) & 0xFF));
	out.push_back((unsigned char)(value & 0xFF));
}


static unsigned long Checksum_32(unsigned char const * data, std::size_t length)
{
	static unsigned long _table[256];
	static bool _built = false;

	if (!_built) {
		for (unsigned long index = 0; index < 256; index++) {
			unsigned long value = index;
			for (int bit = 0; bit < 8; bit++) {
				value = (value & 1) ? (0xEDB88320UL ^ (value >> 1)) : (value >> 1);
			}
			_table[index] = value;
		}
		_built = true;
	}

	unsigned long crc = 0xFFFFFFFFUL;
	for (std::size_t index = 0; index < length; index++) {
		crc = _table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	}
	return(crc ^ 0xFFFFFFFFUL);
}


/// <summary>
/// Appends one PNG chunk, with its length and its checksum around it.
/// </summary>
static void Append_Chunk(std::vector<unsigned char> & out, char const * type, std::vector<unsigned char> const & body)
{
	Append_Big_Endian(out, (unsigned long)body.size());

	std::vector<unsigned char> checked;
	checked.insert(checked.end(), type, type + 4);
	checked.insert(checked.end(), body.begin(), body.end());

	out.insert(out.end(), checked.begin(), checked.end());
	Append_Big_Endian(out, Checksum_32(checked.data(), checked.size()));
}


/// <summary>
/// Wraps bytes in a zlib stream that stores rather than compresses them.
/// </summary>
/// <remarks>
/// A cursor is a few kilobytes and is built once per shape frame, so what the deflate
/// format calls a stored block costs nothing worth a compressor for.
/// </remarks>
static std::vector<unsigned char> Store_Deflate(std::vector<unsigned char> const & raw)
{
	std::vector<unsigned char> out;

	out.push_back(0x78);
	out.push_back(0x01);

	std::size_t offset = 0;

	do {
		std::size_t remaining = raw.size() - offset;
		unsigned int length = (remaining > 0xFFFF) ? 0xFFFF : (unsigned int)remaining;
		bool last = ((offset + length) == raw.size());

		out.push_back(last ? 0x01 : 0x00);
		out.push_back((unsigned char)(length & 0xFF));
		out.push_back((unsigned char)((length >> 8) & 0xFF));
		out.push_back((unsigned char)(~length & 0xFF));
		out.push_back((unsigned char)((~length >> 8) & 0xFF));
		out.insert(out.end(), raw.begin() + offset, raw.begin() + offset + length);

		offset += length;
	}
	while (offset < raw.size());

	unsigned long a = 1;
	unsigned long b = 0;
	for (std::size_t index = 0; index < raw.size(); index++) {
		a = (a + raw[index]) % 65521;
		b = (b + a) % 65521;
	}
	Append_Big_Endian(out, (b << 16) | a);

	return(out);
}


/// <summary>
/// Turns a block of pixels into a PNG.
/// </summary>
/// <remarks>
/// A page takes a cursor image as a URL, so the pixels have to arrive in a format a
/// browser decodes. PNG is the one that carries an alpha channel everywhere.
/// </remarks>
static std::vector<unsigned char> Encode_PNG(unsigned long const * pixels, int width, int height)
{
	std::vector<unsigned char> raw;
	raw.reserve((std::size_t)height * (1 + (std::size_t)width * 4));

	for (int y = 0; y < height; y++) {

		// Each row carries the filter it was encoded with, and these are not filtered.
		raw.push_back(0);

		for (int x = 0; x < width; x++) {
			unsigned long pixel = pixels[(std::size_t)y * width + x];
			raw.push_back((unsigned char)((pixel >> 16) & 0xFF));
			raw.push_back((unsigned char)((pixel >> 8) & 0xFF));
			raw.push_back((unsigned char)(pixel & 0xFF));
			raw.push_back((unsigned char)((pixel >> 24) & 0xFF));
		}
	}

	std::vector<unsigned char> out;
	unsigned char const signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	out.insert(out.end(), signature, signature + sizeof(signature));

	std::vector<unsigned char> header;
	Append_Big_Endian(header, (unsigned long)width);
	Append_Big_Endian(header, (unsigned long)height);
	header.push_back(8);		// Bits per channel.
	header.push_back(6);		// Red, green, blue, and alpha.
	header.push_back(0);		// The only compression the format has.
	header.push_back(0);		// The only filtering the format has.
	header.push_back(0);		// Not interlaced.
	Append_Chunk(out, "IHDR", header);

	Append_Chunk(out, "IDAT", Store_Deflate(raw));
	Append_Chunk(out, "IEND", std::vector<unsigned char>());

	return(out);
}


static std::string Base64(std::vector<unsigned char> const & data)
{
	static char const * const _alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string out;
	out.reserve(((data.size() + 2) / 3) * 4);

	for (std::size_t index = 0; index < data.size(); index += 3) {
		std::size_t remaining = data.size() - index;

		unsigned long group = (unsigned long)data[index] << 16;
		if (remaining > 1) group |= (unsigned long)data[index + 1] << 8;
		if (remaining > 2) group |= (unsigned long)data[index + 2];

		out.push_back(_alphabet[(group >> 18) & 0x3F]);
		out.push_back(_alphabet[(group >> 12) & 0x3F]);
		out.push_back(remaining > 1 ? _alphabet[(group >> 6) & 0x3F] : '=');
		out.push_back(remaining > 2 ? _alphabet[group & 0x3F] : '=');
	}

	return(out);
}


HCURSOR Win32_Window_Create_Cursor(unsigned long const * pixels, int width, int height, int hotx, int hoty)
{
	if (pixels == nullptr || width <= 0 || height <= 0) {
		return(nullptr);
	}

	if (width > MAX_CURSOR_SIZE || height > MAX_CURSOR_SIZE) {
		return(WIN32_UNSUPPORTED("Win32_Window_Create_Cursor: an image past the size a browser will draw", (HCURSOR)nullptr));
	}

	if (hotx < 0) hotx = 0;
	if (hoty < 0) hoty = 0;
	if (hotx >= width) hotx = width - 1;
	if (hoty >= height) hoty = height - 1;

	Win32CursorClass * cursor = new Win32CursorClass;

	// A page that cannot decode the image falls back to whatever the keyword names, so the
	// pointer degrades to an arrow rather than disappearing.
	cursor->Css = "url(\"data:image/png;base64," + Base64(Encode_PNG(pixels, width, height)) + "\") "
		+ std::to_string(hotx) + " " + std::to_string(hoty) + ", auto";
	cursor->IsShared = false;

	_Cursors.push_back(cursor);
	return((HCURSOR)cursor);
}


/// <summary>
/// Fetches one of the cursors the system provides.
/// </summary>
/// <remarks>
/// A page has its own names for these, and they are the ones a browser draws in its own
/// idiom, which is what a system cursor is for.
/// </remarks>
HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name)
{
	static Win32CursorClass * _shared[4];

	struct {
		LPCSTR Name;
		char const * Css;
	} const _standard[4] = {
		{ IDC_ARROW, "default" },
		{ IDC_WAIT, "wait" },
		{ IDC_NO, "not-allowed" },
		{ IDC_HAND, "pointer" }
	};

	if (instance != nullptr) {
		return(WIN32_UNSUPPORTED("LoadCursorA: a cursor resource held in a module", (HCURSOR)nullptr));
	}

	for (int index = 0; index < 4; index++) {
		if (_standard[index].Name != name) {
			continue;
		}

		if (_shared[index] == nullptr) {
			_shared[index] = new Win32CursorClass;
			_shared[index]->Css = _standard[index].Css;
			_shared[index]->IsShared = true;
			_Cursors.push_back(_shared[index]);
		}

		return((HCURSOR)_shared[index]);
	}

	/*
	** Windows reserves the identifiers from 32512 up for the resources it supplies itself,
	** and those are the only ones a null instance reaches. A name outside that block asks
	** a module for one of its own resources, and NULL is what Windows answers when there
	** is no module to ask -- the result, not a gap. The engine's own cursor arrives that
	** way, because this target hands the engine no module handle to hold it against, and
	** the game draws that cursor onto its surface rather than through the pointer.
	*/
	if ((ULONG_PTR)name < FIRST_SYSTEM_CURSOR || (ULONG_PTR)name > LAST_SYSTEM_CURSOR) {
		return(nullptr);
	}

	return(WIN32_UNSUPPORTED("LoadCursorA: a system cursor with no counterpart on a page", (HCURSOR)nullptr));
}


/// <summary>
/// Fetches the cursor a page draws nothing for.
/// </summary>
/// <remarks>
/// Windows has no such cursor because the null one already means this, and because the
/// class cursor is standing by to end it. Neither holds on a page, so hiding the pointer
/// and having no pointer to show have to be said apart.
/// </remarks>
HCURSOR Win32_Window_Blank_Cursor(void)
{
	static Win32CursorClass * _blank = nullptr;

	if (_blank == nullptr) {
		_blank = new Win32CursorClass;
		_blank->Css = "none";
		_blank->IsShared = true;
		_Cursors.push_back(_blank);
	}

	return((HCURSOR)_blank);
}


HCURSOR SetCursor(HCURSOR cursor)
{
	Win32CursorClass * previous = _Current;

	if (cursor != nullptr) {
		_Current = Find_Cursor(cursor);
		if (_Current == nullptr) {
			Win32_Unsupported_Reached("SetCursor: a cursor this port did not build");
		}
	} else {
		_Current = nullptr;
	}

	Apply_Cursor();
	return((HCURSOR)previous);
}


/// <summary>
/// Raises or lowers the pointer's display count.
/// </summary>
/// <remarks>
/// Windows asks the window what the pointer should be, through WM_SETCURSOR, on the next
/// move after anything has disturbed it, and puts the window class's own cursor back when
/// the window declines. A page raises no such message and registers no window class, so the
/// game's own cursor stands in for the class cursor and the pointer is put back here
/// instead. The count is the bookkeeping described where it is declared, and nothing on a
/// canvas turns on it.
/// </remarks>
/// <returns>int; The count after the change.</returns>
int ShowCursor(BOOL show)
{
	_DisplayCount += (show != FALSE) ? 1 : -1;

	Win_Cursor_Apply();
	return(_DisplayCount);
}


BOOL DestroyCursor(HCURSOR cursor)
{
	for (unsigned index = 0; index < _Cursors.size(); index++) {
		if ((HCURSOR)_Cursors[index] != cursor) {
			continue;
		}

		// The shared ones outlive every caller, exactly as they do on Windows.
		if (_Cursors[index]->IsShared) {
			return(FALSE);
		}

		if (_Current == _Cursors[index]) {
			_Current = nullptr;
			Apply_Cursor();
		}

		delete _Cursors[index];
		_Cursors.erase(_Cursors.begin() + index);
		return(TRUE);
	}

	return(FALSE);
}


/// <summary>
/// Fetches where the pointer is, on the desktop.
/// </summary>
/// <remarks>
/// The page reports the pointer in the frame's own pixels, because that is what the rest
/// of the engine wants of it. A screen position is the canvas's, so it goes back out
/// through the scaling; client and screen coincide, so there is nothing further to add.
/// </remarks>
BOOL GetCursorPos(LPPOINT point)
{
	if (point == nullptr) {
		return(FALSE);
	}

	point->x = Browser_Mouse_X();
	point->y = Browser_Mouse_Y();
	Game_Point_To_Window(*point);
	return(TRUE);
}


BOOL SetCursorPos(int, int) { return(WIN32_STUB(FALSE)); }


/*
** A page delivers every mouse event over the canvas to the canvas, whoever asked for it,
** so capture is bookkeeping the engine reads back rather than a routing change.
*/
HWND SetCapture(HWND window)
{
	HWND previous = _Capture;
	_Capture = window;
	return(previous);
}


BOOL ReleaseCapture(void)
{
	_Capture = nullptr;
	return(TRUE);
}


HWND GetCapture(void)
{
	return(_Capture);
}


/// <summary>
/// Confines the pointer to a rectangle.
/// </summary>
/// <remarks>
/// The page cannot hold a pointer inside anything, but the engine only ever asks for the
/// window it already owns, and every position it reads has been pulled onto the frame
/// before it sees it. A smaller rectangle is a confinement nothing here provides.
/// </remarks>
BOOL ClipCursor(RECT const * rect)
{
	if (rect == nullptr) {
		return(TRUE);
	}

	RECT canvas;
	if (!Canvas_Rect(&canvas)) {
		return(FALSE);
	}

	if (rect->left > canvas.left || rect->top > canvas.top
	||	rect->right < canvas.right || rect->bottom < canvas.bottom) {
		return(WIN32_UNSUPPORTED("ClipCursor: a rectangle smaller than the canvas", FALSE));
	}

	return(TRUE);
}


/*
** ---------------------------------------------------------------------------------------
** The display.
** ---------------------------------------------------------------------------------------
*/


HMONITOR MonitorFromWindow(HWND, DWORD) { return(WIN32_STUB((HMONITOR)nullptr)); }
BOOL GetMonitorInfoA(HMONITOR, LPMONITORINFO) { return(WIN32_STUB(FALSE)); }


/*
** A page has no list of display modes to walk. What stands in for one is the set of frame
** sizes the game can honestly be rendered at here: the canvas as the page has laid it out,
** whatever the game is running at now, and the familiar 4:3 and widescreen sizes that fit
** on the display the tab is on. A canvas is whatever size the page asks for, so every one
** of them is a size the renderer really can produce; nothing larger than the screen is
** offered, because a window cannot be opened bigger than the display holding it.
**
** Everything here is measured in CSS pixels, as the frame is. A display carrying two
** device pixels for each CSS pixel shows the same modes as one carrying a single pixel,
** and shows them more sharply.
*/
struct DisplayModeEntry
{
	int Width;
	int Height;
};

static const DisplayModeEntry _DisplayLadder[] = {
	{ 640, 400 },	{ 640, 480 },	{ 800, 600 },	{ 1024, 768 },	{ 1152, 864 },
	{ 1280, 720 },	{ 1280, 800 },	{ 1280, 960 },	{ 1280, 1024 },	{ 1366, 768 },
	{ 1440, 900 },	{ 1600, 900 },	{ 1600, 1200 },	{ 1680, 1050 },	{ 1920, 1080 },
	{ 1920, 1200 },	{ 2048, 1152 },	{ 2560, 1440 },	{ 2560, 1600 },
};

static DisplayModeEntry _DisplayModes[32];
static int _DisplayModeCount = 0;


static void Add_Display_Mode(int width, int height)
{
	if (width < 640 || height < 400) return;
	if (_DisplayModeCount >= (int)(sizeof(_DisplayModes) / sizeof(_DisplayModes[0]))) return;

	for (int index = 0; index < _DisplayModeCount; index++) {
		if (_DisplayModes[index].Width == width && _DisplayModes[index].Height == height) return;
	}

	_DisplayModes[_DisplayModeCount].Width = width;
	_DisplayModes[_DisplayModeCount].Height = height;
	_DisplayModeCount++;
}


static void Build_Display_Modes(void)
{
	_DisplayModeCount = 0;

	int screenwidth = Browser_Screen_Width();
	int screenheight = Browser_Screen_Height();

	// A page that will not say how big the display is gets the sizes a laptop can be
	// relied on to manage rather than the whole ladder.
	if (screenwidth <= 0 || screenheight <= 0) {
		screenwidth = 1920;
		screenheight = 1080;
	}

	for (unsigned index = 0; index < sizeof(_DisplayLadder) / sizeof(_DisplayLadder[0]); index++) {
		if (_DisplayLadder[index].Width <= screenwidth && _DisplayLadder[index].Height <= screenheight) {
			Add_Display_Mode(_DisplayLadder[index].Width, _DisplayLadder[index].Height);
		}
	}

	/*
	** The window itself is always on the list, and choosing it is how the player asks for
	** the frame to keep following the window, so it has to be the size the window actually
	** produces rather than the size it was measured at.
	*/
	int canvaswidth = Browser_Canvas_CSS_Width();
	int canvasheight = Browser_Canvas_CSS_Height();

	if (canvaswidth > 0 && canvasheight > 0) {
		Video_Clamp_Frame_Size(canvaswidth, canvasheight);
		Add_Display_Mode(canvaswidth, canvasheight);
	}

	// So that the list the player is looking at has the resolution they are looking at it in.
	Add_Display_Mode(VideoModeWidth, VideoModeHeight);

	std::sort(_DisplayModes, _DisplayModes + _DisplayModeCount,
		[](DisplayModeEntry const & lhs, DisplayModeEntry const & rhs) {
			return((lhs.Width != rhs.Width) ? (lhs.Width < rhs.Width) : (lhs.Height < rhs.Height));
		});
}


BOOL EnumDisplaySettingsA(LPCSTR, DWORD mode, LPDEVMODEA devmode)
{
	if (devmode == nullptr) return(FALSE);

	int width = 0;
	int height = 0;

	if (mode == ENUM_CURRENT_SETTINGS) {
		width = Browser_Canvas_CSS_Width();
		height = Browser_Canvas_CSS_Height();
		if (width <= 0 || height <= 0) return(FALSE);
	} else {
		// The list is taken afresh at the start of an enumeration, as a driver's is, so a
		// canvas that resizes part way through does not shorten what the caller is reading.
		if (mode == 0) {
			Build_Display_Modes();
		}
		if ((int)mode >= _DisplayModeCount) return(FALSE);
		width = _DisplayModes[mode].Width;
		height = _DisplayModes[mode].Height;
	}

	memset(devmode, 0, sizeof(*devmode));
	devmode->dmSize = sizeof(*devmode);
	devmode->dmBitsPerPel = 16;
	devmode->dmPelsWidth = (DWORD)width;
	devmode->dmPelsHeight = (DWORD)height;
	devmode->dmDisplayFrequency = 60;
	return(TRUE);
}

/*
** ---------------------------------------------------------------------------------------
** The code page.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Is this a code page these conversions know?
/// </summary>
/// <remarks>
/// The engine's text is Windows-1252 throughout, and CP_ACP is that code page on the
/// systems it was written for.
/// </remarks>
static bool Is_Windows_1252(UINT codepage)
{
	return(codepage == CP_ACP || codepage == 1252);
}


static unsigned short Widen_Character(unsigned char byte)
{
	if (byte < 0x80 || byte >= 0xA0) {
		return(byte);
	}

	return(_HighRange[byte - 0x80]);
}


/// <summary>
/// Finds the byte that carries a wide character, if the code page has one.
/// </summary>
/// <returns>The byte, or -1 when the character cannot be written in Windows-1252.</returns>
/// <remarks>
/// Windows also carries a best fit table that answers a near miss with a resemblance -- a
/// typographic dash with a hyphen, an accented letter with its bare one. This has none, so
/// anything the code page does not hold outright is replaced.
/// </remarks>
static int Narrow_Character(unsigned short code)
{
	if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
		return((int)code);
	}

	for (int index = 0; index < 32; index++) {
		if (_HighRange[index] == code) {
			return(0x80 + index);
		}
	}

	return(-1);
}


/// <summary>
/// Converts Windows-1252 text into UTF-16.
/// </summary>
/// <param name="multibytecount">How many bytes to convert, or -1 for a terminated string,
/// whose terminator is converted along with it.</param>
/// <param name="widecount">How much room the destination has, or zero to ask how much is
/// needed.</param>
/// <returns>int; The number of wide characters produced, or zero on failure.</returns>
int MultiByteToWideChar(UINT codepage, DWORD flags, LPCSTR multibyte, int multibytecount, LPWSTR wide, int widecount)
{
	if (multibyte == nullptr || multibytecount == 0 || widecount < 0 || (widecount > 0 && wide == nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	if (!Is_Windows_1252(codepage)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("MultiByteToWideChar: a code page other than Windows-1252", 0));
	}

	// MB_PRECOMPOSED is the default and asks for what this produces anyway. Anything else
	// changes the answer rather than describing it.
	if ((flags & ~(DWORD)MB_PRECOMPOSED) != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("MultiByteToWideChar: a conversion flag with no implementation", 0));
	}

	/*
	 * A negative count means the string names its own end, and the terminator is part of
	 * what is converted. A given count is a length in bytes and a null inside it is a
	 * character like any other.
	 */
	int length = multibytecount;
	if (length < 0) {
		length = (int)strlen(multibyte) + 1;
	}

	if (widecount == 0) {
		return(length);
	}

	if (widecount < length) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	for (int index = 0; index < length; index++) {
		wide[index] = (WCHAR)Widen_Character((unsigned char)multibyte[index]);
	}

	return(length);
}


/// <summary>
/// Converts UTF-16 text into Windows-1252.
/// </summary>
/// <param name="widecount">How many characters to convert, or -1 for a terminated string,
/// whose terminator is converted along with it.</param>
/// <param name="multibytecount">How much room the destination has, or zero to ask how much
/// is needed.</param>
/// <param name="defaultchar">What to write where the code page has no byte, or NULL for a
/// question mark.</param>
/// <param name="useddefaultchar">Set when at least one character had to be replaced.</param>
/// <returns>int; The number of bytes produced, or zero on failure.</returns>
int WideCharToMultiByte(UINT codepage, DWORD flags, LPCWSTR wide, int widecount, LPSTR multibyte, int multibytecount, LPCSTR defaultchar, LPBOOL useddefaultchar)
{
	if (wide == nullptr || widecount == 0 || multibytecount < 0 || (multibytecount > 0 && multibyte == nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	if (!Is_Windows_1252(codepage)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WideCharToMultiByte: a code page other than Windows-1252", 0));
	}

	if (flags != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WideCharToMultiByte: a conversion flag with no implementation", 0));
	}

	int length = widecount;
	if (length < 0) {
		length = 0;
		while (wide[length] != 0) length++;
		length++;
	}

	char replacement = (defaultchar != nullptr) ? defaultchar[0] : '?';
	bool replaced = false;

	std::string out;
	out.reserve((std::size_t)length);

	for (int index = 0; index < length; index++) {
		unsigned short code = (unsigned short)wide[index];

		/*
		 * A character outside the basic plane arrives as a pair and is one character, so
		 * the pair is consumed together and replaced once.
		 */
		if (code >= 0xD800 && code <= 0xDBFF && (index + 1) < length) {
			unsigned short low = (unsigned short)wide[index + 1];
			if (low >= 0xDC00 && low <= 0xDFFF) {
				index++;
				out.push_back(replacement);
				replaced = true;
				continue;
			}
		}

		int byte = Narrow_Character(code);
		if (byte < 0) {
			out.push_back(replacement);
			replaced = true;
		} else {
			out.push_back((char)byte);
		}
	}

	if (useddefaultchar != nullptr) {
		*useddefaultchar = replaced ? TRUE : FALSE;
	}

	if (multibytecount == 0) {
		return((int)out.size());
	}

	if ((std::size_t)multibytecount < out.size()) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	memcpy(multibyte, out.data(), out.size());
	return((int)out.size());
}

#endif	// __EMSCRIPTEN__
