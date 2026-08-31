/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The synthetic USER32. win32user.h states what this stands in for; what follows is why it
// is shaped the way it is.
//
// The engine's front end is written as Windows: ownrdraw.cpp registers a window class per
// control and drives each one from a window procedure. What it does not use is GDI -- the
// controls paint onto the engine's own Surface objects -- so what it actually needs from
// the operating system is windows, a hierarchy, and message delivery. Those are cheap to
// provide in process, and providing them is far cheaper than rewriting the front end.
//
// Two decisions are worth stating.
//
// Coordinates. There is no desktop and no window frame, so a window's client area is its
// window rectangle and a top level window sits at its own screen position. The engine
// places dialog controls at frame coordinates inside a main window measured in canvas
// pixels, which is the mismatch msgroute.cpp exists to correct; this file therefore
// hit-tests the way Windows would, taking child rectangles literally, and leaves the
// correction where the engine already puts it.
//
// Input. The mouse is tapped at browser.cpp's drain through Browser_Set_Event_Hook, so each
// press and release becomes a message exactly once however close together they arrive, and
// the window it is delivered to is what puts it in the keyboard buffer. The keyboard is not
// tapped: browser.cpp posts keys to that buffer directly, which is how the main window
// reads them, so a key message is only queued when the focus is a window with a parent -- a
// dialog or one of its controls -- and would otherwise be doubled.

#include "always.h"

#include "win32user.h"

#if defined(__EMSCRIPTEN__)

#include "browser.h"
#include "data.h"
#include "dbgprint.h"
#include "keyboard.h"
#include "vidscale.h"
#include "win32ctrl.h"

#include <emscripten/emscripten.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>


// How many posted messages may be waiting at once. A queue this deep is already a queue
// nobody is draining, so the overflow is reported rather than grown around.
static unsigned int const MESSAGE_QUEUE_LIMIT = 512;

// Every window carries at least this many bytes of window extra, whatever its class asked
// for, because the dialog slots DWL_MSGRESULT, DWL_DLGPROC and DWL_USER are addressed as
// window extra and the engine writes DWL_USER on windows whose class declared none.
static int const MINIMUM_WINDOW_EXTRA = 32;

// win32compat.h names neither of these. WM_HOTKEY is what a registered hot key arrives as,
// and RDW_UPDATENOW is the RedrawWindow flag that asks for the paint now rather than later.
static UINT const WM_HOTKEY_MESSAGE = 0x0312;
static UINT const RDW_UPDATENOW_FLAG = 0x0100;


struct UserClass
{
	std::string Name;
	UINT Style;
	WNDPROC Procedure;
	int ClassExtra;
	int WindowExtra;
	HINSTANCE Instance;
	HICON Icon;
	HCURSOR Cursor;
	HBRUSH Background;
	ATOM Atom;
};


struct UserWindow
{
	UserClass * Class;
	WNDPROC Procedure;
	HINSTANCE Instance;
	int ID;
	DWORD Style;
	DWORD ExStyle;
	RECT Rect;
	std::string Text;
	LONG_PTR UserData;
	std::vector<LONG_PTR> Extra;
	bool Visible;
	bool Enabled;
	bool NeedsPaint;
	bool Painting;
	RECT UpdateRect;
	UserWindow * Parent;

	/*
	** Siblings front to back. Windows keeps z-order in the sibling chain and so does this,
	** so a hit test walks Children in order and stops at the first window that owns the
	** point.
	*/
	std::vector<UserWindow *> Children;
};


struct UserHotKey
{
	UserWindow * Window;
	int ID;
	UINT Modifiers;
	UINT Key;
};


/*
** A window timer. Windows synthesizes WM_TIMER rather than queueing it, so that a pump too
** slow for the period is given one tick rather than a backlog of them; the due time here is
** pushed forward as the tick is taken, which is what produces that.
*/
struct UserTimer
{
	UserWindow * Window;
	UINT_PTR ID;
	UINT Elapse;
	DWORD Due;
};


static std::vector<UserClass *> _Classes;
static std::vector<UserWindow *> _Windows;
static std::vector<UserWindow *> _TopLevel;
static std::vector<UserHotKey> _HotKeys;
static std::vector<UserTimer> _Timers;
static std::deque<MSG> _Queue;

static UserWindow * _Focus = nullptr;
static UserWindow * _Active = nullptr;
static UserWindow * _Foreground = nullptr;

static ATOM _NextAtom = 0xC000;

static bool _QueueReported = false;

/*
** How many times in a row one window may be handed the same generated paint before it is
** taken to be a window that never validates itself.
*/
static unsigned int const PAINT_REPEAT_LIMIT = 64;

static UserWindow * _PaintWindow = nullptr;
static unsigned int _PaintRepeats = 0;

/*
** What the page last reported, so that a change in it can be turned into a message. The
** position is in window pixels, which is what a mouse message carries.
*/
static POINT _LastMouse = { -1, -1 };
static unsigned char _LastKeyDown[256];
static bool _InputStarted = false;

/*
** Defined with the rest of the page's input, alongside the rule that decides which windows
** are fed keys from there at all. Focus is what reaches it, and focus is settled long
** before that section.
*/
static void Update_Text_Input(void);


/*
** ---------------------------------------------------------------------------------------
** Handles and lookups.
** ---------------------------------------------------------------------------------------
*/


static HWND Handle_Of(UserWindow * window)
{
	return((HWND)window);
}


/// <summary>
/// Turns a handle back into a window, rejecting one that names nothing.
/// </summary>
/// <remarks>
/// A destroyed window is freed, so a handle held past its destruction is checked against
/// the registry rather than dereferenced. Windows lets an address come round again too;
/// what this rules out is the handle of a window that is simply gone.
/// </remarks>
static UserWindow * Window_Of(HWND handle)
{
	if (handle == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < _Windows.size(); index++) {
		if (_Windows[index] == (UserWindow *)handle) {
			return(_Windows[index]);
		}
	}

	return(nullptr);
}


static bool Names_Match(char const * left, char const * right)
{
	if (left == nullptr || right == nullptr) {
		return(false);
	}

	while (*left != '\0' && *right != '\0') {
		char a = *left;
		char b = *right;
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return(false);
		left++;
		right++;
	}

	return(*left == *right);
}


/// <summary>
/// Finds a registered class by the name or the atom a caller named it with.
/// </summary>
static UserClass * Class_Of(LPCSTR name)
{
	if (name == nullptr) {
		return(nullptr);
	}

	/*
	** A class name is either a string or an atom packed into the pointer, which is what
	** MAKEINTATOM produces and what RegisterClass hands back. Windows tells the two apart
	** by value alone, because no string of a process lives in the first 64K of its address
	** space. A wasm module's do: its literals sit at the bottom of linear memory, well
	** inside the range an atom occupies. So a small value is tried as an atom first and
	** then read as a string, rather than being taken for an atom and given up on.
	*/
	if ((ULONG_PTR)name <= 0xFFFF) {
		ATOM atom = (ATOM)(ULONG_PTR)name;
		for (unsigned int index = 0; index < _Classes.size(); index++) {
			if (_Classes[index]->Atom == atom) {
				return(_Classes[index]);
			}
		}
	}

	for (unsigned int index = 0; index < _Classes.size(); index++) {
		if (Names_Match(_Classes[index]->Name.c_str(), name)) {
			return(_Classes[index]);
		}
	}

	return(nullptr);
}


/*
** ---------------------------------------------------------------------------------------
** Geometry.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Fetches where a window's client area starts, in screen coordinates.
/// </summary>
static POINT Screen_Origin(UserWindow const * window)
{
	POINT origin;
	origin.x = 0;
	origin.y = 0;

	for (UserWindow const * walk = window; walk != nullptr; walk = walk->Parent) {
		origin.x += walk->Rect.left;
		origin.y += walk->Rect.top;
	}

	return(origin);
}


bool Win32_User_Window_Rect(HWND window, RECT * rect)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || rect == nullptr) {
		return(false);
	}

	POINT origin = Screen_Origin(entry);

	rect->left = origin.x;
	rect->top = origin.y;
	rect->right = origin.x + (entry->Rect.right - entry->Rect.left);
	rect->bottom = origin.y + (entry->Rect.bottom - entry->Rect.top);
	return(true);
}


bool Win32_User_Client_Rect(HWND window, RECT * rect)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || rect == nullptr) {
		return(false);
	}

	rect->left = 0;
	rect->top = 0;
	rect->right = entry->Rect.right - entry->Rect.left;
	rect->bottom = entry->Rect.bottom - entry->Rect.top;
	return(true);
}


bool Win32_User_Client_Origin(HWND window, POINT * origin)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || origin == nullptr) {
		return(false);
	}

	*origin = Screen_Origin(entry);
	return(true);
}


/// <summary>
/// Finds the deepest visible, enabled descendant that owns a screen position.
/// </summary>
static UserWindow * Descendant_From_Screen_Point(UserWindow * parent, POINT point)
{
	POINT origin = Screen_Origin(parent);

	for (unsigned int index = 0; index < parent->Children.size(); index++) {
		UserWindow * child = parent->Children[index];

		if (!child->Visible || !child->Enabled) {
			continue;
		}

		LONG left = origin.x + child->Rect.left;
		LONG top = origin.y + child->Rect.top;
		LONG right = left + (child->Rect.right - child->Rect.left);
		LONG bottom = top + (child->Rect.bottom - child->Rect.top);

		if (point.x < left || point.x >= right || point.y < top || point.y >= bottom) {
			continue;
		}

		UserWindow * deeper = Descendant_From_Screen_Point(child, point);
		return(deeper != nullptr ? deeper : child);
	}

	return(nullptr);
}


static UserWindow * Window_From_Screen_Point(POINT point)
{
	for (unsigned int index = 0; index < _TopLevel.size(); index++) {
		UserWindow * window = _TopLevel[index];

		if (!window->Visible) {
			continue;
		}

		if (point.x < window->Rect.left || point.x >= window->Rect.right) continue;
		if (point.y < window->Rect.top || point.y >= window->Rect.bottom) continue;

		UserWindow * child = Descendant_From_Screen_Point(window, point);
		return(child != nullptr ? child : window);
	}

	return(nullptr);
}


/*
** ---------------------------------------------------------------------------------------
** Classes.
** ---------------------------------------------------------------------------------------
*/


static ATOM Register_Class(UINT style, WNDPROC procedure, int classextra, int windowextra,
	HINSTANCE instance, HICON icon, HCURSOR cursor, HBRUSH background, LPCSTR name)
{
	if (name == nullptr || procedure == nullptr) {
		return(0);
	}

	if (Class_Of(name) != nullptr) {
		/*
		** Windows fails a second registration of a live class name rather than replacing
		** it, and the engine registers its control classes once behind a guard.
		*/
		return(0);
	}

	UserClass * entry = new UserClass;
	entry->Name = name;
	entry->Style = style;
	entry->Procedure = procedure;
	entry->ClassExtra = classextra;
	entry->WindowExtra = windowextra;
	entry->Instance = instance;
	entry->Icon = icon;
	entry->Cursor = cursor;
	entry->Background = background;
	entry->Atom = _NextAtom++;

	_Classes.push_back(entry);
	return(entry->Atom);
}


