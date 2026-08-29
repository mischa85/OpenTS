/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the Win32 calls the WebAssembly target answers out of the page's canvas:
// MultiByteToWideChar and WideCharToMultiByte, and the window geometry and pointer calls
// that have no window behind them.
//
// The harness is written against the Win32 API rather than against the substitute, and
// builds on both. On Windows it establishes what the API actually does; on WebAssembly it
// holds win32window.cpp to that same account. A check that would pass against the
// substitute but not against Windows is worth nothing here.
//
// The code page checks name Windows-1252 rather than CP_ACP, so that they mean the same
// thing whatever the machine running them is configured for. The geometry checks need a
// canvas of a known size and run only against the substitute; the harness supplies the
// canvas itself, so nothing here reads game data or opens a window.

#if defined(__EMSCRIPTEN__)
#include "browser.h"
#include "vidscale.h"
#include "win32compat.h"
#include "win32user.h"
#include "win32window.h"
#include "wincursor.h"
#else
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>


static int Failures = 0;
static int Checks = 0;

// The canvas the substitute is told the page laid out.
static int const CANVAS_WIDTH = 800;
static int const CANVAS_HEIGHT = 600;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;

	Failures++;
	printf("FAIL %s\n", name);
}


static void Check_Equal(char const * name, long actual, long expected)
{
	Checks++;
	if (actual == expected) return;

	Failures++;
	printf("FAIL %s: got %ld, expected %ld\n", name, actual, expected);
}


/*
** The 27 characters Windows-1252 puts where Latin-1 has controls, paired with the byte
** that carries each. The five positions the code page leaves undefined are not here; they
** are checked separately, because they carry the control of the same value rather than a
** printable character.
*/
static struct {
	unsigned char Byte;
	unsigned short Wide;
} const _HighRange[27] = {
	{ 0x80, 0x20AC }, { 0x82, 0x201A }, { 0x83, 0x0192 }, { 0x84, 0x201E },
	{ 0x85, 0x2026 }, { 0x86, 0x2020 }, { 0x87, 0x2021 }, { 0x88, 0x02C6 },
	{ 0x89, 0x2030 }, { 0x8A, 0x0160 }, { 0x8B, 0x2039 }, { 0x8C, 0x0152 },
	{ 0x8E, 0x017D }, { 0x91, 0x2018 }, { 0x92, 0x2019 }, { 0x93, 0x201C },
	{ 0x94, 0x201D }, { 0x95, 0x2022 }, { 0x96, 0x2013 }, { 0x97, 0x2014 },
	{ 0x98, 0x02DC }, { 0x99, 0x2122 }, { 0x9A, 0x0161 }, { 0x9B, 0x203A },
	{ 0x9C, 0x0153 }, { 0x9E, 0x017E }, { 0x9F, 0x0178 }
};

// The bytes the code page defines nothing for. Windows maps each to the control character
// of its own value, which is what keeps every byte a round trip.
static unsigned char const _Undefined[5] = { 0x81, 0x8D, 0x8F, 0x90, 0x9D };


