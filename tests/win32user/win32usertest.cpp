/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the window manager the engine's front end is written against: the class
// registry, window creation and destruction, the parent and z-order relationships, the
// window word slots including the dialog ones, and the message queue from PostMessage
// through PeekMessage and DispatchMessage into a window procedure.
//
// The harness is written against the Win32 API rather than against the WebAssembly
// target's substitute for it, and builds on both. On Windows it establishes what the API
// actually does; on WebAssembly it holds win32user.cpp to that same account. A check that
// would pass against the substitute but not against Windows is worth nothing here.
//
// It creates every window it touches, reads no game data, and needs no display.

#if defined(__EMSCRIPTEN__)
#include "win32compat.h"
#else
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>


static int Failures = 0;
static int Checks = 0;


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
** What the window procedures under test recorded. A procedure is the only place the
** delivery of a message can be observed, so the checks read these back.
*/
static int CreateCount = 0;
static int DestroyCount = 0;
static int PingCount = 0;
static WPARAM LastPingWParam = 0;
static LPARAM LastPingLParam = 0;
static HWND LastPingWindow = NULL;

static UINT const WM_PING = WM_USER + 17;
static UINT const WM_ANSWER = WM_USER + 18;


static LRESULT CALLBACK Test_Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message) {
		case WM_CREATE:
			CreateCount++;
			return(0);

		case WM_DESTROY:
			DestroyCount++;
			return(0);

		case WM_PING:
			PingCount++;
			LastPingWindow = window;
			LastPingWParam = wparam;
			LastPingLParam = lparam;
			return(0);

		case WM_ANSWER:
			return((LRESULT)(wparam + 1));

		default:
			return(DefWindowProc(window, message, wparam, lparam));
	}
}


static LRESULT CALLBACK Second_Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_ANSWER) {
		return((LRESULT)(wparam + 100));
	}

	return(Test_Procedure(window, message, wparam, lparam));
}


static void Register_Classes(HINSTANCE instance)
{
	WNDCLASSA windowclass;
	memset(&windowclass, 0, sizeof(windowclass));
	windowclass.lpfnWndProc = Test_Procedure;
	windowclass.hInstance = instance;
	windowclass.lpszClassName = "OpenTSTestFrame";

	ATOM frame = RegisterClassA(&windowclass);
	Check("RegisterClass accepts a new class", frame != 0);

	ATOM again = RegisterClassA(&windowclass);
	Check("RegisterClass refuses a live class name", again == 0);

	/*
	** A class asking for window extra gets it, and the dialog slots below it are addressed
	** as window extra too.
	*/
	windowclass.lpszClassName = "OpenTSTestControl";
	windowclass.cbWndExtra = 64;
	Check("RegisterClass accepts a second class", RegisterClassA(&windowclass) != 0);
}


static void Check_Class_Registry(HINSTANCE instance)
{
	Check("UnregisterClass refuses a class that was never registered",
		UnregisterClassA("OpenTSTestAbsent", instance) == FALSE);

	WNDCLASSA windowclass;
	memset(&windowclass, 0, sizeof(windowclass));
	windowclass.lpfnWndProc = Test_Procedure;
	windowclass.hInstance = instance;
	windowclass.lpszClassName = "OpenTSTestTemporary";

	Check("RegisterClass accepts a temporary class", RegisterClassA(&windowclass) != 0);
	Check("UnregisterClass releases a class with no windows",
		UnregisterClassA("OpenTSTestTemporary", instance) != FALSE);
	Check("RegisterClass accepts the name again once released",
		RegisterClassA(&windowclass) != 0);
	Check("UnregisterClass releases it a second time",
		UnregisterClassA("OpenTSTestTemporary", instance) != FALSE);
}


static HWND Create_Frame(HINSTANCE instance)
{
	CreateCount = 0;

	HWND frame = CreateWindowExA(0, "OpenTSTestFrame", "Frame", WS_POPUP,
		0, 0, 640, 480, NULL, NULL, instance, NULL);

	Check("CreateWindowEx builds a top level window", frame != NULL);
	Check("CreateWindowEx sends WM_CREATE", CreateCount >= 1);
	Check("A new window is a window", IsWindow(frame) != FALSE);

	// A child is only visible while its ancestors are, so the frame is shown before the
	// checks that ask after one.
	if (frame != NULL) {
		ShowWindow(frame, SW_SHOW);
	}

	return(frame);
}