ATOM RegisterClassA(WNDCLASSA const * windowclass)
{
	if (windowclass == nullptr) {
		return(0);
	}

	return(Register_Class(windowclass->style, windowclass->lpfnWndProc, windowclass->cbClsExtra,
		windowclass->cbWndExtra, windowclass->hInstance, windowclass->hIcon, windowclass->hCursor,
		windowclass->hbrBackground, windowclass->lpszClassName));
}


ATOM RegisterClassExA(WNDCLASSEXA const * windowclass)
{
	if (windowclass == nullptr) {
		return(0);
	}

	return(Register_Class(windowclass->style, windowclass->lpfnWndProc, windowclass->cbClsExtra,
		windowclass->cbWndExtra, windowclass->hInstance, windowclass->hIcon, windowclass->hCursor,
		windowclass->hbrBackground, windowclass->lpszClassName));
}


BOOL UnregisterClassA(LPCSTR classname, HINSTANCE)
{
	UserClass * entry = Class_Of(classname);
	if (entry == nullptr) {
		return(FALSE);
	}

	/*
	** Windows refuses to unregister a class that still has windows, because their
	** procedures live in it.
	*/
	for (unsigned int index = 0; index < _Windows.size(); index++) {
		if (_Windows[index]->Class == entry) {
			return(FALSE);
		}
	}

	for (unsigned int index = 0; index < _Classes.size(); index++) {
		if (_Classes[index] == entry) {
			_Classes.erase(_Classes.begin() + index);
			break;
		}
	}

	delete entry;
	return(TRUE);
}


int GetClassNameA(HWND window, LPSTR classname, int count)
{
	if (classname == nullptr || count <= 0) {
		return(0);
	}

	classname[0] = '\0';

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || entry->Class == nullptr) {
		return(0);
	}

	int length = (int)entry->Class->Name.size();
	if (length > count - 1) {
		length = count - 1;
	}

	memcpy(classname, entry->Class->Name.c_str(), (size_t)length);
	classname[length] = '\0';
	return(length);
}


/*
** ---------------------------------------------------------------------------------------
** Dispatch.
** ---------------------------------------------------------------------------------------
*/


LRESULT CallWindowProcA(WNDPROC previous, HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (previous == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	return(previous(window, message, wparam, lparam));
}


LRESULT SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	WNDPROC procedure = entry->Procedure;
	if (procedure == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	return(procedure(window, message, wparam, lparam));
}


LRESULT SendDlgItemMessageA(HWND dialog, int id, UINT message, WPARAM wparam, LPARAM lparam)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(0);
	}

	return(SendMessageA(item, message, wparam, lparam));
}


static bool Queue_Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (_Queue.size() >= MESSAGE_QUEUE_LIMIT) {
		if (!_QueueReported) {
			_QueueReported = true;
			DebugString("Win32 user: the message queue is full; posted messages are being dropped.\n");
		}
		return(false);
	}

	MSG entry;
	entry.hwnd = window;
	entry.message = message;
	entry.wParam = wparam;
	entry.lParam = lparam;
	entry.time = GetTickCount();
	entry.pt.x = _LastMouse.x;
	entry.pt.y = _LastMouse.y;

	_Queue.push_back(entry);
	return(true);
}


BOOL PostMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (window != nullptr && Window_Of(window) == nullptr) {
		return(FALSE);
	}

	return(Queue_Message(window, message, wparam, lparam) ? TRUE : FALSE);
}


void PostQuitMessage(int exitcode)
{
	Queue_Message(nullptr, WM_QUIT, (WPARAM)exitcode, 0);
}


static bool Message_Matches(MSG const & entry, HWND window, UINT filtermin, UINT filtermax)
{
	if (window != nullptr && entry.hwnd != window) {
		return(false);
	}

	if (filtermin == 0 && filtermax == 0) {
		return(true);
	}

	return(entry.message >= filtermin && entry.message <= filtermax);
}


/// <summary>
/// Finds the window with the best claim to a paint, front to back and parents first.
/// </summary>
static UserWindow * Window_Awaiting_Paint(UserWindow * parent)
{
	std::vector<UserWindow *> const & windows = (parent != nullptr) ? parent->Children : _TopLevel;

	for (unsigned int index = 0; index < windows.size(); index++) {
		UserWindow * window = windows[index];

		if (!window->Visible) {
			continue;
		}

		if (window->NeedsPaint) {
			return(window);
		}

		UserWindow * deeper = Window_Awaiting_Paint(window);
		if (deeper != nullptr) {
			return(deeper);
		}
	}

	return(nullptr);
}


/*
** ---------------------------------------------------------------------------------------
** Window timers.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Sets a window timer, or resets one the window already has under that identifier.
/// </summary>
/// <remarks>
/// Only the form that posts to a window is here. The other form -- an identifier Windows
/// allocates, whose ticks go to a TIMERPROC rather than to a window -- has no caller in the
/// engine, so it reports itself rather than being half built.
/// </remarks>
UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC callback)
{
	if (callback != nullptr) {
		return(WIN32_UNSUPPORTED("SetTimer: a timer whose ticks go to a callback", 0));
	}

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(WIN32_UNSUPPORTED("SetTimer: a timer belonging to no window", 0));
	}

	DWORD due = GetTickCount() + elapse;

	for (unsigned int index = 0; index < _Timers.size(); index++) {
		if (_Timers[index].Window == entry && _Timers[index].ID == id) {
			_Timers[index].Elapse = elapse;
			_Timers[index].Due = due;
			return(id);
		}
	}

	UserTimer timer;
	timer.Window = entry;
	timer.ID = id;
	timer.Elapse = elapse;
	timer.Due = due;
	_Timers.push_back(timer);
	return(id);
}


BOOL KillTimer(HWND window, UINT_PTR id)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _Timers.size(); index++) {
		if (_Timers[index].Window == entry && _Timers[index].ID == id) {
			_Timers.erase(_Timers.begin() + index);
			return(TRUE);
		}
	}

	return(FALSE);
}


/// <summary>
/// Finds the timer with the best claim to a tick.
/// </summary>
/// <returns>Returns with the position of the timer, or -1 when none is due.</returns>
static int Timer_Awaiting_Tick(void)
{
	DWORD now = GetTickCount();

	for (unsigned int index = 0; index < _Timers.size(); index++) {
		if ((LONG)(now - _Timers[index].Due) >= 0) {
			return((int)index);
		}
	}

	return(-1);
}


BOOL PeekMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax, UINT remove)
{
	if (message == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _Queue.size(); index++) {
		if (!Message_Matches(_Queue[index], window, filtermin, filtermax)) {
			continue;
		}

		*message = _Queue[index];

		if ((remove & PM_REMOVE) != 0) {
			_Queue.erase(_Queue.begin() + index);
		}

		return(TRUE);
	}

	/*
	** WM_PAINT is generated rather than posted, so it is reported once the queue is empty
	** and goes on being reported until the window validates itself. That is what makes an
	** InvalidateRect with no UpdateWindow behind it reach the window at all, which is how
	** the front end asks for most of its repainting.
	*/
	UserWindow * painting = Window_Awaiting_Paint(nullptr);

	if (painting != _PaintWindow) {
		_PaintWindow = painting;
		_PaintRepeats = 0;
	}

	if (painting != nullptr) {
		_PaintRepeats++;

		/*
		** A window that invalidates itself and never validates spins the pump. Windows
		** spins with it; a page stops answering instead, so the update is dropped here and
		** the window is named rather than left to freeze the tab.
		*/
		if (_PaintRepeats > PAINT_REPEAT_LIMIT) {
			DebugString("Win32 user: a %s window never validates its paint; the update is being dropped.\n",
				(painting->Class != nullptr) ? painting->Class->Name.c_str() : "?");
			ValidateRect(Handle_Of(painting), nullptr);
			painting = nullptr;
			_PaintWindow = nullptr;
			_PaintRepeats = 0;
		}
	}

	if (painting != nullptr) {
		MSG paint;
		paint.hwnd = Handle_Of(painting);
		paint.message = WM_PAINT;
		paint.wParam = 0;
		paint.lParam = 0;
		paint.time = 0;
		paint.pt.x = _LastMouse.x;
		paint.pt.y = _LastMouse.y;

		if (Message_Matches(paint, window, filtermin, filtermax)) {
			*message = paint;
			return(TRUE);
		}
	}

	/*
	** WM_TIMER is generated too, and after WM_PAINT because Windows ranks it last of all.
	** The timer is rearmed only where the tick is taken, so a caller that peeks without
	** removing sees the same tick until somebody consumes it.
	*/
	int due = Timer_Awaiting_Tick();

	if (due >= 0) {
		MSG tick;
		tick.hwnd = Handle_Of(_Timers[(unsigned int)due].Window);
		tick.message = WM_TIMER;
		tick.wParam = (WPARAM)_Timers[(unsigned int)due].ID;
		tick.lParam = 0;
		tick.time = GetTickCount();
		tick.pt.x = _LastMouse.x;
		tick.pt.y = _LastMouse.y;

		if (Message_Matches(tick, window, filtermin, filtermax)) {
			if ((remove & PM_REMOVE) != 0) {
				_Timers[(unsigned int)due].Due = GetTickCount() + _Timers[(unsigned int)due].Elapse;
			}
			*message = tick;
			return(TRUE);
		}
	}

	/*
	 * A pump that finds nothing to do is a pump that is about to ask again, and on this
	 * target a loop that keeps asking is a page that stops answering. Windows lets the
	 * thread go here by having nothing to run; the equivalent is to hand it back, paced so
	 * that a peek from inside a frame does not cost a frame.
	 */
	Browser_Yield_If_Due();
	return(FALSE);
}


BOOL GetMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax)
{
	if (message == nullptr) {
		return(FALSE);
	}

	while (true) {
		if (PeekMessageA(message, window, filtermin, filtermax, PM_REMOVE)) {
			return(message->message == WM_QUIT ? FALSE : TRUE);
		}

		if (!Browser_Yield_Is_Available()) {
			/*
			 * Nothing carries the wait, so waiting here would never return. Reporting the
			 * end of the queue is the failure a caller can act on.
			 */
			return(WIN32_UNSUPPORTED("GetMessage: waiting for a message without the yield scaffold", FALSE));
		}

		Browser_Yield();
		Win32_User_Service();
	}
}


BOOL TranslateMessage(MSG const * message)
{
	if (message == nullptr) {
		return(FALSE);
	}

	if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN) {
		return(FALSE);
	}

	unsigned short key = (unsigned short)(message->wParam & 0xFF);
	char character = Browser_Key_To_ASCII((unsigned short)(key | (Browser_Key_Modifiers() & WWKEY_SHIFT_BIT)));

	if (character == '\0') {
		return(FALSE);
	}

	UINT translated = (message->message == WM_SYSKEYDOWN) ? WM_SYSCHAR : WM_CHAR;
	Queue_Message(message->hwnd, translated, (WPARAM)(unsigned char)character, message->lParam);
	return(TRUE);
}