static void Test_Widen(void)
{
	WCHAR wide[8];

	/*
	**	A terminated string is converted along with its terminator, and the count
	**	returned covers it.
	*/
	memset(wide, 0xFF, sizeof(wide));
	Check_Equal("widen counts the terminator", MultiByteToWideChar(1252, 0, "abc", -1, wide, 8), 4);
	Check("widen produces the characters", wide[0] == 'a' && wide[1] == 'b' && wide[2] == 'c' && wide[3] == 0);

	/*
	**	A given count is a length in bytes, so a null inside it is a character like any
	**	other and no terminator is added.
	*/
	memset(wide, 0xFF, sizeof(wide));
	Check_Equal("widen keeps an embedded null", MultiByteToWideChar(1252, 0, "a\0b", 3, wide, 8), 3);
	Check("widen converts around the null", wide[0] == 'a' && wide[1] == 0 && wide[2] == 'b');
	Check("widen adds no terminator of its own", wide[3] == 0xFFFF);

	/*
	**	A destination count of zero asks how much room the result needs.
	*/
	Check_Equal("widen measures the result", MultiByteToWideChar(1252, 0, "abcd", -1, NULL, 0), 5);

	/*
	**	A destination too small to hold the result fails rather than truncating.
	*/
	SetLastError(0);
	Check_Equal("widen refuses a short buffer", MultiByteToWideChar(1252, 0, "abcd", -1, wide, 3), 0);
	Check_Equal("widen says why it refused", (long)GetLastError(), (long)ERROR_INSUFFICIENT_BUFFER);

	/*
	**	Nothing to convert is an invalid request rather than an empty result.
	*/
	SetLastError(0);
	Check_Equal("widen refuses an empty count", MultiByteToWideChar(1252, 0, "abcd", 0, wide, 8), 0);
	Check_Equal("widen says why it refused the count", (long)GetLastError(), (long)ERROR_INVALID_PARAMETER);

	/*
	**	The range that separates Windows-1252 from Latin-1.
	*/
	for (int index = 0; index < 27; index++) {
		char byte = (char)_HighRange[index].Byte;
		Check_Equal("widen converts one high byte", MultiByteToWideChar(1252, 0, &byte, 1, wide, 8), 1);
		Check_Equal("widen maps the high byte", (long)(unsigned short)wide[0], (long)_HighRange[index].Wide);
	}

	for (int index = 0; index < 5; index++) {
		char byte = (char)_Undefined[index];
		Check_Equal("widen converts an undefined byte", MultiByteToWideChar(1252, 0, &byte, 1, wide, 8), 1);
		Check_Equal("widen maps it to its own control", (long)(unsigned short)wide[0], (long)_Undefined[index]);
	}
}


static void Test_Narrow(void)
{
	char narrow[8];
	WCHAR wide[8];

	memset(wide, 0, sizeof(wide));
	wide[0] = 'a';
	wide[1] = 'b';
	wide[2] = 'c';

	memset(narrow, 0x7F, sizeof(narrow));
	Check_Equal("narrow counts the terminator", WideCharToMultiByte(1252, 0, wide, -1, narrow, 8, NULL, NULL), 4);
	Check("narrow produces the bytes", memcmp(narrow, "abc\0", 4) == 0);

	/*
	**	An embedded null is a character, and the count given is a length in characters.
	*/
	memset(wide, 0, sizeof(wide));
	wide[0] = 'a';
	wide[1] = 0;
	wide[2] = 'b';

	memset(narrow, 0x7F, sizeof(narrow));
	Check_Equal("narrow keeps an embedded null", WideCharToMultiByte(1252, 0, wide, 3, narrow, 8, NULL, NULL), 3);
	Check("narrow converts around the null", memcmp(narrow, "a\0b", 3) == 0);
	Check("narrow adds no terminator of its own", narrow[3] == 0x7F);

	memset(wide, 0, sizeof(wide));
	wide[0] = 'a';
	wide[1] = 'b';
	wide[2] = 'c';
	wide[3] = 'd';

	Check_Equal("narrow measures the result", WideCharToMultiByte(1252, 0, wide, -1, NULL, 0, NULL, NULL), 5);

	SetLastError(0);
	Check_Equal("narrow refuses a short buffer", WideCharToMultiByte(1252, 0, wide, -1, narrow, 3, NULL, NULL), 0);
	Check_Equal("narrow says why it refused", (long)GetLastError(), (long)ERROR_INSUFFICIENT_BUFFER);

	SetLastError(0);
	Check_Equal("narrow refuses an empty count", WideCharToMultiByte(1252, 0, wide, 0, narrow, 8, NULL, NULL), 0);
	Check_Equal("narrow says why it refused the count", (long)GetLastError(), (long)ERROR_INVALID_PARAMETER);

	/*
	**	The high range comes back to the byte it was made from, and so do the positions
	**	the code page defines nothing for.
	*/
	for (int index = 0; index < 27; index++) {
		wide[0] = (WCHAR)_HighRange[index].Wide;
		Check_Equal("narrow converts one high character", WideCharToMultiByte(1252, 0, wide, 1, narrow, 8, NULL, NULL), 1);
		Check_Equal("narrow maps the high character", (long)(unsigned char)narrow[0], (long)_HighRange[index].Byte);
	}

	for (int index = 0; index < 5; index++) {
		wide[0] = (WCHAR)_Undefined[index];
		Check_Equal("narrow converts an undefined control", WideCharToMultiByte(1252, 0, wide, 1, narrow, 8, NULL, NULL), 1);
		Check_Equal("narrow maps the control to its byte", (long)(unsigned char)narrow[0], (long)_Undefined[index]);
	}

	/*
	**	A character the code page has no byte for is replaced, and the caller is told.
	*/
	BOOL used = FALSE;
	wide[0] = 0x4E00;
	Check_Equal("narrow replaces what it cannot write", WideCharToMultiByte(1252, 0, wide, 1, narrow, 8, NULL, &used), 1);
	Check("narrow replaces with a question mark", narrow[0] == '?');
	Check("narrow reports the replacement", used == TRUE);

	used = FALSE;
	Check_Equal("narrow uses the caller's replacement", WideCharToMultiByte(1252, 0, wide, 1, narrow, 8, "#", &used), 1);
	Check("narrow writes the caller's replacement", narrow[0] == '#');
	Check("narrow reports the caller's replacement", used == TRUE);

	used = TRUE;
	wide[0] = 'a';
	Check_Equal("narrow converts a plain character", WideCharToMultiByte(1252, 0, wide, 1, narrow, 8, NULL, &used), 1);
	Check("narrow reports no replacement", used == FALSE);
}


