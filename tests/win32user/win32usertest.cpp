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


// The style that makes a combo box a list with no edit field. win32compat.h has no need
// for it, and windows.h supplies it on the other target.
#ifndef CBS_DROPDOWNLIST
#define CBS_DROPDOWNLIST	0x0003L
#endif


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


/*
** What the creation messages carried, recorded by Creation_Procedure. The engine's own
** drop-down reads the creation parameter as the first word of the structure, so the layout
** is checked through the same reading as well as through the member.
*/
static int NcCreateCount = 0;
static int CreateStructCount = 0;
static void * SeenCreateParams = NULL;
static void * SeenFirstWord = NULL;
static CREATESTRUCTA SeenCreate;

// What the procedures under test answer the creation messages with.
static LRESULT NcCreateAnswer = TRUE;
static LRESULT CreateAnswer = 0;


static LRESULT CALLBACK Creation_Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message) {
		case WM_NCCREATE:
			NcCreateCount++;
			SeenFirstWord = *(void **)lparam;
			return(NcCreateAnswer);

		case WM_CREATE:
			CreateStructCount++;
			SeenCreate = *(CREATESTRUCTA *)lparam;
			SeenCreateParams = ((CREATESTRUCTA *)lparam)->lpCreateParams;
			return(CreateAnswer);

		default:
			return(DefWindowProc(window, message, wparam, lparam));
	}
}