LRESULT DispatchMessageA(MSG const * message)
{
	if (message == nullptr || message->hwnd == nullptr) {
		return(0);
	}

	return(SendMessageA(message->hwnd, message->message, message->wParam, message->lParam));
}


LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	UserWindow * entry = Window_Of(window);

	switch (message) {
		case WM_SETTEXT:
			if (entry != nullptr) {
				entry->Text = (lparam != 0) ? (char const *)lparam : "";
				return(TRUE);
			}
			return(FALSE);

		case WM_GETTEXTLENGTH:
			return(entry != nullptr ? (LRESULT)entry->Text.size() : 0);

		case WM_GETTEXT: {
			char * buffer = (char *)lparam;
			int count = (int)wparam;
			if (buffer == nullptr || count <= 0) {
				return(0);
			}
			buffer[0] = '\0';
			if (entry == nullptr) {
				return(0);
			}
			int length = (int)entry->Text.size();
			if (length > count - 1) {
				length = count - 1;
			}
			memcpy(buffer, entry->Text.c_str(), (size_t)length);
			buffer[length] = '\0';
			return(length);
		}

		case WM_NCCREATE:
			/*
			** Anything but zero lets the creation stand, and a class that does not handle
			** this message must not be the one that stops its own window existing.
			*/
			return(TRUE);

		case WM_CLOSE:
			DestroyWindow(window);
			return(0);

		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(0);
	}
}


/*
** ---------------------------------------------------------------------------------------
** Windows.
** ---------------------------------------------------------------------------------------
*/


HWND CreateWindowExA(DWORD exstyle, LPCSTR classname, LPCSTR windowname, DWORD style,
	int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
	UserClass * windowclass = Class_Of(classname);
	if (windowclass == nullptr) {
		/*
		 * Windows fails here too, and the failure is the honest answer: nothing in this
		 * build registers the stock control classes, so a window asking for one is a
		 * window whose behavior does not exist yet.
		 */
		return(nullptr);
	}

	UserWindow * parentwindow = Window_Of(parent);
	if (parent != nullptr && parentwindow == nullptr) {
		return(nullptr);
	}

	if (x == CW_USEDEFAULT) x = 0;
	if (y == CW_USEDEFAULT) y = 0;
	if (width == CW_USEDEFAULT) width = 0;
	if (height == CW_USEDEFAULT) height = 0;

	UserWindow * window = new UserWindow;
	window->Class = windowclass;
	window->Procedure = windowclass->Procedure;
	window->Instance = (instance != nullptr) ? instance : windowclass->Instance;
	window->ID = ((style & WS_CHILD) != 0) ? (int)(ULONG_PTR)menu : 0;
	window->Style = style;
	window->ExStyle = exstyle;
	window->Rect.left = x;
	window->Rect.top = y;
	window->Rect.right = x + width;
	window->Rect.bottom = y + height;
	window->Text = (windowname != nullptr) ? windowname : "";
	window->UserData = 0;
	window->Visible = ((style & WS_VISIBLE) != 0);
	window->Enabled = ((style & WS_DISABLED) == 0);
	window->NeedsPaint = window->Visible;
	window->Painting = false;
	window->UpdateRect.left = 0;
	window->UpdateRect.top = 0;
	window->UpdateRect.right = width;
	window->UpdateRect.bottom = height;
	window->Parent = ((style & WS_CHILD) != 0) ? parentwindow : nullptr;

	int extrabytes = windowclass->WindowExtra;
	if (extrabytes < MINIMUM_WINDOW_EXTRA) {
		extrabytes = MINIMUM_WINDOW_EXTRA;
	}
	int const slot = (int)sizeof(LONG_PTR);
	window->Extra.assign((size_t)((extrabytes + slot - 1) / slot), 0);

	_Windows.push_back(window);

	if (window->Parent != nullptr) {
		window->Parent->Children.insert(window->Parent->Children.begin(), window);
	} else {
		_TopLevel.insert(_TopLevel.begin(), window);
	}

	HWND handle = Handle_Of(window);

	/*
	** The creation parameter travels to the procedure inside this, which is the only way a
	** class gets its state before its first message. The structure describes the call as it
	** was made, so the class name is passed on as the caller wrote it -- string or atom --
	** and the sizes are the ones asked for rather than the ones stored.
	*/
	CREATESTRUCTA create;
	create.lpCreateParams = param;
	create.hInstance = window->Instance;
	create.hMenu = menu;
	create.hwndParent = parent;
	create.cy = height;
	create.cx = width;
	create.y = y;
	create.x = x;
	create.style = (LONG)style;
	create.lpszName = windowname;
	create.lpszClass = classname;
	create.dwExStyle = exstyle;

	/*
	** Either message may refuse the window, and Windows spells the refusal differently in
	** each: FALSE from WM_NCCREATE, -1 from WM_CREATE.
	*/
	if (SendMessageA(handle, WM_NCCREATE, 0, (LPARAM)&create) == 0) {
		DestroyWindow(handle);
		return(nullptr);
	}

	if (SendMessageA(handle, WM_CREATE, 0, (LPARAM)&create) == (LRESULT)-1) {
		DestroyWindow(handle);
		return(nullptr);
	}

	if (window->Visible) {
		SendMessageA(handle, WM_SHOWWINDOW, TRUE, 0);
	}

	return(handle);
}


/// <summary>
/// Marks the area a window occupies in its parent as needing repainting.
/// </summary>
/// <remarks>
/// Nothing composites here: what a window drew is on the engine's surfaces, and the parent
/// repainting is what puts the background back. So a window that goes away, moves, or is
/// hidden has to tell its parent, exactly as Windows tells it.
/// </remarks>
static void Invalidate_Behind(UserWindow * window)
{
	if (window->Parent == nullptr || !window->Visible) {
		return;
	}

	InvalidateRect(Handle_Of(window->Parent), &window->Rect, TRUE);
}


static void Unlink_Window(UserWindow * window)
{
	std::vector<UserWindow *> & siblings = (window->Parent != nullptr) ? window->Parent->Children : _TopLevel;

	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == window) {
			siblings.erase(siblings.begin() + index);
			break;
		}
	}

	for (unsigned int index = 0; index < _Windows.size(); index++) {
		if (_Windows[index] == window) {
			_Windows.erase(_Windows.begin() + index);
			break;
		}
	}
}


static void Destroy_Window(UserWindow * window)
{
	HWND handle = Handle_Of(window);

	Invalidate_Behind(window);

	SendMessageA(handle, WM_DESTROY, 0, 0);

	while (!window->Children.empty()) {
		Destroy_Window(window->Children[0]);
	}

	SendMessageA(handle, WM_NCDESTROY, 0, 0);

	if (_Focus == window) {
		_Focus = nullptr;
		Update_Text_Input();
	}
	if (_Active == window) _Active = nullptr;
	if (_Foreground == window) _Foreground = nullptr;
	if (_PaintWindow == window) _PaintWindow = nullptr;

	if (GetCapture() == handle) {
		ReleaseCapture();
	}

	for (unsigned int index = 0; index < _HotKeys.size(); ) {
		if (_HotKeys[index].Window == window) {
			_HotKeys.erase(_HotKeys.begin() + index);
		} else {
			index++;
		}
	}

	for (unsigned int index = 0; index < _Timers.size(); ) {
		if (_Timers[index].Window == window) {
			_Timers.erase(_Timers.begin() + index);
		} else {
			index++;
		}
	}

	for (unsigned int index = 0; index < _Queue.size(); ) {
		if (_Queue[index].hwnd == handle) {
			_Queue.erase(_Queue.begin() + index);
		} else {
			index++;
		}
	}

	Unlink_Window(window);
	delete window;
}


BOOL DestroyWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	Destroy_Window(entry);
	return(TRUE);
}


BOOL ShowWindow(HWND window, int command)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	bool wasvisible = entry->Visible;
	bool visible = (command != SW_HIDE);

	if (visible != wasvisible) {
		if (visible) {
			entry->Visible = true;
			entry->Style |= WS_VISIBLE;
			InvalidateRect(window, nullptr, TRUE);
		} else {
			Invalidate_Behind(entry);
			entry->Visible = false;
			entry->Style &= ~(DWORD)WS_VISIBLE;
		}
		SendMessageA(window, WM_SHOWWINDOW, visible ? TRUE : FALSE, 0);
	}

	return(wasvisible ? TRUE : FALSE);
}


BOOL IsWindow(HWND window)
{
	return(Window_Of(window) != nullptr ? TRUE : FALSE);
}


BOOL IsWindowVisible(HWND window)
{
	UserWindow * entry = Window_Of(window);

	for (UserWindow * walk = entry; walk != nullptr; walk = walk->Parent) {
		if (!walk->Visible) {
			return(FALSE);
		}
	}

	return(entry != nullptr ? TRUE : FALSE);
}


BOOL IsWindowEnabled(HWND window)
{
	UserWindow * entry = Window_Of(window);
	return((entry != nullptr && entry->Enabled) ? TRUE : FALSE);
}


BOOL IsIconic(HWND window)
{
	UserWindow * entry = Window_Of(window);
	return((entry != nullptr && (entry->Style & WS_MINIMIZE) != 0) ? TRUE : FALSE);
}


BOOL EnableWindow(HWND window, BOOL enable)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	bool wasdisabled = !entry->Enabled;
	bool enabled = (enable != FALSE);

	if (enabled != entry->Enabled) {
		entry->Enabled = enabled;
		if (enabled) {
			entry->Style &= ~(DWORD)WS_DISABLED;
		} else {
			entry->Style |= WS_DISABLED;
		}
		SendMessageA(window, WM_ENABLE, enable != FALSE ? TRUE : FALSE, 0);
	}

	return(wasdisabled ? TRUE : FALSE);
}


BOOL CloseWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	entry->Style |= WS_MINIMIZE;
	return(TRUE);
}


static void Move_Window(UserWindow * window, int x, int y, int width, int height, bool repaint)
{
	bool moved = (window->Rect.left != x || window->Rect.top != y);
	bool resized = ((window->Rect.right - window->Rect.left) != width ||
					(window->Rect.bottom - window->Rect.top) != height);

	if (moved || resized) {
		Invalidate_Behind(window);
	}

	window->Rect.left = x;
	window->Rect.top = y;
	window->Rect.right = x + width;
	window->Rect.bottom = y + height;

	HWND handle = Handle_Of(window);

	if (moved) {
		POINT origin = Screen_Origin(window);
		SendMessageA(handle, WM_MOVE, 0, MAKELONG(origin.x, origin.y));
	}

	if (resized) {
		SendMessageA(handle, WM_SIZE, 0, MAKELONG(width, height));
	}

	if (repaint && window->Visible) {
		InvalidateRect(handle, nullptr, TRUE);
	}
}


BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	Move_Window(entry, x, y, width, height, repaint != FALSE);
	return(TRUE);
}


/// <summary>
/// Raises a window to the front of its siblings.
/// </summary>
static void Raise_Window(UserWindow * window)
{
	std::vector<UserWindow *> & siblings = (window->Parent != nullptr) ? window->Parent->Children : _TopLevel;

	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == window) {
			siblings.erase(siblings.begin() + index);
			siblings.insert(siblings.begin(), window);
			return;
		}
	}
}


static void Lower_Window(UserWindow * window)
{
	std::vector<UserWindow *> & siblings = (window->Parent != nullptr) ? window->Parent->Children : _TopLevel;

	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == window) {
			siblings.erase(siblings.begin() + index);
			siblings.push_back(window);
			return;
		}
	}
}


BOOL SetWindowPos(HWND window, HWND insertafter, int x, int y, int cx, int cy, UINT flags)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	int newx = ((flags & SWP_NOMOVE) != 0) ? entry->Rect.left : x;
	int newy = ((flags & SWP_NOMOVE) != 0) ? entry->Rect.top : y;
	int width = ((flags & SWP_NOSIZE) != 0) ? (entry->Rect.right - entry->Rect.left) : cx;
	int height = ((flags & SWP_NOSIZE) != 0) ? (entry->Rect.bottom - entry->Rect.top) : cy;

	Move_Window(entry, newx, newy, width, height, (flags & SWP_NOREDRAW) == 0);

	if ((flags & SWP_NOZORDER) == 0) {
		if (insertafter == HWND_BOTTOM) {
			Lower_Window(entry);
		} else {
			Raise_Window(entry);
		}
	}

	if ((flags & SWP_SHOWWINDOW) != 0) ShowWindow(window, SW_SHOW);
	if ((flags & SWP_HIDEWINDOW) != 0) ShowWindow(window, SW_HIDE);

	return(TRUE);
}


BOOL BringWindowToTop(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	Raise_Window(entry);
	return(TRUE);
}


HWND GetParent(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr || entry->Parent == nullptr) {
		return(nullptr);
	}

	return(Handle_Of(entry->Parent));
}


HWND GetDesktopWindow(void)
{
	/*
	 * There is no desktop window and nothing may be created as its child, so the honest
	 * answer is that there is none. HWND_DESKTOP is the same value, which is what
	 * MapWindowPoints callers pass to mean screen coordinates.
	 */
	return(nullptr);
}


HWND GetTopWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);

	if (entry == nullptr) {
		return(_TopLevel.empty() ? nullptr : Handle_Of(_TopLevel[0]));
	}

	return(entry->Children.empty() ? nullptr : Handle_Of(entry->Children[0]));
}


HWND GetWindow(HWND window, UINT command)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(nullptr);
	}

	std::vector<UserWindow *> const & siblings = (entry->Parent != nullptr) ? entry->Parent->Children : _TopLevel;

	int position = -1;
	for (unsigned int index = 0; index < siblings.size(); index++) {
		if (siblings[index] == entry) {
			position = (int)index;
			break;
		}
	}

	switch (command) {
		case GW_CHILD:
			return(entry->Children.empty() ? nullptr : Handle_Of(entry->Children[0]));

		case GW_HWNDFIRST:
			return(siblings.empty() ? nullptr : Handle_Of(siblings[0]));

		case GW_HWNDLAST:
			return(siblings.empty() ? nullptr : Handle_Of(siblings[siblings.size() - 1]));

		case GW_HWNDNEXT:
			if (position >= 0 && (unsigned int)(position + 1) < siblings.size()) {
				return(Handle_Of(siblings[position + 1]));
			}
			return(nullptr);

		case GW_HWNDPREV:
			if (position > 0) {
				return(Handle_Of(siblings[position - 1]));
			}
			return(nullptr);

		case GW_OWNER:
			return(nullptr);

		default:
			return(WIN32_UNSUPPORTED("GetWindow: a relationship the registry does not keep", (HWND)nullptr));
	}
}


BOOL IsChild(HWND parent, HWND window)
{
	UserWindow * parententry = Window_Of(parent);
	UserWindow * entry = Window_Of(window);

	if (parententry == nullptr || entry == nullptr) {
		return(FALSE);
	}

	for (UserWindow * walk = entry->Parent; walk != nullptr; walk = walk->Parent) {
		if (walk == parententry) {
			return(TRUE);
		}
	}

	return(FALSE);
}


HWND GetDlgItem(HWND dialog, int id)
{
	UserWindow * entry = Window_Of(dialog);
	if (entry == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < entry->Children.size(); index++) {
		if (entry->Children[index]->ID == id) {
			return(Handle_Of(entry->Children[index]));
		}

		HWND deeper = GetDlgItem(Handle_Of(entry->Children[index]), id);
		if (deeper != nullptr) {
			return(deeper);
		}
	}

	return(nullptr);
}


int GetDlgCtrlID(HWND window)
{
	UserWindow * entry = Window_Of(window);
	return(entry != nullptr ? entry->ID : 0);
}


HWND FindWindowA(LPCSTR classname, LPCSTR windowname)
{
	for (unsigned int index = 0; index < _TopLevel.size(); index++) {
		UserWindow * window = _TopLevel[index];

		if (classname != nullptr && (window->Class == nullptr || !Names_Match(window->Class->Name.c_str(), classname))) {
			continue;
		}

		if (windowname != nullptr && !Names_Match(window->Text.c_str(), windowname)) {
			continue;
		}

		return(Handle_Of(window));
	}

	return(nullptr);
}


BOOL EnumChildWindows(HWND parent, WNDENUMPROC callback, LPARAM parameter)
{
	UserWindow * entry = Window_Of(parent);
	if (entry == nullptr || callback == nullptr) {
		return(FALSE);
	}

	// The callback may destroy what it is handed, so the walk is over a snapshot.
	std::vector<UserWindow *> children = entry->Children;

	for (unsigned int index = 0; index < children.size(); index++) {
		if (Window_Of(Handle_Of(children[index])) == nullptr) {
			continue;
		}

		if (!callback(Handle_Of(children[index]), parameter)) {
			return(FALSE);
		}

		if (Window_Of(Handle_Of(children[index])) == nullptr) {
			continue;
		}

		if (!EnumChildWindows(Handle_Of(children[index]), callback, parameter)) {
			return(FALSE);
		}
	}

	return(TRUE);
}


HWND ChildWindowFromPoint(HWND parent, POINT point)
{
	UserWindow * entry = Window_Of(parent);
	if (entry == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < entry->Children.size(); index++) {
		UserWindow * child = entry->Children[index];

		if (point.x < child->Rect.left || point.x >= child->Rect.right) continue;
		if (point.y < child->Rect.top || point.y >= child->Rect.bottom) continue;

		return(Handle_Of(child));
	}

	return(parent);
}


HWND WindowFromPoint(POINT point)
{
	UserWindow * window = Window_From_Screen_Point(point);
	return(window != nullptr ? Handle_Of(window) : nullptr);
}


int MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count)
{
	if (points == nullptr) {
		return(0);
	}

	POINT fromorigin;
	fromorigin.x = 0;
	fromorigin.y = 0;
	UserWindow * fromwindow = Window_Of(from);
	if (fromwindow != nullptr) {
		fromorigin = Screen_Origin(fromwindow);
	}

	POINT toorigin;
	toorigin.x = 0;
	toorigin.y = 0;
	UserWindow * towindow = Window_Of(to);
	if (towindow != nullptr) {
		toorigin = Screen_Origin(towindow);
	}

	LONG dx = fromorigin.x - toorigin.x;
	LONG dy = fromorigin.y - toorigin.y;

	for (UINT index = 0; index < count; index++) {
		points[index].x += dx;
		points[index].y += dy;
	}

	return((int)MAKELONG(dx, dy));
}


/*
** ---------------------------------------------------------------------------------------
** Window words.
** ---------------------------------------------------------------------------------------
*/


LONG_PTR GetWindowLongPtrA(HWND window, int index)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	switch (index) {
		case GWL_WNDPROC:	return((LONG_PTR)entry->Procedure);
		case GWL_HINSTANCE:	return((LONG_PTR)entry->Instance);
		case GWL_HWNDPARENT: return((LONG_PTR)(entry->Parent != nullptr ? Handle_Of(entry->Parent) : nullptr));
		case GWL_STYLE:		return((LONG_PTR)entry->Style);
		case GWL_EXSTYLE:	return((LONG_PTR)entry->ExStyle);
		case GWL_USERDATA:	return(entry->UserData);
		case GWL_ID:		return((LONG_PTR)entry->ID);
		default:			break;
	}

	int const slot = (int)sizeof(LONG_PTR);
	if (index < 0 || (index % slot) != 0 || (unsigned int)(index / slot) >= entry->Extra.size()) {
		return(0);
	}

	return(entry->Extra[(unsigned int)(index / slot)]);
}


LONG GetWindowLongA(HWND window, int index)
{
	return((LONG)GetWindowLongPtrA(window, index));
}


LONG_PTR SetWindowLongPtrA(HWND window, int index, LONG_PTR value)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	LONG_PTR previous = GetWindowLongPtrA(window, index);

	switch (index) {
		case GWL_WNDPROC:
			entry->Procedure = (WNDPROC)value;
			return(previous);

		case GWL_HINSTANCE:
			entry->Instance = (HINSTANCE)value;
			return(previous);

		case GWL_STYLE:
			entry->Style = (DWORD)value;
			entry->Visible = ((entry->Style & WS_VISIBLE) != 0);
			entry->Enabled = ((entry->Style & WS_DISABLED) == 0);
			return(previous);

		case GWL_EXSTYLE:
			entry->ExStyle = (DWORD)value;
			return(previous);

		case GWL_USERDATA:
			entry->UserData = value;
			return(previous);

		case GWL_ID:
			entry->ID = (int)value;
			return(previous);

		case GWL_HWNDPARENT:
			return(WIN32_UNSUPPORTED("SetWindowLong: reparenting through GWL_HWNDPARENT", previous));

		default:
			break;
	}

	int const slot = (int)sizeof(LONG_PTR);
	if (index < 0 || (index % slot) != 0 || (unsigned int)(index / slot) >= entry->Extra.size()) {
		return(0);
	}

	entry->Extra[(unsigned int)(index / slot)] = value;
	return(previous);
}


LONG SetWindowLongA(HWND window, int index, LONG value)
{
	return((LONG)SetWindowLongPtrA(window, index, (LONG_PTR)value));
}


BOOL SetWindowTextA(HWND window, LPCSTR text)
{
	if (Window_Of(window) == nullptr) {
		return(FALSE);
	}

	return(SendMessageA(window, WM_SETTEXT, 0, (LPARAM)text) != 0 ? TRUE : FALSE);
}