/// <summary>
/// Converts every byte out and back again.
/// </summary>
static void Test_Round_Trip(void)
{
	char before[256];
	WCHAR wide[256];
	char after[256];

	for (int index = 0; index < 256; index++) {
		before[index] = (char)index;
	}

	Check_Equal("round trip widens every byte", MultiByteToWideChar(1252, 0, before, 256, wide, 256), 256);
	Check_Equal("round trip narrows them back", WideCharToMultiByte(1252, 0, wide, 256, after, 256, NULL, NULL), 256);
	Check("round trip returns what it started with", memcmp(before, after, 256) == 0);
}


#if defined(__EMSCRIPTEN__)


static void Test_Geometry(void)
{
	RECT rect;

	memset(&rect, 0, sizeof(rect));
	Check("the client rectangle is answered", GetClientRect(NULL, &rect) != FALSE);
	Check("the client rectangle is the canvas",
		rect.left == 0 && rect.top == 0 && rect.right == CANVAS_WIDTH && rect.bottom == CANVAS_HEIGHT);

	memset(&rect, 0, sizeof(rect));
	Check("the window rectangle is answered", GetWindowRect(NULL, &rect) != FALSE);
	Check("the window rectangle is the client rectangle",
		rect.left == 0 && rect.top == 0 && rect.right == CANVAS_WIDTH && rect.bottom == CANVAS_HEIGHT);

	/*
	**	Nothing surrounds the canvas, so a client position is a screen position and a
	**	window is no larger than the client area it holds.
	*/
	POINT point;
	point.x = 17;
	point.y = 23;
	Check("a client position converts", ClientToScreen(NULL, &point) != FALSE);
	Check("a client position is a screen position", point.x == 17 && point.y == 23);
	Check("a screen position converts", ScreenToClient(NULL, &point) != FALSE);
	Check("a screen position is a client position", point.x == 17 && point.y == 23);

	SetRect(&rect, 0, 0, 640, 400);
	Check("a window rectangle adjusts", AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE) != FALSE);
	Check("there is no frame to add",
		rect.left == 0 && rect.top == 0 && rect.right == 640 && rect.bottom == 400);

	Check_Equal("the screen is the canvas, across", GetSystemMetrics(SM_CXSCREEN), CANVAS_WIDTH);
	Check_Equal("the screen is the canvas, down", GetSystemMetrics(SM_CYSCREEN), CANVAS_HEIGHT);
	Check_Equal("there is no caption bar", GetSystemMetrics(SM_CYCAPTION), 0);
	Check_Equal("the buttons are not swapped for a second time", GetSystemMetrics(SM_SWAPBUTTON), 0);
}