static void Check_Creation_Parameter(HINSTANCE instance, HWND frame)
{
	WNDCLASSA windowclass;
	memset(&windowclass, 0, sizeof(windowclass));
	windowclass.lpfnWndProc = Creation_Procedure;
	windowclass.hInstance = instance;
	windowclass.lpszClassName = "OpenTSTestCreation";

	if (RegisterClassA(&windowclass) == 0) {
		Check("RegisterClass accepts the creation class", false);
		return;
	}

	int marker = 0;
	void * param = &marker;

	NcCreateCount = 0;
	CreateStructCount = 0;
	SeenCreateParams = NULL;
	SeenFirstWord = NULL;
	NcCreateAnswer = TRUE;
	CreateAnswer = 0;

	HWND window = CreateWindowExA(WS_EX_TOPMOST, "OpenTSTestCreation", "Creation",
		WS_CHILD | WS_VISIBLE, 11, 22, 33, 44, frame, (HMENU)(ULONG_PTR)77, instance, param);

	Check("CreateWindowEx builds the window whose creation is under test", window != NULL);
	Check_Equal("CreateWindowEx sends WM_NCCREATE once", NcCreateCount, 1);
	Check_Equal("CreateWindowEx sends WM_CREATE once", CreateStructCount, 1);
	Check("WM_CREATE carries the creation parameter", SeenCreateParams == param);
	Check("The creation parameter is the structure's first word", SeenFirstWord == param);
	Check("The structure describes the window created",
		SeenCreate.hwndParent == frame && SeenCreate.hInstance == instance &&
		SeenCreate.hMenu == (HMENU)(ULONG_PTR)77);
	Check("The structure carries the position and size asked for",
		SeenCreate.x == 11 && SeenCreate.y == 22 && SeenCreate.cx == 33 && SeenCreate.cy == 44);
	Check("The structure carries the styles asked for",
		(SeenCreate.style & WS_CHILD) != 0 && SeenCreate.dwExStyle == WS_EX_TOPMOST);
	Check("The structure names the window",
		SeenCreate.lpszName != NULL && strcmp(SeenCreate.lpszName, "Creation") == 0);

	if (window != NULL) {
		DestroyWindow(window);
	}

	/*
	** A procedure refusing the window stops it existing, and the refusals are spelled
	** differently: FALSE from WM_NCCREATE, which is refused before WM_CREATE is ever sent,
	** and -1 from WM_CREATE.
	*/
	NcCreateCount = 0;
	CreateStructCount = 0;
	NcCreateAnswer = FALSE;

	HWND refused = CreateWindowExA(0, "OpenTSTestCreation", NULL, WS_CHILD,
		0, 0, 10, 10, frame, NULL, instance, param);

	Check("WM_NCCREATE returning FALSE stops the window being created", refused == NULL);
	Check_Equal("A window refused at WM_NCCREATE is never sent WM_CREATE", CreateStructCount, 0);

	NcCreateAnswer = TRUE;
	CreateAnswer = -1;

	refused = CreateWindowExA(0, "OpenTSTestCreation", NULL, WS_CHILD,
		0, 0, 10, 10, frame, NULL, instance, param);

	Check("WM_CREATE returning -1 stops the window being created", refused == NULL);

	CreateAnswer = 0;
	UnregisterClassA("OpenTSTestCreation", instance);
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


/// <summary>
/// Paints out every window that is waiting for one.
/// </summary>
/// <remarks>
/// WM_PAINT is generated rather than posted: a visible window with an invalid region has
/// one waiting whatever else is on the queue, and it goes on waiting until the window
/// validates itself. So the checks that ask what is on the queue drain the paints first,
/// leaving the queue holding only what was posted to it.
/// </remarks>
static void Drain_Paints(void)
{
	MSG message;

	for (int guard = 0; guard < 64; guard++) {
		if (!PeekMessageA(&message, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) {
			return;
		}

		DispatchMessageA(&message);
	}
}


static void Check_Messages(HINSTANCE instance, HWND frame)
{
	HWND control = CreateWindowExA(0, "OpenTSTestControl", NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 50, 50, frame, (HMENU)(ULONG_PTR)88, instance, NULL);

	Check("CreateWindowEx builds the window the messages are sent to", control != NULL);

	Drain_Paints();

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

	Drain_Paints();

	PostMessageA(control, WM_PING, 2, 0);
	Check("A window filter naming another window finds nothing",
		PeekMessageA(&message, frame, 0, 0, PM_REMOVE) == FALSE);
	Check("A window filter naming this window finds it",
		PeekMessageA(&message, control, 0, 0, PM_REMOVE) != FALSE);

	Drain_Paints();

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



/*
** ---------------------------------------------------------------------------------------
** Dialog templates.
** ---------------------------------------------------------------------------------------
**
** A template is a byte stream rather than a structure, so the checks build one here and
** read the windows back out of the dialog it produces. Every field the shipped templates
** actually use is exercised: both template forms, an ordinal class name and a string one,
** the four byte alignment before each item, and the creation data block an item may carry.
*/

static int InitDialogCount = 0;
static HWND LastInitDialog = NULL;


static INT_PTR CALLBACK Test_Dialog_Procedure(HWND window, UINT message, WPARAM, LPARAM)
{
	if (message == WM_INITDIALOG) {
		InitDialogCount++;
		LastInitDialog = window;
		return(TRUE);
	}

	return(FALSE);
}


/*
** A template under construction. Windows takes the whole thing by pointer, so the bytes
** are laid out here in the order it reads them.
*/
struct TemplateBuilder
{
	unsigned char Bytes[512];
	unsigned int Length;

	TemplateBuilder(void) : Length(0) { memset(Bytes, 0, sizeof(Bytes)); }

	void Put_Word(unsigned short value)
	{
		Bytes[Length++] = (unsigned char)(value & 0xFF);
		Bytes[Length++] = (unsigned char)(value >> 8);
	}

	void Put_Long(unsigned long value)
	{
		Put_Word((unsigned short)(value & 0xFFFF));
		Put_Word((unsigned short)(value >> 16));
	}

	void Put_Text(char const * text)
	{
		for (char const * scan = text; *scan != '\0'; scan++) {
			Put_Word((unsigned short)(unsigned char)*scan);
		}
		Put_Word(0);
	}

	void Put_Ordinal(unsigned short ordinal)
	{
		Put_Word(0xFFFF);
		Put_Word(ordinal);
	}

	void Align(void)
	{
		while ((Length & 3) != 0) {
			Bytes[Length++] = 0;
		}
	}
};


/// <summary>
/// Lays out a classic template: a dialog with a button named by ordinal, a static named by
/// string, and a creation data block between them.
/// </summary>
static void Build_Classic_Template(TemplateBuilder & builder)
{
	builder.Put_Long(WS_CHILD);			// style, with no DS_SETFONT and no WS_VISIBLE
	builder.Put_Long(0);				// extended style
	builder.Put_Word(2);				// item count
	builder.Put_Word(0);				// x
	builder.Put_Word(0);				// y
	builder.Put_Word(100);				// width, in dialog units
	builder.Put_Word(80);				// height, in dialog units
	builder.Put_Word(0);				// no menu
	builder.Put_Word(0);				// no class of its own
	builder.Put_Text("Harness");		// title

	builder.Align();
	builder.Put_Long(WS_CHILD | WS_VISIBLE | WS_TABSTOP);
	builder.Put_Long(0);
	builder.Put_Word(4);				// x
	builder.Put_Word(6);				// y
	builder.Put_Word(40);				// width
	builder.Put_Word(14);				// height
	builder.Put_Word(1001);				// identifier
	builder.Put_Ordinal(0x0080);		// the button class, as a template names it
	builder.Put_Text("Press");
	builder.Put_Word(6);				// creation data: this word plus four bytes
	builder.Put_Long(0xDEADBEEF);

	builder.Align();
	builder.Put_Long(WS_CHILD | WS_VISIBLE);
	builder.Put_Long(0);
	builder.Put_Word(4);
	builder.Put_Word(30);
	builder.Put_Word(60);
	builder.Put_Word(10);
	builder.Put_Word(1002);
	builder.Put_Text("Static");			// the same class, named as a string
	builder.Put_Text("Caption");
	builder.Put_Word(0);				// no creation data
}


/// <summary>
/// Lays out an extended template, which the shipped resources also carry: a different
/// header, a help identifier before each item, and a full double word identifier.
/// </summary>
static void Build_Extended_Template(TemplateBuilder & builder)
{
	builder.Put_Word(1);				// dialog version
	builder.Put_Word(0xFFFF);			// the signature that tells the two forms apart
	builder.Put_Long(0);				// help identifier
	builder.Put_Long(0);				// extended style
	builder.Put_Long(WS_CHILD);			// style
	builder.Put_Word(1);				// item count
	builder.Put_Word(0);
	builder.Put_Word(0);
	builder.Put_Word(100);
	builder.Put_Word(80);
	builder.Put_Word(0);				// no menu
	builder.Put_Word(0);				// no class of its own
	builder.Put_Text("Extended");

	builder.Align();
	builder.Put_Long(0);				// help identifier
	builder.Put_Long(0);				// extended style
	builder.Put_Long(WS_CHILD | WS_VISIBLE);
	builder.Put_Word(8);
	builder.Put_Word(8);
	builder.Put_Word(50);
	builder.Put_Word(12);
	builder.Put_Long(2001);				// identifier, a double word in this form
	builder.Put_Ordinal(0x0082);		// the static class
	builder.Put_Text("Extended caption");
	builder.Put_Word(0);
}


static bool Class_Name_Is(HWND window, char const * expected)
{
	char name[64];
	name[0] = '\0';
	GetClassNameA(window, name, sizeof(name));
	return(strcmp(name, expected) == 0);
}


static void Check_Dialog_Templates(HINSTANCE instance, HWND frame)
{
	TemplateBuilder classic;
	Build_Classic_Template(classic);

	InitDialogCount = 0;
	LastInitDialog = NULL;

	HWND dialog = CreateDialogIndirectParamA(instance, (LPCDLGTEMPLATE)classic.Bytes, frame,
		Test_Dialog_Procedure, 0);

	Check("CreateDialogIndirectParam builds a dialog from a classic template", dialog != NULL);
	if (dialog == NULL) {
		return;
	}

	Check_Equal("The dialog procedure was told the dialog was starting", InitDialogCount, 1);
	Check("WM_INITDIALOG named the dialog itself", LastInitDialog == dialog);

	HWND button = GetDlgItem(dialog, 1001);
	HWND caption = GetDlgItem(dialog, 1002);

	Check("The template's first control was created", button != NULL);
	Check("The creation data block did not swallow the second control", caption != NULL);

	if (button != NULL && caption != NULL) {
		Check("An ordinal class name resolves to the button class", Class_Name_Is(button, "Button"));
		Check("A string class name resolves as written", Class_Name_Is(caption, "Static"));

		char text[64];
		text[0] = '\0';
		GetWindowTextA(caption, text, sizeof(text));
		Check("A control's caption comes from the template", strcmp(text, "Caption") == 0);

		Check_Equal("GetDlgCtrlID reports the identifier the template gave", GetDlgCtrlID(button), 1001);
		Check("The controls belong to the dialog", GetParent(button) == dialog);

		/*
		** Dialog units are not pixels. A template measures in quarters of the base
		** character width and eighths of its height, and the dialog manager converts.
		*/
		DWORD units = GetDialogBaseUnits();
		RECT client;
		GetClientRect(button, &client);
		Check_Equal("A control's width is its dialog units converted",
			client.right - client.left, 40 * (long)LOWORD(units) / 4);
		Check_Equal("A control's height is its dialog units converted",
			client.bottom - client.top, 14 * (long)HIWORD(units) / 8);
	}

	Check("A template without WS_VISIBLE leaves the dialog hidden", IsWindowVisible(dialog) == FALSE);

	DestroyWindow(dialog);
	Check("The dialog's controls went with it", IsWindow(button) == FALSE);

	TemplateBuilder extended;
	Build_Extended_Template(extended);

	InitDialogCount = 0;

	HWND second = CreateDialogIndirectParamA(instance, (LPCDLGTEMPLATE)extended.Bytes, frame,
		Test_Dialog_Procedure, 0);

	Check("CreateDialogIndirectParam builds a dialog from an extended template", second != NULL);
	if (second != NULL) {
		Check_Equal("The extended dialog started", InitDialogCount, 1);
		HWND item = GetDlgItem(second, 2001);
		Check("An extended template's control carries its full identifier", item != NULL);
		if (item != NULL) {
			Check("An extended template names its classes the same way",
				Class_Name_Is(item, "Static"));
		}
		DestroyWindow(second);
	}

	Check("CreateDialogIndirectParam refuses a template that is not there",
		CreateDialogIndirectParamA(instance, NULL, frame, Test_Dialog_Procedure, 0) == NULL);
}



/*
** ---------------------------------------------------------------------------------------
** Stock controls.
** ---------------------------------------------------------------------------------------
**
** ownrdraw.cpp paints the front end itself but keeps asking the stock controls what to
** paint, so what matters about them is the state they carry rather than what they look
** like. Only the classes USER32 provides on its own are checked here, so that the harness
** needs no common control library on Windows.
*/

static void Check_Stock_Controls(HINSTANCE instance, HWND frame)
{
	HWND check = CreateWindowExA(0, "Button", "Toggle", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
		0, 0, 80, 20, frame, (HMENU)(ULONG_PTR)3001, instance, NULL);

	Check("A check box is a window", check != NULL);
	if (check != NULL) {
		Check_Equal("A new check box is clear", (long)SendMessageA(check, BM_GETCHECK, 0, 0), BST_UNCHECKED);
		SendMessageA(check, BM_SETCHECK, BST_CHECKED, 0);
		Check_Equal("BM_SETCHECK is read back by BM_GETCHECK",
			(long)SendMessageA(check, BM_GETCHECK, 0, 0), BST_CHECKED);

		CheckDlgButton(frame, 3001, BST_UNCHECKED);
		Check_Equal("CheckDlgButton reaches the control through the dialog",
			(long)IsDlgButtonChecked(frame, 3001), BST_UNCHECKED);

		DestroyWindow(check);
	}

	HWND edit = CreateWindowExA(0, "Edit", "", WS_CHILD | WS_VISIBLE,
		0, 0, 80, 20, frame, (HMENU)(ULONG_PTR)3002, instance, NULL);

	Check("An edit box is a window", edit != NULL);
	if (edit != NULL) {
		SetWindowTextA(edit, "abcd");

		char text[32];
		text[0] = '\0';
		GetWindowTextA(edit, text, sizeof(text));
		Check("An edit box keeps the text it was given", strcmp(text, "abcd") == 0);

		SendMessageA(edit, EM_SETSEL, 1, 3);

		DWORD start = 0;
		DWORD end = 0;
		SendMessageA(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
		Check("EM_GETSEL reports the selection EM_SETSEL made", start == 1 && end == 3);

		SendMessageA(edit, EM_REPLACESEL, 0, (LPARAM)"XY");
		text[0] = '\0';
		GetWindowTextA(edit, text, sizeof(text));
		Check("EM_REPLACESEL replaces the selected run", strcmp(text, "aXYd") == 0);

		DestroyWindow(edit);
	}

	HWND list = CreateWindowExA(0, "ListBox", NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY,
		0, 0, 120, 80, frame, (HMENU)(ULONG_PTR)3003, instance, NULL);

	Check("A list box is a window", list != NULL);
	if (list != NULL) {
		Check_Equal("A new list box is empty", (long)SendMessageA(list, LB_GETCOUNT, 0, 0), 0);
		Check_Equal("LB_ADDSTRING reports where the row landed",
			(long)SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)"alpha"), 0);
		Check_Equal("A second row lands after the first",
			(long)SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)"beta"), 1);
		Check_Equal("LB_INSERTSTRING puts a row where it is told",
			(long)SendMessageA(list, LB_INSERTSTRING, 1, (LPARAM)"middle"), 1);
		Check_Equal("The rows are all there", (long)SendMessageA(list, LB_GETCOUNT, 0, 0), 3);

		char row[64];
		row[0] = '\0';
		Check_Equal("LB_GETTEXT reports the length it wrote",
			(long)SendMessageA(list, LB_GETTEXT, 1, (LPARAM)row), 6);
		Check("LB_GETTEXT reads a row back", strcmp(row, "middle") == 0);

		SendMessageA(list, LB_SETITEMDATA, 1, 4242);
		Check_Equal("LB_GETITEMDATA reads back what LB_SETITEMDATA stored",
			(long)SendMessageA(list, LB_GETITEMDATA, 1, 0), 4242);

		SendMessageA(list, LB_SETCURSEL, 2, 0);
		Check_Equal("LB_GETCURSEL reports the selection",
			(long)SendMessageA(list, LB_GETCURSEL, 0, 0), 2);
		Check_Equal("The selected row reads as selected",
			(long)SendMessageA(list, LB_GETSEL, 2, 0) != 0, 1);

		Check_Equal("LB_DELETESTRING reports what is left",
			(long)SendMessageA(list, LB_DELETESTRING, 0, 0), 2);
		row[0] = '\0';
		SendMessageA(list, LB_GETTEXT, 0, (LPARAM)row);
		Check("The rows below a deleted one moved up", strcmp(row, "middle") == 0);

		SendMessageA(list, LB_RESETCONTENT, 0, 0);
		Check_Equal("LB_RESETCONTENT empties the list",
			(long)SendMessageA(list, LB_GETCOUNT, 0, 0), 0);
		Check_Equal("An empty list has no selection",
			(long)SendMessageA(list, LB_GETCURSEL, 0, 0), LB_ERR);

		DestroyWindow(list);
	}

	HWND combo = CreateWindowExA(0, "ComboBox", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
		0, 0, 120, 80, frame, (HMENU)(ULONG_PTR)3004, instance, NULL);

	Check("A combo box is a window", combo != NULL);
	if (combo != NULL) {
		Check_Equal("CB_ADDSTRING reports where the item landed",
			(long)SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"one"), 0);
		SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"two");
		Check_Equal("The items are all there", (long)SendMessageA(combo, CB_GETCOUNT, 0, 0), 2);
		Check_Equal("A new combo box has no selection",
			(long)SendMessageA(combo, CB_GETCURSEL, 0, 0), CB_ERR);

		SendMessageA(combo, CB_SETCURSEL, 1, 0);
		Check_Equal("CB_GETCURSEL reports the selection",
			(long)SendMessageA(combo, CB_GETCURSEL, 0, 0), 1);

		char item[64];
		item[0] = '\0';
		SendMessageA(combo, CB_GETLBTEXT, 1, (LPARAM)item);
		Check("CB_GETLBTEXT reads an item back", strcmp(item, "two") == 0);

		Check_Equal("CB_FINDSTRING finds an item by its beginning",
			(long)SendMessageA(combo, CB_FINDSTRING, (WPARAM)-1, (LPARAM)"on"), 0);

		SendMessageA(combo, CB_RESETCONTENT, 0, 0);
		Check_Equal("CB_RESETCONTENT empties the list",
			(long)SendMessageA(combo, CB_GETCOUNT, 0, 0), 0);

		DestroyWindow(combo);
	}

	Drain_Paints();
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
	Check_Creation_Parameter(instance, frame);
	Check_Window_Words(instance, frame);
	Check_Messages(instance, frame);
	Check_Focus(instance, frame);
	Check_Destruction(instance, frame);
	Check_Dialog_Templates(instance, frame);
	Check_Stock_Controls(instance, frame);

	Check("A class with live windows cannot be unregistered",
		UnregisterClassA("OpenTSTestFrame", instance) == FALSE);

	DestroyWindow(frame);

	printf("win32user: %d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