int GetWindowTextA(HWND window, LPSTR text, int count)
{
	if (text == nullptr || count <= 0) {
		return(0);
	}

	text[0] = '\0';

	if (Window_Of(window) == nullptr) {
		return(0);
	}

	return((int)SendMessageA(window, WM_GETTEXT, (WPARAM)count, (LPARAM)text));
}


BOOL SetDlgItemTextA(HWND dialog, int id, LPCSTR text)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(FALSE);
	}

	return(SetWindowTextA(item, text));
}


UINT GetDlgItemTextA(HWND dialog, int id, LPSTR text, int count)
{
	if (text != nullptr && count > 0) {
		text[0] = '\0';
	}

	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(0);
	}

	return((UINT)GetWindowTextA(item, text, count));
}


BOOL CheckDlgButton(HWND dialog, int id, UINT check)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(FALSE);
	}

	SendMessageA(item, BM_SETCHECK, (WPARAM)check, 0);
	return(TRUE);
}


UINT IsDlgButtonChecked(HWND dialog, int id)
{
	HWND item = GetDlgItem(dialog, id);
	if (item == nullptr) {
		return(0);
	}

	return((UINT)SendMessageA(item, BM_GETCHECK, 0, 0));
}


/*
** ---------------------------------------------------------------------------------------
** Focus, activation and painting.
** ---------------------------------------------------------------------------------------
*/


HWND SetFocus(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (window != nullptr && entry == nullptr) {
		return(nullptr);
	}

	UserWindow * previous = _Focus;
	if (previous == entry) {
		return(previous != nullptr ? Handle_Of(previous) : nullptr);
	}

	_Focus = entry;

	if (previous != nullptr) {
		SendMessageA(Handle_Of(previous), WM_KILLFOCUS, (WPARAM)window, 0);
	}
	if (entry != nullptr) {
		SendMessageA(window, WM_SETFOCUS, (WPARAM)(previous != nullptr ? Handle_Of(previous) : nullptr), 0);
	}

	Update_Text_Input();

	return(previous != nullptr ? Handle_Of(previous) : nullptr);
}


HWND GetFocus(void)
{
	return(_Focus != nullptr ? Handle_Of(_Focus) : nullptr);
}


HWND GetActiveWindow(void)
{
	return(_Active != nullptr ? Handle_Of(_Active) : nullptr);
}


HWND SetActiveWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (window != nullptr && entry == nullptr) {
		return(nullptr);
	}

	UserWindow * previous = _Active;
	_Active = entry;

	if (previous != entry) {
		if (previous != nullptr) {
			SendMessageA(Handle_Of(previous), WM_ACTIVATE, 0, 0);
		}
		if (entry != nullptr) {
			SendMessageA(window, WM_ACTIVATE, 1, 0);
		}
	}

	return(previous != nullptr ? Handle_Of(previous) : nullptr);
}


HWND GetForegroundWindow(void)
{
	return(_Foreground != nullptr ? Handle_Of(_Foreground) : nullptr);
}


BOOL SetForegroundWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	_Foreground = entry;
	Raise_Window(entry);
	SetActiveWindow(window);
	return(TRUE);
}


BOOL InvalidateRect(HWND window, RECT const * rect, BOOL)
{
	if (window == nullptr) {
		for (unsigned int index = 0; index < _Windows.size(); index++) {
			_Windows[index]->NeedsPaint = true;
			Win32_User_Client_Rect(Handle_Of(_Windows[index]), &_Windows[index]->UpdateRect);
		}
		return(TRUE);
	}

	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	RECT client;
	Win32_User_Client_Rect(window, &client);

	if (rect == nullptr) {
		entry->UpdateRect = client;
	} else if (entry->NeedsPaint) {
		UnionRect(&entry->UpdateRect, &entry->UpdateRect, rect);
	} else {
		entry->UpdateRect = *rect;
	}

	entry->NeedsPaint = true;
	return(TRUE);
}


BOOL ValidateRect(HWND window, RECT const *)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	/*
	** Only a whole-window validation is kept. Every caller in the engine validates the
	** whole window, so tracking a region would be bookkeeping nothing reads back.
	*/
	entry->NeedsPaint = false;
	SetRectEmpty(&entry->UpdateRect);
	return(TRUE);
}


BOOL GetUpdateRect(HWND window, LPRECT rect, BOOL)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		if (rect != nullptr) SetRectEmpty(rect);
		return(FALSE);
	}

	if (rect != nullptr) {
		*rect = entry->UpdateRect;
	}

	return(entry->NeedsPaint ? TRUE : FALSE);
}


BOOL UpdateWindow(HWND window)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	if (!entry->NeedsPaint || !entry->Visible || entry->Painting) {
		return(TRUE);
	}

	/*
	** The update region survives the paint, as it does on Windows: a control asks what it
	** has to repaint while it is repainting, and validates itself when it is done. So the
	** recursion a procedure that paints by calling back here would cause is guarded
	** against directly rather than by dropping the region first.
	*/
	entry->Painting = true;
	SendMessageA(window, WM_PAINT, 0, 0);
	entry->Painting = false;
	return(TRUE);
}


BOOL RedrawWindow(HWND window, RECT const * update, HRGN, UINT flags)
{
	if (!InvalidateRect(window, update, FALSE)) {
		return(FALSE);
	}

	if ((flags & RDW_UPDATENOW_FLAG) != 0) {
		UpdateWindow(window);
	}

	return(TRUE);
}


/*
** ---------------------------------------------------------------------------------------
** Hot keys.
** ---------------------------------------------------------------------------------------
*/


BOOL RegisterHotKey(HWND window, int id, UINT modifiers, UINT key)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _HotKeys.size(); index++) {
		if (_HotKeys[index].Window == entry && _HotKeys[index].ID == id) {
			return(FALSE);
		}
	}

	UserHotKey hotkey;
	hotkey.Window = entry;
	hotkey.ID = id;
	hotkey.Modifiers = modifiers;
	hotkey.Key = key;
	_HotKeys.push_back(hotkey);
	return(TRUE);
}


BOOL UnregisterHotKey(HWND window, int id)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _HotKeys.size(); index++) {
		if (_HotKeys[index].Window == entry && _HotKeys[index].ID == id) {
			_HotKeys.erase(_HotKeys.begin() + index);
			return(TRUE);
		}
	}

	return(FALSE);
}


/*
** ---------------------------------------------------------------------------------------
** Trivia the engine asks for on the way up.
** ---------------------------------------------------------------------------------------
*/


BOOL InitCommonControls(void)
{
	/*
	 * The common controls this build uses are the owner-drawn classes ownrdraw.cpp
	 * registers for itself, so there is no library here to bring up.
	 */
	return(TRUE);
}


/// <summary>
/// Answers for an icon the page has no way to show.
/// </summary>
/// <remarks>
/// A tab's icon belongs to the page, not to the engine, so what comes back is a token that
/// names the request rather than an image. Nothing on this target draws an HICON; a
/// caller that started to would find nothing behind it, which is why the token is distinct
/// per request instead of a single shared value.
/// </remarks>
HICON LoadIconA(HINSTANCE, LPCSTR name)
{
	static int _tokens = 0;

	if (name == nullptr) {
		return(nullptr);
	}

	_tokens++;
	return((HICON)(ULONG_PTR)(0x1C00 + _tokens));
}


/*
** ---------------------------------------------------------------------------------------
** Dialogs.
** ---------------------------------------------------------------------------------------
**
** A dialog template is a compiled description of a window and the controls inside it, and
** the dialog manager is what turns one into windows. The front end has nothing else: every
** screen behind the main menu is a template in the shipped language library, so a template
** that is not read is a screen that does not exist.
**
** Two shapes of template are in circulation and the library carries both. The extended one
** announces itself with a first double word of 0xFFFF0001, which no classic one can begin
** with because that would be a style with two undefined bits and an item count of one in
** the wrong place. Everything after the fixed head is variable: name fields are either an
** ordinal or a UTF-16 string, item templates start on four byte boundaries, and each one
** ends with a block of creation data only the control it belongs to can read.
*/


/*
** The dialog base units. Windows measures a template in units of a quarter of the system
** font's average character width and an eighth of its height. There is no system font on a
** page, so the classic 8 by 16 stands in for it. The shipped templates were not laid out
** against those units -- they name MS Sans Serif 8, which is a good deal narrower -- but
** the number chosen here does not decide the layout: windlg.cpp measures its reference
** template through these same units and carries every dialog by the ratio, so a base unit
** only has to be the one both sides agree on.
*/
static int const DIALOG_BASE_UNIT_X = 8;
static int const DIALOG_BASE_UNIT_Y = 16;

// The class a dialog with no class of its own belongs to, spelled as Windows spells it.
static char const * const DIALOG_CLASS_NAME = "#32770";

// The first double word of an extended template, and nothing else.
static DWORD const DIALOG_TEMPLATE_EX_SIGNATURE = 0xFFFF0001;

/*
** How far a template of unknown length may be walked. Windows walks one with no bound at
** all, because a template is a resource the program shipped; data.cpp knows the length of
** every resource it hands out and that length is what bounds a template from there. A
** template from anywhere else -- one a caller built in memory -- gets this instead of a
** walk with no end, and it is an order of magnitude past the largest template shipped.
*/
static unsigned int const DIALOG_TEMPLATE_LIMIT = 64u * 1024u;


/*
** What a modal dialog leaves behind. DialogBox runs its own message pump and returns what
** EndDialog was called with, so the two need somewhere to meet.
*/
struct ModalDialog
{
	HWND Window;
	INT_PTR Result;
	bool Ended;
};

static std::vector<ModalDialog *> _ModalDialogs;


/// What a template's name field turned out to hold.
enum class TemplateNameType
{
	Empty,
	Ordinal,
	Text
};


/*
** A read head over one template, which stops at the end of the resource rather than
** running past it. data.cpp remembers how long each resource it hands out is, which is
** what makes the end knowable at all.
*/
struct DialogTemplateReader
{
	unsigned char const * Base;
	unsigned char const * Point;
	unsigned char const * End;
	bool Overrun;

	DialogTemplateReader(void const * data, unsigned int size) :
		Base((unsigned char const *)data),
		Point((unsigned char const *)data),
		End((unsigned char const *)data + size),
		Overrun(false)
	{
	}

	bool Has(unsigned int bytes) const
	{
		return(!Overrun && (unsigned int)(End - Point) >= bytes);
	}

	WORD Fetch_Word(void)
	{
		if (!Has(2)) {
			Overrun = true;
			return(0);
		}

		WORD value;
		memcpy(&value, Point, sizeof(value));
		Point += sizeof(value);
		return(value);
	}

	DWORD Fetch_Long(void)
	{
		DWORD low = Fetch_Word();
		DWORD high = Fetch_Word();
		return(low | (high << 16));
	}

	short Fetch_Short(void)
	{
		return((short)Fetch_Word());
	}

	void Skip(unsigned int bytes)
	{
		if (!Has(bytes)) {
			Overrun = true;
			Point = End;
			return;
		}

		Point += bytes;
	}