static void Test_Pointer(void)
{
	/*
	**	Capture is bookkeeping here, because the canvas receives the events either way,
	**	but the engine reads it back and it has to answer what it was told.
	*/
	Check("nothing holds the capture to begin with", GetCapture() == NULL);
	SetCapture((HWND)1);
	Check("the capture is remembered", GetCapture() == (HWND)1);
	Check("the capture is released", ReleaseCapture() != FALSE);
	Check("nothing holds the capture afterwards", GetCapture() == NULL);

	/*
	**	The canvas confines the pointer as far as the engine can observe it, and nothing
	**	smaller than the canvas does.
	*/
	Check("an unconfined pointer is granted", ClipCursor(NULL) != FALSE);

	RECT rect;
	SetRect(&rect, 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);
	Check("the canvas confines the pointer", ClipCursor(&rect) != FALSE);

	SetRect(&rect, 10, 10, 20, 20);
	Check("a smaller rectangle does not", ClipCursor(&rect) == FALSE);

	/*
	**	The page reports the pointer in the frame's own pixels; a screen position is the
	**	canvas's, and the frame fills it here, so the two coincide.
	*/
	POINT point;
	memset(&point, 0, sizeof(point));
	Check("the pointer position is answered", GetCursorPos(&point) != FALSE);
	Check("the pointer is where the page put it", point.x == 40 && point.y == 50);
}


/*
** ---------------------------------------------------------------------------------------
** What the substitute reaches for and the engine would otherwise supply. The canvas is
** the harness's own, so the geometry checks have a size to hold the substitute to.
** ---------------------------------------------------------------------------------------
*/


void Win32_Unsupported_Reached(char const * description)
{
	printf("unsupported: %s\n", description);
}


/// <summary>
/// Stands in for the game's answer to what the pointer should be.
/// </summary>
/// <returns>bool; Always false. There is no game cursor here, which is the answer a
/// released mouse gives.</returns>
bool Win_Cursor_Handle_Set_Cursor(void)
{
	return(false);
}


/*
** The harness registers no windows, so every handle is one the window manager has never
** heard of and the canvas answers for all of them.
*/
bool Win32_User_Window_Rect(HWND, RECT *)
{
	return(false);
}


bool Win32_User_Client_Rect(HWND, RECT *)
{
	return(false);
}


bool Win32_User_Client_Origin(HWND, POINT *)
{
	return(false);
}


static DWORD _LastError = 0;


DWORD GetLastError(void)
{
	return(_LastError);
}


void SetLastError(DWORD error)
{
	_LastError = error;
}


char const * Browser_Canvas_Selector(void)
{
	return("#canvas");
}


int Browser_Canvas_Width(void)
{
	return(CANVAS_WIDTH);
}


int Browser_Canvas_Height(void)
{
	return(CANVAS_HEIGHT);
}


int Browser_Mouse_X(void)
{
	return(40);
}


int Browser_Mouse_Y(void)
{
	return(50);
}


void Game_Point_To_Window(POINT &)
{
}

#endif	// __EMSCRIPTEN__


int main(void)
{
	Test_Widen();
	Test_Narrow();
	Test_Round_Trip();

#if defined(__EMSCRIPTEN__)
	Test_Geometry();
	Test_Pointer();
#endif

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
