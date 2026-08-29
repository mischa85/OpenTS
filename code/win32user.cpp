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
#include "dbgprint.h"
#include "keyboard.h"
#include "vidscale.h"

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
	LONG UserData;
	std::vector<LONG> Extra;
	bool Visible;
	bool Enabled;
	bool NeedsPaint;
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


static std::vector<UserClass *> _Classes;
static std::vector<UserWindow *> _Windows;
static std::vector<UserWindow *> _TopLevel;
static std::vector<UserHotKey> _HotKeys;
static std::deque<MSG> _Queue;

static UserWindow * _Focus = nullptr;
static UserWindow * _Active = nullptr;
static UserWindow * _Foreground = nullptr;

static ATOM _NextAtom = 0xC000;

static bool _QueueReported = false;

/*
** What the page last reported, so that a change in it can be turned into a message. The
** position is in window pixels, which is what a mouse message carries.
*/
static POINT _LastMouse = { -1, -1 };
static unsigned char _LastKeyDown[256];
static bool _InputStarted = false;


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
	window->UpdateRect.left = 0;
	window->UpdateRect.top = 0;
	window->UpdateRect.right = width;
	window->UpdateRect.bottom = height;
	window->Parent = ((style & WS_CHILD) != 0) ? parentwindow : nullptr;

	int extrabytes = windowclass->WindowExtra;
	if (extrabytes < MINIMUM_WINDOW_EXTRA) {
		extrabytes = MINIMUM_WINDOW_EXTRA;
	}
	window->Extra.assign((size_t)((extrabytes + 3) / 4), 0);

	_Windows.push_back(window);

	if (window->Parent != nullptr) {
		window->Parent->Children.insert(window->Parent->Children.begin(), window);
	} else {
		_TopLevel.insert(_TopLevel.begin(), window);
	}

	HWND handle = Handle_Of(window);

	if (param != nullptr) {
		/*
		 * Windows delivers the creation parameter inside a CREATESTRUCT, and win32compat.h
		 * has no such structure to build one in. A procedure that reads it therefore gets
		 * nothing, which is worth saying out loud rather than passing a pointer of another
		 * shape.
		 */
		WIN32_UNSUPPORTED("CreateWindowEx: the creation parameter has no CREATESTRUCT to travel in", 0);
	}

	if (SendMessageA(handle, WM_CREATE, 0, 0) == (LRESULT)-1) {
		DestroyWindow(handle);
		return(nullptr);
	}

	if (window->Visible) {
		SendMessageA(handle, WM_SHOWWINDOW, TRUE, 0);
	}

	return(handle);
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

	SendMessageA(handle, WM_DESTROY, 0, 0);

	while (!window->Children.empty()) {
		Destroy_Window(window->Children[0]);
	}

	SendMessageA(handle, WM_NCDESTROY, 0, 0);

	if (_Focus == window) _Focus = nullptr;
	if (_Active == window) _Active = nullptr;
	if (_Foreground == window) _Foreground = nullptr;

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
		entry->Visible = visible;
		if (visible) {
			entry->Style |= WS_VISIBLE;
			InvalidateRect(window, nullptr, TRUE);
		} else {
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


LONG GetWindowLongA(HWND window, int index)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	switch (index) {
		case GWL_WNDPROC:	return((LONG)(ULONG_PTR)entry->Procedure);
		case GWL_HINSTANCE:	return((LONG)(ULONG_PTR)entry->Instance);
		case GWL_HWNDPARENT: return((LONG)(ULONG_PTR)(entry->Parent != nullptr ? Handle_Of(entry->Parent) : nullptr));
		case GWL_STYLE:		return((LONG)entry->Style);
		case GWL_EXSTYLE:	return((LONG)entry->ExStyle);
		case GWL_USERDATA:	return(entry->UserData);
		case GWL_ID:		return((LONG)entry->ID);
		default:			break;
	}

	if (index < 0 || (index % 4) != 0 || (unsigned int)(index / 4) >= entry->Extra.size()) {
		return(0);
	}

	return(entry->Extra[(unsigned int)(index / 4)]);
}


LONG SetWindowLongA(HWND window, int index, LONG value)
{
	UserWindow * entry = Window_Of(window);
	if (entry == nullptr) {
		return(0);
	}

	LONG previous = GetWindowLongA(window, index);

	switch (index) {
		case GWL_WNDPROC:
			entry->Procedure = (WNDPROC)(ULONG_PTR)value;
			return(previous);

		case GWL_HINSTANCE:
			entry->Instance = (HINSTANCE)(ULONG_PTR)value;
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

	if (index < 0 || (index % 4) != 0 || (unsigned int)(index / 4) >= entry->Extra.size()) {
		return(0);
	}

	entry->Extra[(unsigned int)(index / 4)] = value;
	return(previous);
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

	if (!entry->NeedsPaint || !entry->Visible) {
		return(TRUE);
	}

	// Cleared first so that a procedure which paints by calling back here does not recur.
	entry->NeedsPaint = false;
	SetRectEmpty(&entry->UpdateRect);

	SendMessageA(window, WM_PAINT, 0, 0);
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

#endif	// __EMSCRIPTEN__