static void Check_Creation(HINSTANCE instance, HWND frame)
{
	Check("CreateWindowEx refuses an unregistered class",
		CreateWindowExA(0, "OpenTSTestAbsent", NULL, WS_POPUP, 0, 0, 10, 10,
			NULL, NULL, instance, NULL) == NULL);

	HWND control = CreateWindowExA(0, "OpenTSTestControl", "Control", WS_CHILD | WS_VISIBLE,
		10, 20, 100, 30, frame, (HMENU)(ULONG_PTR)4242, instance, NULL);

	Check("CreateWindowEx builds a child window", control != NULL);
	Check("A child window reports its parent", GetParent(control) == frame);
	Check("A child window carries the identifier its menu argument named",
		GetDlgCtrlID(control) == 4242);
	Check("GetDlgItem finds a child by identifier", GetDlgItem(frame, 4242) == control);
	Check("GetDlgItem reports nothing for an identifier no child has",
		GetDlgItem(frame, 4243) == NULL);
	Check("A child created visible is visible", IsWindowVisible(control) != FALSE);
	Check("A child created without WS_DISABLED is enabled", IsWindowEnabled(control) != FALSE);
	Check("IsChild sees the relationship", IsChild(frame, control) != FALSE);
	Check("IsChild does not see one that is not there", IsChild(control, frame) == FALSE);

	char name[64];
	Check("GetClassName reports the class the window was created from",
		GetClassNameA(control, name, sizeof(name)) > 0 && strcmp(name, "OpenTSTestControl") == 0);

	char text[64];
	Check("GetWindowText reports the title the window was created with",
		GetWindowTextA(control, text, sizeof(text)) > 0 && strcmp(text, "Control") == 0);

	Check("SetWindowText replaces it", SetWindowTextA(control, "Renamed") != FALSE);
	Check("GetWindowText reads the replacement back",
		GetWindowTextA(control, text, sizeof(text)) > 0 && strcmp(text, "Renamed") == 0);

	/*
	** A second child so that the sibling chain, which is the z-order, has something to
	** report. The most recently created window is the front one.
	*/
	HWND second = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		10, 60, 100, 30, frame, (HMENU)(ULONG_PTR)4243, instance, NULL);

	Check("CreateWindowEx builds a second child", second != NULL);
	Check("GetTopWindow reports the front child", GetTopWindow(frame) == second);
	Check("GW_HWNDNEXT walks to the one behind it", GetWindow(second, GW_HWNDNEXT) == control);
	Check("GW_HWNDPREV walks back", GetWindow(control, GW_HWNDPREV) == second);
	Check("The back child has nothing behind it", GetWindow(control, GW_HWNDNEXT) == NULL);

	Check("BringWindowToTop raises a child", BringWindowToTop(control) != FALSE);
	Check("The raised child is now in front", GetTopWindow(frame) == control);

	Check("ShowWindow reports the window was visible", ShowWindow(second, SW_HIDE) != FALSE);
	Check("A hidden window is not visible", IsWindowVisible(second) == FALSE);
	Check("ShowWindow reports the window was hidden", ShowWindow(second, SW_SHOW) == FALSE);
	Check("A shown window is visible again", IsWindowVisible(second) != FALSE);

	Check("EnableWindow reports the window was enabled", EnableWindow(second, FALSE) == FALSE);
	Check("A disabled window is disabled", IsWindowEnabled(second) == FALSE);
	Check("EnableWindow reports the window was disabled", EnableWindow(second, TRUE) != FALSE);
}