	/// Advances to the next four byte boundary, measured from the start of the template.
	void Align(void)
	{
		Skip((unsigned int)((4 - ((size_t)(Point - Base) & 3)) & 3));
	}

	TemplateNameType Fetch_Name(std::string & text, unsigned int & ordinal);
};


/// <summary>
/// Reads one of a template's name fields.
/// </summary>
/// <remarks>
/// A template writes the six original control classes as ordinals and everything else as
/// UTF-16. The engine's captions and class names are ASCII, so a character outside it is
/// stood in for rather than encoded into a string the rest of the engine reads as bytes.
/// </remarks>
TemplateNameType DialogTemplateReader::Fetch_Name(std::string & text, unsigned int & ordinal)
{
	text.clear();
	ordinal = 0;

	WORD character = Fetch_Word();

	if (character == 0) {
		return(TemplateNameType::Empty);
	}

	if (character == 0xFFFF) {
		ordinal = Fetch_Word();
		return(TemplateNameType::Ordinal);
	}

	while (character != 0 && !Overrun) {
		text.push_back((character < 0x80) ? (char)character : '?');
		character = Fetch_Word();
	}

	return(TemplateNameType::Text);
}


struct DialogTemplateHeader
{
	DWORD Style;
	DWORD ExStyle;
	unsigned int ItemCount;
	int X;
	int Y;
	int CX;
	int CY;
	std::string Class;
	std::string Title;
	bool HasClass;
};


struct DialogTemplateItem
{
	DWORD Style;
	DWORD ExStyle;
	int X;
	int Y;
	int CX;
	int CY;
	int ID;
	std::string Class;
	std::string Title;
};


static int Dialog_Units_To_Pixels_X(int units)
{
	return(units * DIALOG_BASE_UNIT_X / 4);
}


static int Dialog_Units_To_Pixels_Y(int units)
{
	return(units * DIALOG_BASE_UNIT_Y / 8);
}


/// <summary>
/// Reads a template's head, up to the first item.
/// </summary>
/// <param name="extended">Receives whether this is an extended template, which the items
/// after it are laid out differently for.</param>
/// <returns>bool; true when the head was read without running off the end.</returns>
static bool Read_Dialog_Header(DialogTemplateReader & reader, DialogTemplateHeader & header, bool & extended)
{
	extended = false;
	if (reader.Has(4)) {
		DWORD signature;
		memcpy(&signature, reader.Point, sizeof(signature));
		extended = (signature == DIALOG_TEMPLATE_EX_SIGNATURE);
	}

	if (extended) {
		reader.Skip(4);					// the version and the signature
		reader.Fetch_Long();			// the help identifier
		header.ExStyle = reader.Fetch_Long();
		header.Style = reader.Fetch_Long();
	} else {
		header.Style = reader.Fetch_Long();
		header.ExStyle = reader.Fetch_Long();
	}

	header.ItemCount = reader.Fetch_Word();
	header.X = reader.Fetch_Short();
	header.Y = reader.Fetch_Short();
	header.CX = reader.Fetch_Short();
	header.CY = reader.Fetch_Short();

	std::string scratch;
	unsigned int ordinal = 0;

	reader.Fetch_Name(scratch, ordinal);	// the menu, which nothing here can show

	header.HasClass = (reader.Fetch_Name(header.Class, ordinal) == TemplateNameType::Text);
	reader.Fetch_Name(header.Title, ordinal);

	if ((header.Style & DS_SETFONT) != 0) {
		reader.Fetch_Word();				// the point size
		if (extended) {
			reader.Fetch_Word();			// the weight
			reader.Skip(2);					// the italic flag and the character set
		}
		reader.Fetch_Name(scratch, ordinal);	// the face name
	}

	return(!reader.Overrun);
}


/// <summary>
/// Reads one item template.
/// </summary>
/// <returns>bool; true when the item was read without running off the end.</returns>
static bool Read_Dialog_Item(DialogTemplateReader & reader, bool extended, DialogTemplateItem & item)
{
	reader.Align();

	if (extended) {
		reader.Fetch_Long();			// the help identifier
		item.ExStyle = reader.Fetch_Long();
		item.Style = reader.Fetch_Long();
	} else {
		item.Style = reader.Fetch_Long();
		item.ExStyle = reader.Fetch_Long();
	}

	item.X = reader.Fetch_Short();
	item.Y = reader.Fetch_Short();
	item.CX = reader.Fetch_Short();
	item.CY = reader.Fetch_Short();

	/*
	** A classic template identifies a control with a word, so the placeholder the resource
	** compiler writes for an unused identifier arrives as 65535 rather than as -1. Windows
	** widens it the same way, and the front end compares against the widened form.
	*/
	item.ID = extended ? (int)reader.Fetch_Long() : (int)reader.Fetch_Word();

	unsigned int ordinal = 0;
	if (reader.Fetch_Name(item.Class, ordinal) == TemplateNameType::Ordinal) {
		char const * name = Win32_Stock_Control_Class(ordinal);
		item.Class = (name != nullptr) ? name : "";
	}

	reader.Fetch_Name(item.Title, ordinal);

	/*
	** The creation data block, which only the control it was written for can read. Its
	** leading word counts itself, and a template with nothing to say writes a bare zero.
	*/
	WORD creation = reader.Fetch_Word();
	if (creation > sizeof(WORD)) {
		reader.Skip(creation - sizeof(WORD));
	}

	return(!reader.Overrun);
}


/// <summary>
/// Runs a dialog's messages through its dialog procedure first.
/// </summary>
/// <remarks>
/// This is the class procedure every dialog window is created with, and the DWL_DLGPROC
/// word is what makes a window a dialog as far as the rest of the engine is concerned:
/// ownrdraw.cpp reads it to tell a dialog from one of its controls. A dialog procedure
/// reports whether it handled a message rather than returning a result, and leaves a real
/// result in DWL_MSGRESULT where the message carries one.
/// </remarks>
static LRESULT CALLBACK Dialog_Window_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	DLGPROC procedure = (DLGPROC)(ULONG_PTR)GetWindowLongA(window, DWL_DLGPROC);

	if (procedure != nullptr) {
		SetWindowLongA(window, DWL_MSGRESULT, 0);

		INT_PTR handled = procedure(window, message, wparam, lparam);
		if (handled != 0) {
			LONG result = GetWindowLongA(window, DWL_MSGRESULT);
			return((result != 0) ? (LRESULT)result : (LRESULT)handled);
		}
	}

	return(DefWindowProcA(window, message, wparam, lparam));
}


static void Register_Dialog_Class(void)
{
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	WNDCLASSA windowclass;
	memset(&windowclass, 0, sizeof(windowclass));

	windowclass.style = CS_DBLCLKS;
	windowclass.lpfnWndProc = Dialog_Window_Proc;
	windowclass.cbWndExtra = DLGWINDOWEXTRA;
	windowclass.lpszClassName = DIALOG_CLASS_NAME;

	RegisterClassA(&windowclass);
}


/// <summary>
/// Finds the control a dialog should open with the focus on.
/// </summary>
/// <returns>Returns with the first visible, enabled tab stop, or NULL when the dialog has
/// none.</returns>
static HWND First_Tab_Stop(HWND dialog)
{
	UserWindow * entry = Window_Of(dialog);
	if (entry == nullptr) {
		return(nullptr);
	}

	/*
	** Tab order follows the template, and a child created later sits in front of its
	** siblings, so the template order is the sibling chain read backwards.
	*/
	for (unsigned int index = entry->Children.size(); index > 0; index--) {
		UserWindow * child = entry->Children[index - 1];

		if ((child->Style & WS_TABSTOP) == 0) continue;
		if (!child->Visible || !child->Enabled) continue;

		return(Handle_Of(child));
	}

	return(nullptr);
}


DWORD GetDialogBaseUnits(void)
{
	return(MAKELONG(DIALOG_BASE_UNIT_X, DIALOG_BASE_UNIT_Y));
}


HWND CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATE dialogtemplate, HWND parent,
	DLGPROC dialogproc, LPARAM initparam)
{
	if (dialogtemplate == nullptr) {
		return(nullptr);
	}

	unsigned int size = Fetch_Resource_Size(dialogtemplate);
	if (size == 0) {
		size = DIALOG_TEMPLATE_LIMIT;
	}

	Register_Dialog_Class();
	Win32_Register_Stock_Controls();

	DialogTemplateReader reader(dialogtemplate, size);
	DialogTemplateHeader header;
	bool extended = false;

	if (!Read_Dialog_Header(reader, header, extended)) {
		return(nullptr);
	}

	char const * classname = header.HasClass ? header.Class.c_str() : DIALOG_CLASS_NAME;

	/*
	** The dialog is built before it is shown, so that a procedure handling WM_INITDIALOG
	** can move and populate it without any of that being seen happening.
	*/
	HWND dialog = CreateWindowExA(header.ExStyle, classname, header.Title.c_str(),
		header.Style & ~(DWORD)WS_VISIBLE,
		Dialog_Units_To_Pixels_X(header.X), Dialog_Units_To_Pixels_Y(header.Y),
		Dialog_Units_To_Pixels_X(header.CX), Dialog_Units_To_Pixels_Y(header.CY),
		parent, nullptr, instance, nullptr);

	if (dialog == nullptr) {
		return(nullptr);
	}

	SetWindowLongA(dialog, DWL_DLGPROC, (LONG)(ULONG_PTR)dialogproc);

	for (unsigned int index = 0; index < header.ItemCount; index++) {
		DialogTemplateItem item;

		if (!Read_Dialog_Item(reader, extended, item)) {
			break;
		}

		if (item.Class.empty()) {
			continue;
		}

		CreateWindowExA(item.ExStyle, item.Class.c_str(), item.Title.c_str(), item.Style | WS_CHILD,
			Dialog_Units_To_Pixels_X(item.X), Dialog_Units_To_Pixels_Y(item.Y),
			Dialog_Units_To_Pixels_X(item.CX), Dialog_Units_To_Pixels_Y(item.CY),
			dialog, (HMENU)(ULONG_PTR)item.ID, instance, nullptr);
	}

	HWND focus = First_Tab_Stop(dialog);

	if (SendMessageA(dialog, WM_INITDIALOG, (WPARAM)focus, initparam) != 0 && focus != nullptr) {
		SetFocus(focus);
	}

	if ((header.Style & WS_VISIBLE) != 0) {
		ShowWindow(dialog, SW_SHOW);
	}

	return(dialog);
}


/// <summary>
/// Creates a dialog from a template named in a module's resources.
/// </summary>
/// <remarks>
/// The language library is the only resource image this target has, so a template asked
/// for out of the executable is looked for there and honestly not found when it is
/// somewhere else. windlg.cpp measures its layout against such a template and carries a
/// fallback for exactly this.
/// </remarks>
HWND CreateDialogParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc,
	LPARAM initparam)
{
	void const * dialogtemplate = Fetch_Resource(templatename, (LPCSTR)RT_DIALOG);
	if (dialogtemplate == nullptr) {
		return(nullptr);
	}

	return(CreateDialogIndirectParamA(instance, (LPCDLGTEMPLATE)dialogtemplate, parent, dialogproc, initparam));
}