static void Check_Window_Words(HINSTANCE instance, HWND frame)
{
	HWND control = CreateWindowExA(WS_EX_CLIENTEDGE, "OpenTSTestControl", NULL,
		WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, frame, (HMENU)(ULONG_PTR)77, instance, NULL);

	Check("CreateWindowEx builds the window the word slots are read from", control != NULL);

	Check("GWL_STYLE reports the style the window was created with",
		(GetWindowLongA(control, GWL_STYLE) & WS_CHILD) != 0);
	Check("GWL_EXSTYLE reports the extended style",
		(GetWindowLongA(control, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0);
	Check("GWL_ID reports the control identifier", GetWindowLongA(control, GWL_ID) == 77);
	Check("GWL_HWNDPARENT reports the parent",
		(HWND)(ULONG_PTR)GetWindowLongA(control, GWL_HWNDPARENT) == frame);

	Check_Equal("GWL_USERDATA starts at zero", GetWindowLongA(control, GWL_USERDATA), 0);
	Check_Equal("SetWindowLong reports the previous GWL_USERDATA",
		SetWindowLongA(control, GWL_USERDATA, 0x5A5A), 0);
	Check_Equal("GWL_USERDATA reads the value back",
		GetWindowLongA(control, GWL_USERDATA), 0x5A5A);

	/*
	** The dialog slots. The engine stores the address of a dialog's result in DWL_USER on
	** windows whose class declared no window extra of its own, so the slots exist whatever
	** the class asked for.
	*/
	Check_Equal("DWL_USER starts at zero", GetWindowLongA(control, DWL_USER), 0);
	SetWindowLongA(control, DWL_USER, 0x1234);
	Check_Equal("DWL_USER reads the value back", GetWindowLongA(control, DWL_USER), 0x1234);

	SetWindowLongA(control, DWL_MSGRESULT, 0x2345);
	SetWindowLongA(control, DWL_DLGPROC, 0x3456);
	Check_Equal("DWL_MSGRESULT keeps its own value",
		GetWindowLongA(control, DWL_MSGRESULT), 0x2345);
	Check_Equal("DWL_DLGPROC keeps its own value",
		GetWindowLongA(control, DWL_DLGPROC), 0x3456);
	Check_Equal("DWL_USER is not disturbed by its neighbors",
		GetWindowLongA(control, DWL_USER), 0x1234);

	/*
	** Subclassing. The engine's controls are built by replacing the class procedure and
	** calling the displaced one for whatever they do not handle.
	*/
	Check_Equal("SendMessage reaches the class procedure",
		(long)SendMessageA(control, WM_ANSWER, 5, 0), 6);

	WNDPROC previous = (WNDPROC)(ULONG_PTR)SetWindowLongA(control, GWL_WNDPROC,
		(LONG)(ULONG_PTR)Second_Procedure);

	Check("SetWindowLong hands back the procedure it displaced", previous == Test_Procedure);
	Check_Equal("SendMessage now reaches the replacement",
		(long)SendMessageA(control, WM_ANSWER, 5, 0), 105);
	Check_Equal("CallWindowProc still reaches the displaced procedure",
		(long)CallWindowProcA(previous, control, WM_ANSWER, 5, 0), 6);

	SetWindowLongA(control, GWL_WNDPROC, (LONG)(ULONG_PTR)previous);
	Check_Equal("The displaced procedure can be put back",
		(long)SendMessageA(control, WM_ANSWER, 5, 0), 6);

	DestroyWindow(control);
}


static void Check_Messages(HINSTANCE instance, HWND frame)
{
	HWND control = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 50, 50, frame, (HMENU)(ULONG_PTR)88, instance, NULL);

	Check("CreateWindowEx builds the window the messages are sent to", control != NULL);

	/*
	** SendMessage is synchronous: the procedure has run by the time it returns.
	*/
	PingCount = 0;
	SendMessageA(control, WM_PING, 11, 22);
	Check_Equal("SendMessage reaches the procedure at once", PingCount, 1);
	Check("SendMessage carries the window", LastPingWindow == control);
	Check_Equal("SendMessage carries the wParam", (long)LastPingWParam, 11);
	Check_Equal("SendMessage carries the lParam", (long)LastPingLParam, 22);

	Check_Equal("SendMessage reports what the procedure returned",
		(long)SendMessageA(control, WM_ANSWER, 41, 0), 42);

	/*
	** A posted message waits on the queue until something pumps it.
	*/
	PingCount = 0;
	Check("PostMessage accepts the message",
		PostMessageA(control, WM_PING, 33, 44) != FALSE);
	Check_Equal("A posted message has not reached the procedure yet", PingCount, 0);

	MSG message;
	memset(&message, 0, sizeof(message));

	Check("PeekMessage finds the posted message",
		PeekMessageA(&message, control, 0, 0, PM_NOREMOVE) != FALSE);
	Check_Equal("PeekMessage reports the message", (long)message.message, (long)WM_PING);
	Check_Equal("A message left on the queue is still there",
		PeekMessageA(&message, control, 0, 0, PM_NOREMOVE) != FALSE, 1);

	Check("PeekMessage takes the message off the queue",
		PeekMessageA(&message, control, 0, 0, PM_REMOVE) != FALSE);
	Check_Equal("PeekMessage reports the wParam", (long)message.wParam, 33);
	Check_Equal("PeekMessage reports the lParam", (long)message.lParam, 44);
	Check("PeekMessage carries the window the message was posted to",
		message.hwnd == control);

	Check_Equal("The message was taken off the queue and not copied", PingCount, 0);

	Check("DispatchMessage reaches the procedure",
		DispatchMessageA(&message) == 0);
	Check_Equal("DispatchMessage delivered it once", PingCount, 1);

	/*
	** A filter that does not name this message leaves it alone, and one that does finds it.
	*/
	PostMessageA(control, WM_PING, 1, 0);
	Check("A range filter that excludes the message finds nothing",
		PeekMessageA(&message, control, WM_PING + 1, WM_PING + 2, PM_REMOVE) == FALSE);
	Check("A range filter that includes the message finds it",
		PeekMessageA(&message, control, WM_PING, WM_PING, PM_REMOVE) != FALSE);

	PostMessageA(control, WM_PING, 2, 0);
	Check("A window filter naming another window finds nothing",
		PeekMessageA(&message, frame, 0, 0, PM_REMOVE) == FALSE);
	Check("A window filter naming this window finds it",
		PeekMessageA(&message, control, 0, 0, PM_REMOVE) != FALSE);

	Check("The queue is empty once the messages are taken",
		PeekMessageA(&message, NULL, 0, 0, PM_REMOVE) == FALSE);

	DestroyWindow(control);
}


static void Check_Focus(HINSTANCE instance, HWND frame)
{
	HWND first = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 50, 50, frame, (HMENU)(ULONG_PTR)91, instance, NULL);
	HWND second = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		0, 60, 50, 50, frame, (HMENU)(ULONG_PTR)92, instance, NULL);

	Check("CreateWindowEx builds the windows the focus moves between",
		first != NULL && second != NULL);

	SetFocus(first);
	Check("SetFocus gives the focus to the window named", GetFocus() == first);

	Check("SetFocus hands back the window that had it", SetFocus(second) == first);
	Check("The focus moved", GetFocus() == second);

	DestroyWindow(second);
	Check("Destroying the focus window leaves nothing focused", GetFocus() != second);

	DestroyWindow(first);
}