INT_PTR DialogBoxParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc,
	LPARAM initparam)
{
	HWND dialog = CreateDialogParamA(instance, templatename, parent, dialogproc, initparam);
	if (dialog == nullptr) {
		return(-1);
	}

	ModalDialog modal;
	modal.Window = dialog;
	modal.Result = 0;
	modal.Ended = false;
	_ModalDialogs.push_back(&modal);

	ShowWindow(dialog, SW_SHOW);
	SetFocus(dialog);

	/*
	** The modal pump, which is the engine's own service loop rather than a wait: a page
	** owns the thread the engine is borrowing, so a loop that does not hand it back stops
	** the tab rather than blocking a thread.
	*/
	while (!modal.Ended && IsWindow(dialog)) {
		Browser_Service();
		Win32_User_Service();

		MSG message;
		while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (IsDialogMessageA(dialog, &message)) {
				continue;
			}
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}

		Browser_Yield_If_Due();
	}

	for (unsigned int index = 0; index < _ModalDialogs.size(); index++) {
		if (_ModalDialogs[index] == &modal) {
			_ModalDialogs.erase(_ModalDialogs.begin() + index);
			break;
		}
	}

	if (IsWindow(dialog)) {
		DestroyWindow(dialog);
	}

	return(modal.Result);
}


BOOL EndDialog(HWND dialog, INT_PTR result)
{
	for (unsigned int index = 0; index < _ModalDialogs.size(); index++) {
		if (_ModalDialogs[index]->Window == dialog) {
			_ModalDialogs[index]->Result = result;
			_ModalDialogs[index]->Ended = true;
			return(TRUE);
		}
	}

	/*
	** A modeless dialog was never waited on, so there is nothing to hand the result to and
	** the only thing left to do is what closing it would have done.
	*/
	return(DestroyWindow(dialog));
}


HWND GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous)
{
	UserWindow * entry = Window_Of(dialog);
	if (entry == nullptr || entry->Children.empty()) {
		return(nullptr);
	}

	/*
	** Template order, which the sibling chain holds backwards because a child created
	** later sits in front of the ones before it.
	*/
	std::vector<UserWindow *> order(entry->Children.rbegin(), entry->Children.rend());

	int start = -1;
	for (unsigned int index = 0; index < order.size(); index++) {
		if (Handle_Of(order[index]) == control) {
			start = (int)index;
			break;
		}
	}

	int count = (int)order.size();
	int step = (previous != FALSE) ? -1 : 1;

	for (int distance = 1; distance <= count; distance++) {
		int index = ((start + step * distance) % count + count) % count;
		UserWindow * candidate = order[(unsigned int)index];

		if ((candidate->Style & WS_TABSTOP) == 0) continue;
		if (!candidate->Visible || !candidate->Enabled) continue;

		return(Handle_Of(candidate));
	}

	return(nullptr);
}


HWND GetNextDlgGroupItem(HWND, HWND, BOOL) { return(WIN32_STUB((HWND)nullptr)); }


/// <summary>
/// Offers a message to a modeless dialog before the normal handling sees it.
/// </summary>
/// <remarks>
/// A message aimed anywhere but this dialog is declined, so the caller's loop -- which
/// offers every message to every open dialog in turn -- goes on to dispatch it normally.
/// What is claimed here is the keyboard navigation a dialog is expected to do for itself.
/// </remarks>
BOOL IsDialogMessageA(HWND dialog, LPMSG message)
{
	if (dialog == nullptr || message == nullptr) {
		return(FALSE);
	}

	if (message->hwnd != dialog && !IsChild(dialog, message->hwnd)) {
		return(FALSE);
	}

	if (message->message == WM_KEYDOWN) {
		LRESULT code = SendMessageA(message->hwnd, WM_GETDLGCODE, 0, 0);

		switch (message->wParam) {
			case VK_TAB:
				if ((code & DLGC_WANTTAB) == 0) {
					HWND next = GetNextDlgTabItem(dialog, message->hwnd,
						Browser_Key_Is_Down(VK_SHIFT) ? TRUE : FALSE);
					if (next != nullptr) {
						SetFocus(next);
					}
					return(TRUE);
				}
				break;

			case VK_RETURN:
				if ((code & DLGC_WANTALLKEYS) == 0) {
					SendMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
					return(TRUE);
				}
				break;

			case VK_ESCAPE:
				SendMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
				return(TRUE);

			default:
				break;
		}
	}

	TranslateMessage(message);
	DispatchMessageA(message);
	return(TRUE);
}


/*
** ---------------------------------------------------------------------------------------
** The page's input, as messages.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Should this window receive key presses as messages?
/// </summary>
/// <remarks>
/// browser.cpp drains the page's key events into the engine's keyboard buffer, which is the
/// main window's keyboard path on this target. Delivering them again as messages would
/// double every key, so only the windows the keyboard buffer does not serve -- the dialogs
/// and their controls -- are fed from here. A mouse event carries no such reservation: it
/// is delivered as a message wherever it lands, and the window it lands on is what puts it
/// in the keyboard buffer.
/// </remarks>
static bool Takes_Keys(UserWindow const * window)
{
	return(window != nullptr && window->Parent != nullptr && window->Enabled);
}


/// <summary>
/// Tells the page whether what holds the focus is somewhere the player may type.
/// </summary>
/// <remarks>
/// A device whose only keyboard is drawn on its screen raises one for whatever the page has
/// focused, so the engine's own focus is what has to be relayed. WM_GETDLGCODE is the
/// question Windows already asks a control about what it wants from the keyboard, and an
/// edit control is the one that answers DLGC_WANTCHARS; a window the keys are not delivered
/// to in the first place is not asked at all. Answering the question can move the focus
/// again -- ownrdraw.cpp deflects focus away from an edit control it has not yet revealed --
/// and the nested SetFocus that does so has already settled this, so its answer stands.
///
/// The hall of fame asks for a keyboard directly, because there is no control to focus and
/// nothing here can speak for it.
/// </remarks>
static void Update_Text_Input(void)
{
	UserWindow * focus = _Focus;
	bool wanted = false;

	if (Takes_Keys(focus)) {
		LRESULT code = SendMessageA(Handle_Of(focus), WM_GETDLGCODE, 0, 0);
		wanted = ((code & DLGC_WANTCHARS) != 0) && (_Focus == focus);
	}

	if (wanted) {
		Browser_Begin_Text_Input();
	} else {
		Browser_End_Text_Input();
	}
}


static WPARAM Mouse_Button_Flags(void)
{
	WPARAM flags = 0;

	if (Browser_Key_Is_Down(VK_LBUTTON)) flags |= MK_LBUTTON;
	if (Browser_Key_Is_Down(VK_RBUTTON)) flags |= MK_RBUTTON;
	if (Browser_Key_Is_Down(VK_MBUTTON)) flags |= MK_MBUTTON;

	unsigned short modifiers = Browser_Key_Modifiers();
	if ((modifiers & WWKEY_SHIFT_BIT) != 0) flags |= MK_SHIFT;
	if ((modifiers & WWKEY_CTRL_BIT) != 0) flags |= MK_CONTROL;

	return(flags);
}


/// <summary>
/// Queues a mouse message for whichever window owns the position.
/// </summary>
/// <returns>bool; Did a window take it?</returns>
static bool Deliver_Mouse(UINT message, WPARAM wparam, POINT screen)
{
	UserWindow * target = Window_Of(GetCapture());
	if (target == nullptr) {
		target = Window_From_Screen_Point(screen);
	}

	if (target == nullptr || !target->Enabled) {
		return(false);
	}

	POINT origin = Screen_Origin(target);
	return(Queue_Message(Handle_Of(target), message, wparam,
		MAKELONG((short)(screen.x - origin.x), (short)(screen.y - origin.y))));
}


/// <summary>
/// Turns one of the page's mouse events into a window message.
/// </summary>
/// <remarks>
/// A press and its release both arrive well inside a single engine pass, so reading the
/// button state back once a pass loses the click entirely. The events are taken from
/// browser.cpp's drain instead, where every one of them is seen exactly once.
///
/// An event this claims does not go on to the keyboard buffer, because the window it is
/// delivered to is what puts it there -- the main window's procedure hands its mouse
/// messages to the keyboard handler, exactly as it does on Windows. An event no window
/// takes is left for browser.cpp to post, which is what carries the front end before a
/// window exists at all.
/// </remarks>
static bool Consume_Browser_Event(unsigned short key, int x, int y, bool is_mouse, bool is_release)
{
	if (!is_mouse) {
		return(false);
	}

	UINT message;
	WPARAM button;

	switch (key) {
		case VK_LBUTTON:
			message = is_release ? WM_LBUTTONUP : WM_LBUTTONDOWN;
			button = MK_LBUTTON;
			break;

		case VK_RBUTTON:
			message = is_release ? WM_RBUTTONUP : WM_RBUTTONDOWN;
			button = MK_RBUTTON;
			break;

		case VK_MBUTTON:
			message = is_release ? WM_MBUTTONUP : WM_MBUTTONDOWN;
			button = MK_MBUTTON;
			break;

		default:
			return(false);
	}

	/*
	 * The page reports a position in the frame, and a mouse message carries one in window
	 * pixels. They differ whenever the frame is scaled onto the canvas, and msgroute.cpp
	 * is written against the window pixel form.
	 */
	POINT screen;
	screen.x = x;
	screen.y = y;
	Game_Point_To_Window(screen);

	/*
	 * The button flags describe the state the message is delivered in, which for the button
	 * the message is about is the state the event just established.
	 */
	WPARAM flags = Mouse_Button_Flags();
	if (is_release) {
		flags &= ~button;
	} else {
		flags |= button;
	}

	// A window tracks the cursor from the moves it is sent, so it is owed the one this press
	// arrived at before the press itself.
	if (screen.x != _LastMouse.x || screen.y != _LastMouse.y) {
		_LastMouse = screen;
		Deliver_Mouse(WM_MOUSEMOVE, flags, screen);
	}

	return(Deliver_Mouse(message, flags, screen));
}


static void Service_Mouse(void)
{
	POINT screen;
	screen.x = Browser_Mouse_X();
	screen.y = Browser_Mouse_Y();
	Game_Point_To_Window(screen);

	if (screen.x == _LastMouse.x && screen.y == _LastMouse.y) {
		return;
	}

	_LastMouse = screen;
	Deliver_Mouse(WM_MOUSEMOVE, Mouse_Button_Flags(), screen);
}