static void Check_Destruction(HINSTANCE instance, HWND frame)
{
	HWND parent = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 200, 200, frame, (HMENU)(ULONG_PTR)55, instance, NULL);
	HWND child = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 50, 50, parent, (HMENU)(ULONG_PTR)56, instance, NULL);

	Check("CreateWindowEx builds a window with a child of its own",
		parent != NULL && child != NULL);
	Check("GetDlgItem reaches a grandchild", GetDlgItem(frame, 56) == child);

	DestroyCount = 0;
	Check("DestroyWindow accepts a live window", DestroyWindow(parent) != FALSE);
	Check("DestroyWindow sends WM_DESTROY to the window and its child", DestroyCount >= 2);
	Check("A destroyed window is no longer a window", IsWindow(parent) == FALSE);
	Check("Its child went with it", IsWindow(child) == FALSE);
	Check("It is gone from its parent's children", GetDlgItem(frame, 55) == NULL);

	Check("DestroyWindow refuses a handle that names nothing",
		DestroyWindow(NULL) == FALSE);
}


int main(void)
{
	HINSTANCE instance = (HINSTANCE)(ULONG_PTR)0x400000;

	Register_Classes(instance);
	Check_Class_Registry(instance);

	HWND frame = Create_Frame(instance);
	if (frame == NULL) {
		printf("win32user: no frame window; %d of %d checks ran\n", Checks - Failures, Checks);
		return(1);
	}

	Check_Creation(instance, frame);
	Check_Window_Words(instance, frame);
	Check_Messages(instance, frame);
	Check_Focus(instance, frame);
	Check_Destruction(instance, frame);

	Check("A class with live windows cannot be unregistered",
		UnregisterClassA("OpenTSTestFrame", instance) == FALSE);

	DestroyWindow(frame);

	printf("win32user: %d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