static void Service_Hot_Keys(unsigned short key, unsigned short modifiers)
{
	UINT pressed = 0;
	if ((modifiers & WWKEY_ALT_BIT) != 0) pressed |= MOD_ALT;
	if ((modifiers & WWKEY_CTRL_BIT) != 0) pressed |= MOD_CONTROL;
	if ((modifiers & WWKEY_SHIFT_BIT) != 0) pressed |= MOD_SHIFT;

	for (unsigned int index = 0; index < _HotKeys.size(); index++) {
		if (_HotKeys[index].Key != key || _HotKeys[index].Modifiers != pressed) {
			continue;
		}

		Queue_Message(Handle_Of(_HotKeys[index].Window), WM_HOTKEY_MESSAGE,
			(WPARAM)_HotKeys[index].ID, MAKELONG(pressed, key));
	}
}


static void Service_Keyboard(void)
{
	unsigned short modifiers = Browser_Key_Modifiers();
	bool alt = ((modifiers & WWKEY_ALT_BIT) != 0);

	for (int key = 0; key < 256; key++) {
		bool down = Browser_Key_Is_Down((unsigned short)key);
		if (down == (_LastKeyDown[key] != 0)) {
			continue;
		}

		_LastKeyDown[key] = down ? 1 : 0;

		if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON) {
			continue;
		}

		if (down) {
			Service_Hot_Keys((unsigned short)key, modifiers);
		}

		if (!Takes_Keys(_Focus)) {
			continue;
		}

		UINT message;
		if (alt) {
			message = down ? WM_SYSKEYDOWN : WM_SYSKEYUP;
		} else {
			message = down ? WM_KEYDOWN : WM_KEYUP;
		}

		// The repeat count, and the transition state bit a key release carries.
		LPARAM lparam = down ? 1 : (LPARAM)(1 | (1L << 30) | (1L << 31));

		Queue_Message(Handle_Of(_Focus), message, (WPARAM)key, lparam);
	}
}


void Win32_User_Service(void)
{
	if (!_InputStarted) {
		_InputStarted = true;
		memset(_LastKeyDown, 0, sizeof(_LastKeyDown));
		_LastMouse.x = Browser_Mouse_X();
		_LastMouse.y = Browser_Mouse_Y();
		Game_Point_To_Window(_LastMouse);
		Browser_Set_Event_Hook(Consume_Browser_Event);
	}

	Service_Mouse();
	Service_Keyboard();
}


/*
** ---------------------------------------------------------------------------------------
** The message box.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Puts a message box on the page and waits for an answer.
/// </summary>
/// <remarks>
/// The engine asks real questions here -- the low disk space prompt acts on a yes or no --
/// so answering for the player is not an option. A page offers confirm(), which would do
/// it, but confirm() stops the page: nothing composites, nothing is scheduled, and a
/// browser is entitled to suppress it outright. So the box is laid out in the page instead
/// and the wait is the engine's own yield, which is what keeps the tab alive while the
/// question stands.
///
/// Without the yield scaffold there is nothing to wait on, and a wait that cannot return
/// is worse than no box at all; that build logs the question and reports the box cancelled,
/// which is what the Win32 original returns when it cannot put one up.
/// </remarks>
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
	char const * body = (text != nullptr) ? text : "";
	char const * title = (caption != nullptr) ? caption : "OpenTS";

	fprintf(stderr, "OpenTS message box [%s]: %s\n", title, body);
	fflush(stderr);

	if (!Browser_Yield_Is_Available()) {
		return(WIN32_UNSUPPORTED("MessageBox: asking the player without the yield scaffold", IDCANCEL));
	}

	int cancelled;
	switch (type & 0x0000000FL) {
		case MB_OKCANCEL:			cancelled = IDCANCEL; break;
		case MB_ABORTRETRYIGNORE:	cancelled = IDIGNORE; break;
		case MB_YESNOCANCEL:		cancelled = IDCANCEL; break;
		case MB_YESNO:				cancelled = IDNO; break;
		case MB_RETRYCANCEL:		cancelled = IDCANCEL; break;
		default:					cancelled = IDOK; break;
	}

	/*
	 * The buttons and the identifiers they answer with are the Win32 contract; the page is
	 * only where they are drawn. A box already up is replaced rather than stacked, because
	 * only one question can be waited on at a time.
	 *
	 * Nothing below puts a comma anywhere but inside parentheses. The preprocessor splits
	 * this block on every other one, because braces and brackets do not group a macro
	 * argument. The identifiers are written as literals for the same reason -- a macro
	 * name inside a stringified block stays a name -- so they are pinned here.
	 */
	static_assert(IDOK == 1 && IDCANCEL == 2 && IDABORT == 3 && IDRETRY == 4, "message box results");
	static_assert(IDIGNORE == 5 && IDYES == 6 && IDNO == 7, "message box results");

	EM_ASM({
		var previous = document.getElementById("opents-messagebox");
		if (previous) previous.remove();

		var buttons = [];
		var add = function (label, id) { buttons.push(Array(label, id)); };

		if ($2 == 1) { add("OK", 1); add("Cancel", 2); }
		else if ($2 == 2) { add("Abort", 3); add("Retry", 4); add("Ignore", 5); }
		else if ($2 == 3) { add("Yes", 6); add("No", 7); add("Cancel", 2); }
		else if ($2 == 4) { add("Yes", 6); add("No", 7); }
		else if ($2 == 5) { add("Retry", 4); add("Cancel", 2); }
		else { add("OK", 1); }

		var box = document.createElement("div");
		box.id = "opents-messagebox";
		box.setAttribute("style",
			"position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;" +
			"justify-content:center;background:rgba(0,0,0,.6);" +
			"font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace");

		var panel = document.createElement("div");
		panel.setAttribute("style",
			"min-width:280px;max-width:min(560px,90vw);background:#14171c;color:#d8dbe0;" +
			"border:1px solid #2e343d;padding:16px 18px");

		var heading = document.createElement("div");
		heading.setAttribute("style", "font-weight:600;margin-bottom:10px");
		heading.textContent = UTF8ToString($0);

		var message = document.createElement("div");
		message.setAttribute("style", "white-space:pre-wrap;margin-bottom:16px");
		message.textContent = UTF8ToString($1);

		var row = document.createElement("div");
		row.setAttribute("style", "display:flex;gap:8px;justify-content:flex-end");

		buttons.forEach(function (entry) {
			var button = document.createElement("button");
			button.type = "button";
			button.textContent = entry[0];
			button.setAttribute("style",
				"font:inherit;padding:5px 14px;background:#222831;color:#d8dbe0;" +
				"border:1px solid #3a424d;cursor:pointer");
			button.addEventListener("click", function () { window.__opentsMessageBox = entry[1]; });
			row.appendChild(button);
		});

		panel.appendChild(heading);
		panel.appendChild(message);
		panel.appendChild(row);
		box.appendChild(panel);
		document.body.appendChild(box);

		window.__opentsMessageBox = 0;
	}, title, body, (int)(type & 0x0000000FL));

	int answer = 0;
	while (answer == 0) {
		Browser_Yield();
		answer = EM_ASM_INT({ return window.__opentsMessageBox | 0; });
	}

	EM_ASM({
		var box = document.getElementById("opents-messagebox");
		if (box) box.remove();
		window.__opentsMessageBox = 0;
	});

	/*
	** A box the page took away rather than answered reports the same result Windows does
	** when the player dismisses it without choosing.
	*/
	return(answer > 0 ? answer : cancelled);
}


/// <summary>
/// Puts a message box on the page from a parameter block.
/// </summary>
/// <param name="parameters">The Win32 parameter block; its size field must be this build's.</param>
/// <returns>The identifier of the button chosen, or zero if no box could be put up.</returns>
/// <remarks>
/// Everything this form carries beyond MessageBox is either a Windows resource or a Windows
/// help hook: lpszIcon names an icon resource, dwContextHelpId and lpfnMsgBoxCallback answer
/// a Help button, dwLanguageId picks the button captions out of USER32, and hInstance is the
/// module the first two are looked up in. A page has none of them. No style draws an icon
/// here, so MB_USERICON loses an icon rather than gaining a wrong one, and MB_HELP is a
/// button that nothing behind it could answer. What is left is the text, the caption, the
/// owner and the style, and those are MessageBox.
///
/// The size field is the block's only statement of its own shape, so a block that does not
/// state this one's is refused rather than read as if it did.
/// </remarks>
int MessageBoxIndirectA(MSGBOXPARAMSA const * parameters)
{
	if (parameters == nullptr || parameters->cbSize != sizeof(MSGBOXPARAMSA)) {
		return(WIN32_UNSUPPORTED("MessageBoxIndirect: a parameter block of another shape", 0));
	}

	return(MessageBoxA(parameters->hwndOwner, parameters->lpszText, parameters->lpszCaption,
		parameters->dwStyle));
}


/*
** ---------------------------------------------------------------------------------------
** Keyboard state.
** ---------------------------------------------------------------------------------------
*/


SHORT GetKeyState(int) { return(WIN32_STUB(0)); }
SHORT GetAsyncKeyState(int) { return(WIN32_STUB(0)); }
BOOL GetKeyboardState(PBYTE) { return(WIN32_STUB(FALSE)); }
UINT MapVirtualKeyA(UINT, UINT) { return(WIN32_STUB(0)); }
int ToAscii(UINT, UINT, BYTE const *, LPWORD, UINT) { return(WIN32_STUB(0)); }
int GetKeyNameTextA(LONG, LPSTR buffer, int size) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
int TranslateAcceleratorA(HWND, HACCEL, LPMSG) { return(WIN32_STUB(0)); }


/*
** ---------------------------------------------------------------------------------------
** Menus.
** ---------------------------------------------------------------------------------------
*/


/*
** No window on this target has a menu bar. The window layer keeps no menu of its own, and
** the classes the engine registers name none, so NULL -- what Windows answers for a window
** without a menu -- is the result rather than a gap.
*/
HMENU GetMenu(HWND) { return(nullptr); }
HMENU GetSystemMenu(HWND, BOOL) { return(WIN32_STUB((HMENU)nullptr)); }
BOOL DeleteMenu(HMENU, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL EnableMenuItem(HMENU, UINT, UINT) { return(WIN32_STUB(FALSE)); }


/*
** ---------------------------------------------------------------------------------------
** Window scroll bars.
** ---------------------------------------------------------------------------------------
*/


/*
** A window's own scroll information, not the scroll bar control's: the model those two
** would read belongs to Scroll_Bar_Proc in win32ctrl.cpp, and no window here keeps one.
*/
BOOL GetScrollInfo(HWND, int, LPSCROLLINFO) { return(WIN32_STUB(FALSE)); }
int SetScrollInfo(HWND, int, LPCSCROLLINFO, BOOL) { return(WIN32_STUB(0)); }


/*
** ---------------------------------------------------------------------------------------
** Context help.
** ---------------------------------------------------------------------------------------
*/


DWORD GetWindowContextHelpId(HWND) { return(WIN32_STUB(0)); }
BOOL WinHelpA(HWND, LPCSTR, UINT, ULONG_PTR) { return(WIN32_STUB(FALSE)); }

#endif	// __EMSCRIPTEN__
