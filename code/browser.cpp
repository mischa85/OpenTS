/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The browser side of the platform seam. browser.h states what this stands in for; what
// follows is why it is shaped the way it is.
//
// A tab runs the page on the same thread that services input, layout, and paint, so a
// function that does not return starves all three. The engine has roughly sixty waits
// that do not return, and docs/WASM-PORT.md Part A is explicit that flattening them all
// before anything runs is the wrong order to work in. So every wait the engine already
// has reaches Windows_Message_Handler or Call_Back, and both of those come here, and here
// the thread is handed back.
//
// How it is handed back is the scaffold, and the scaffold is temporary. With
// OPENTS_WASM_JSPI the yield suspends this stack against an animation frame and resumes it
// where it left off, so the engine's own loops work unaltered. Without it the yield only
// counts, and a build that reaches a wait freezes -- which is the honest state of a port
// whose waits are not flattened yet. Browser_Blocking_Wait_Count is the number that has to
// reach zero before the option can go.

#include "always.h"

#include "browser.h"

#if defined(__EMSCRIPTEN__)

#include "_keyboar.h"
#include "dbgprint.h"
#include "keyboard.h"
#include "misc.h"
#include "video.h"
#include "vidscale.h"
#include "win.h"
#include "wwmouse.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cstring>


// The page lays the canvas out under this id. The renderer names its drawing surface with
// a CSS selector and the event callbacks name their target the same way, so one spelling
// serves both.
static char const * const CANVAS_SELECTOR = "#canvas";

// The cadence Browser_Yield_If_Due paces itself to. It is not the game's frame rate, which
// the engine's own timers decide; it is the shortest useful gap between two returns to the
// page.
static const double YIELD_INTERVAL = 1000.0 / 60.0;

// The engine records mouse events as a key plus a position, so a burst that arrives while
// the engine is away is queued here and drained in engine context rather than pushed into
// the keyboard buffer from a JavaScript callback part way through a read of it.
static const int EVENT_QUEUE_SIZE = 128;

struct BrowserEvent
{
	unsigned short Key;
	short X;
	short Y;
	bool IsMouse;
	bool IsRelease;
};

static BrowserEvent _Events[EVENT_QUEUE_SIZE];
static int _EventHead = 0;
static int _EventTail = 0;

static BrowserEventHook _EventHook = nullptr;

static bool _Initialized = false;

// Only main is a promising export, so a suspend is only legal once the engine is inside it.
// Static initialization runs before that, and anything it reaches must return normally.
static bool _EngineEntered = false;

static int _CanvasWidth = 0;
static int _CanvasHeight = 0;
static int _CanvasCSSWidth = 0;
static int _CanvasCSSHeight = 0;

static int _ScreenWidth = 0;
static int _ScreenHeight = 0;

static BrowserDisplayPolicy _DisplayPolicy = BROWSER_DISPLAY_NATIVE;
static int _DisplayWidth = 0;
static int _DisplayHeight = 0;

static bool _Hidden = false;

static int _MouseX = 0;
static int _MouseY = 0;

/*
 * Has the pointer left the canvas? A full screen game's cursor cannot, so the engine reads
 * a pointer held against an edge as a standing request to keep scrolling the map. In a page
 * the pointer walks off the canvas and the events simply stop, which killed the scroll part
 * way through a pan. The position is therefore left pinned to the edge it left through until
 * it comes back or the page loses the keyboard, which is what the engine's own model expects.
 */
static bool _MouseOutside = false;

static unsigned short _Modifiers = 0;
static unsigned char _KeyDown[256];

// What the browser said the key produces, kept per virtual key and per shift state. The
// page reports a character with the event it belongs to, which is the only place the
// keyboard layout is known, so To_ASCII reads back what was recorded when the key went
// down rather than asking a translation table that a browser does not have.
static char _Ascii[256];
static char _ShiftedAscii[256];

static double _LastYield = 0.0;
static unsigned int _FrameSerial = 0;
static unsigned int _BlockingWaits = 0;

#if defined(OPENTS_WASM_JSPI)

// Handing the thread back is an await, and awaiting from the middle of the engine's own
// call stack is what the scaffold buys. A hidden tab is not given animation frames at all,
// so it falls back to a timer: a lockstep game whose peer simply stops is a game everyone
// else waits on, and stopping is the one thing this must not do.
EM_ASYNC_JS(void, Browser_Await_Frame, (int hidden), {
	if (hidden || typeof requestAnimationFrame !== "function") {
		await new Promise(function (resolve) { setTimeout(resolve, 0); });
	} else {
		await new Promise(function (resolve) { requestAnimationFrame(resolve); });
	}
});

#endif


/// <summary>
/// Names the canvas the game is drawn onto.
/// </summary>
char const * Browser_Canvas_Selector(void)
{
	return(CANVAS_SELECTOR);
}


/// <summary>
/// Reads back how the page was asked to size the game's frame, and how big the display it
/// is on is.
/// </summary>
static void Read_Page_Configuration(void)
{
	int scaled = EM_ASM_INT({
		var value = (new URLSearchParams(location.search).get("display") || "").toLowerCase();
		return (value === "" || value === "native" || value === "window") ? 0 : 1;
	});

	_DisplayPolicy = (scaled != 0) ? BROWSER_DISPLAY_SCALED : BROWSER_DISPLAY_NATIVE;

	_DisplayWidth = EM_ASM_INT({
		var parts = /^(\d+)x(\d+)$/.exec((new URLSearchParams(location.search).get("display") || "").toLowerCase());
		return parts ? parseInt(parts[1], 10) : 0;
	});

	_DisplayHeight = EM_ASM_INT({
		var parts = /^(\d+)x(\d+)$/.exec((new URLSearchParams(location.search).get("display") || "").toLowerCase());
		return parts ? parseInt(parts[2], 10) : 0;
	});

	_ScreenWidth = EM_ASM_INT({ return (window.screen && window.screen.width) ? (window.screen.width | 0) : 0; });
	_ScreenHeight = EM_ASM_INT({ return (window.screen && window.screen.height) ? (window.screen.height | 0) : 0; });
}


/// <summary>
/// Measures the canvas as the page has laid it out.
/// </summary>
/// <remarks>
/// The page resizes for reasons the engine never sees, so the size is read back rather than
/// tracked. The drawing buffer is matched to the laid out box in device pixels, which is
/// what keeps a browser frame from being composited through a second, softening rescale.
/// </remarks>
/// <returns>bool; Did either measurement change?</returns>
static bool Measure_Canvas(void)
{
	double csswidth = 0.0;
	double cssheight = 0.0;

	if (emscripten_get_element_css_size(CANVAS_SELECTOR, &csswidth, &cssheight) != EMSCRIPTEN_RESULT_SUCCESS) {
		return(false);
	}

	double ratio = emscripten_get_device_pixel_ratio();

	int cssw = (int)(csswidth + 0.5);
	int cssh = (int)(cssheight + 0.5);
	int width = (int)(csswidth * ratio + 0.5);
	int height = (int)(cssheight * ratio + 0.5);

	if (cssw <= 0 || cssh <= 0 || width <= 0 || height <= 0) {
		return(false);
	}

	if (width == _CanvasWidth && height == _CanvasHeight && cssw == _CanvasCSSWidth && cssh == _CanvasCSSHeight) {
		return(false);
	}

	_CanvasWidth = width;
	_CanvasHeight = height;
	_CanvasCSSWidth = cssw;
	_CanvasCSSHeight = cssh;

	emscripten_set_canvas_element_size(CANVAS_SELECTOR, width, height);
	return(true);
}


/// <summary>
/// Adds an event to the queue the engine drains, dropping it when the queue is full.
/// </summary>
/// <remarks>
/// This runs from a page callback, which may arrive while the engine is suspended part way
/// through a wait. Nothing here touches engine state for that reason.
/// </remarks>
static void Queue_Event(BrowserEvent const & event)
{
	int next = (_EventTail + 1) % EVENT_QUEUE_SIZE;

	if (next == _EventHead) {
		return;
	}

	_Events[_EventTail] = event;
	_EventTail = next;
}


/// <summary>
/// Translates a DOM key identifier into the virtual key the engine's tables are written
/// against.
/// </summary>
/// <remarks>
/// The identifier names the physical key rather than the character it produces, which is
/// what the engine's hotkeys want. The character the key produced is recorded separately,
/// because that is the half a layout decides.
/// </remarks>
/// <param name="code">The DOM code of the key, as the page reported it.</param>
/// <returns>The virtual key, or VK_NONE when the key has no counterpart.</returns>
static unsigned short Virtual_Key_For_Code(char const * code)
{
	static struct {
		char const * Code;
		unsigned short Key;
	} const _named[] = {
		{ "Escape", VK_ESCAPE },		{ "Backspace", VK_BACK },		{ "Tab", VK_TAB },
		{ "Enter", VK_RETURN },			{ "NumpadEnter", VK_RETURN },	{ "Space", VK_SPACE },
		{ "ShiftLeft", VK_SHIFT },		{ "ShiftRight", VK_SHIFT },
		{ "ControlLeft", VK_CONTROL },	{ "ControlRight", VK_CONTROL },
		{ "AltLeft", VK_MENU },			{ "AltRight", VK_MENU },
		{ "CapsLock", VK_CAPITAL },		{ "NumLock", VK_NUMLOCK },		{ "ScrollLock", VK_SCROLL },
		{ "Pause", VK_PAUSE },			{ "PrintScreen", VK_SNAPSHOT },
		{ "Insert", VK_INSERT },		{ "Delete", VK_DELETE },
		{ "Home", VK_HOME },			{ "End", VK_END },
		{ "PageUp", VK_PRIOR },			{ "PageDown", VK_NEXT },
		{ "ArrowLeft", VK_LEFT },		{ "ArrowUp", VK_UP },
		{ "ArrowRight", VK_RIGHT },		{ "ArrowDown", VK_DOWN },
		{ "NumpadMultiply", VK_MULTIPLY },	{ "NumpadAdd", VK_ADD },
		{ "NumpadSubtract", VK_SUBTRACT },	{ "NumpadDecimal", VK_DECIMAL },
		{ "NumpadDivide", VK_DIVIDE },
		{ "Minus", VK_NONE_BD },		{ "Equal", VK_NONE_BB },
		{ "BracketLeft", VK_NONE_DB },	{ "BracketRight", VK_NONE_DD },
		{ "Backslash", VK_NONE_DC },	{ "Semicolon", VK_NONE_BA },
		{ "Quote", VK_NONE_DE },		{ "Backquote", VK_NONE_C0 },
		{ "Comma", VK_NONE_BC },		{ "Period", VK_NONE_BE },
		{ "Slash", VK_NONE_BF },
		{ "ContextMenu", VK_NONE_5D },
		{ "MetaLeft", VK_NONE_5B },		{ "MetaRight", VK_NONE_5C },
	};

	if (code == nullptr || code[0] == '\0') {
		return(VK_NONE);
	}

	if (strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z' && code[4] == '\0') {
		return((unsigned short)(VK_A + (code[3] - 'A')));
	}

	if (strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9' && code[6] == '\0') {
		return((unsigned short)(VK_0 + (code[5] - '0')));
	}

	if (strncmp(code, "Numpad", 6) == 0 && code[6] >= '0' && code[6] <= '9' && code[7] == '\0') {
		return((unsigned short)(VK_NUMPAD0 + (code[6] - '0')));
	}

	if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
		int number = code[1] - '0';
		if (code[2] >= '0' && code[2] <= '9' && code[3] == '\0') {
			number = number * 10 + (code[2] - '0');
		} else if (code[2] != '\0') {
			number = 0;
		}
		if (number >= 1 && number <= 24) {
			return((unsigned short)(VK_F1 + number - 1));
		}
	}

	for (unsigned index = 0; index < sizeof(_named) / sizeof(_named[0]); index++) {
		if (strcmp(code, _named[index].Code) == 0) {
			return(_named[index].Key);
		}
	}

	return(VK_NONE);
}


/// <summary>
/// Records the modifier state the page reported along with an event.
/// </summary>
static void Note_Modifiers(bool shift, bool ctrl, bool alt)
{
	_Modifiers = 0;
	if (shift) _Modifiers |= WWKEY_SHIFT_BIT;
	if (ctrl) _Modifiers |= WWKEY_CTRL_BIT;
	if (alt) _Modifiers |= WWKEY_ALT_BIT;

	_KeyDown[VK_SHIFT] = shift ? 1 : 0;
	_KeyDown[VK_CONTROL] = ctrl ? 1 : 0;
	_KeyDown[VK_MENU] = alt ? 1 : 0;
}


/// <summary>
/// Turns a position over the canvas into one in the game's frame.
/// </summary>
/// <remarks>
/// The page reports the position in CSS pixels and the drawing buffer is measured in
/// device pixels; conflating the two is what puts a cursor at the wrong place on a scaled
/// display. Once it is in drawing buffer pixels it is a window position like any other,
/// and the engine's own scaling conversion finishes the job.
/// </remarks>
static void Canvas_Point_To_Game(double cssx, double cssy, int & x, int & y)
{
	double ratio = emscripten_get_device_pixel_ratio();

	POINT point;
	point.x = (LONG)(cssx * ratio + 0.5);
	point.y = (LONG)(cssy * ratio + 0.5);

	Window_Point_To_Game(point);
	Clamp_To_Game(point);

	x = (int)point.x;
	y = (int)point.y;
}


static EM_BOOL Key_Callback(int type, EmscriptenKeyboardEvent const * event, void *)
{
	Note_Modifiers(event->shiftKey != 0, event->ctrlKey != 0, event->altKey != 0);

	unsigned short key = Virtual_Key_For_Code(event->code);
	if (key == VK_NONE) {
		return(EM_FALSE);
	}

	// A single character key value is the layout's own answer to what this key produces,
	// and it is the only answer a browser offers. Anything longer is a name such as
	// "Shift" or "ArrowUp" and produces no character at all.
	if (event->key[0] != '\0' && event->key[1] == '\0') {
		if ((_Modifiers & WWKEY_SHIFT_BIT) != 0) {
			_ShiftedAscii[key & 0xFF] = event->key[0];
		} else {
			_Ascii[key & 0xFF] = event->key[0];
		}
	}

	bool release = (type == EMSCRIPTEN_EVENT_KEYUP);

	// Windows suppresses the repeats the engine does not want by way of the previous key
	// state bit; the page reports the same thing as a repeat flag.
	if (!release && event->repeat != 0) {
		return(EM_TRUE);
	}

	_KeyDown[key & 0xFF] = release ? 0 : 1;

	BrowserEvent queued;
	queued.Key = key;
	queued.X = 0;
	queued.Y = 0;
	queued.IsMouse = false;
	queued.IsRelease = release;
	Queue_Event(queued);

	// Tab, the function keys, and the arrows all scroll or navigate the page unless the
	// event is claimed, and the game wants every one of them.
	return(EM_TRUE);
}


static EM_BOOL Mouse_Callback(int type, EmscriptenMouseEvent const * event, void *)
{
	Note_Modifiers(event->shiftKey != 0, event->ctrlKey != 0, event->altKey != 0);

	Canvas_Point_To_Game(event->targetX, event->targetY, _MouseX, _MouseY);

	if (type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
		return(EM_TRUE);
	}

	unsigned short key;
	switch (event->button) {
		case 1:
			key = VK_MBUTTON;
			break;

		case 2:
			key = VK_RBUTTON;
			break;

		default:
			key = VK_LBUTTON;
			break;
	}

	bool release = (type == EMSCRIPTEN_EVENT_MOUSEUP);
	_KeyDown[key] = release ? 0 : 1;

	BrowserEvent queued;
	queued.Key = key;
	queued.X = (short)_MouseX;
	queued.Y = (short)_MouseY;
	queued.IsMouse = true;
	queued.IsRelease = release;
	Queue_Event(queued);

	// A double click reaches the engine as a second press and release of the same button,
	// which is what the Windows handler makes of WM_LBUTTONDBLCLK.
	if (type == EMSCRIPTEN_EVENT_DBLCLICK) {
		queued.IsRelease = true;
		Queue_Event(queued);
	}

	return(EM_TRUE);
}


/// <summary>
/// Brings the reported position one pixel off whichever edge it was pinned against.
/// </summary>
static void Release_Mouse_Edge(void)
{
	if (!_MouseOutside) {
		return;
	}

	_MouseOutside = false;

	if (_MouseX <= 0) _MouseX = 1;
	if (_MouseY <= 0) _MouseY = 1;
	if (VideoModeWidth > 2 && _MouseX >= VideoModeWidth - 1) _MouseX = VideoModeWidth - 2;
	if (VideoModeHeight > 2 && _MouseY >= VideoModeHeight - 1) _MouseY = VideoModeHeight - 2;
}


static EM_BOOL Mouse_Boundary_Callback(int type, EmscriptenMouseEvent const * event, void *)
{
	if (type == EMSCRIPTEN_EVENT_MOUSEENTER) {
		_MouseOutside = false;
		return(EM_FALSE);
	}

	/*
	 * The event fires as the pointer crosses the boundary, so the position it carries is
	 * the one the pointer left through. Pulling that onto the frame puts it exactly on the
	 * edge, which is the position the engine reads as "keep scrolling this way".
	 */
	Canvas_Point_To_Game(event->targetX, event->targetY, _MouseX, _MouseY);
	_MouseOutside = true;
	return(EM_FALSE);
}


/*
** The page losing the keyboard is as close as a tab comes to the game losing the screen.
** Whatever the pointer was doing over the canvas stops there: it is somewhere else now.
*/
static EM_BOOL Blur_Callback(int, EmscriptenFocusEvent const *, void *)
{
	Release_Mouse_Edge();
	return(EM_FALSE);
}


static EM_BOOL Visibility_Callback(int, EmscriptenVisibilityChangeEvent const * event, void *)
{
	_Hidden = (event->hidden != 0);

	if (_Hidden) {
		Release_Mouse_Edge();
	}
	return(EM_FALSE);
}


/// <summary>
/// Puts the canvas, the page's events, and the game's focus flag in the state the page
/// has them in.
/// </summary>
void Browser_Service(void)
{
	if (!_Initialized) {
		return;
	}

	if (Measure_Canvas()) {

		// The presenter follows the canvas at once: the frame it already holds is simply
		// scaled into the new window, so the picture is never absent while the window is
		// being dragged.
		Video_On_Resize(_CanvasWidth, _CanvasHeight);

		// The frame is sized in CSS pixels. The drawing buffer above is not: a sidebar of
		// a fixed number of pixels would come out half as wide on a display carrying two
		// device pixels for each CSS one.
		Video_Request_Frame_Size(_CanvasCSSWidth, _CanvasCSSHeight);
	}

	/*
	 * Page visibility stands in for the window focus the engine parks itself on. Focus
	 * itself deliberately does not: clicking on another window does not stop a browser
	 * compositing this one, and a lockstep game that paused there would stall its peers.
	 */
	GameInFocus = !_Hidden;

	while (_EventHead != _EventTail) {
		BrowserEvent const event = _Events[_EventHead];
		_EventHead = (_EventHead + 1) % EVENT_QUEUE_SIZE;

		if (_EventHook != nullptr && _EventHook(event.Key, event.X, event.Y, event.IsMouse, event.IsRelease)) {
			continue;
		}

		if (Keyboard != nullptr) {
			if (event.IsMouse) {
				Keyboard->Post_Mouse_Event(event.Key, event.X, event.Y, event.IsRelease);
			} else {
				Keyboard->Post_Key_Event(event.Key, event.IsRelease);
			}
		}
	}
}


/// <summary>
/// Installs the hook that sees the page's events before the keyboard buffer does.
/// </summary>
void Browser_Set_Event_Hook(BrowserEventHook hook)
{
	_EventHook = hook;
}


/// <summary>
/// Hands the thread back to the page and returns when the page schedules the engine again.
/// </summary>
void Browser_Yield(void)
{
	if (!_EngineEntered) {
		return;
	}

	_BlockingWaits++;

#if defined(OPENTS_WASM_JSPI)
	Browser_Await_Frame(_Hidden ? 1 : 0);
	_FrameSerial++;
#else
	/*
	 * Nothing carries the wait. The engine keeps the thread, the page stops answering, and
	 * the count above is the work left to do; docs/WASM-PORT.md A.6 steps 5 to 9 are what
	 * removes the need for the scaffold this build was configured without.
	 */
	static bool _reported = false;
	if (!_reported) {
		_reported = true;
		DebugString("Browser: a wait was reached and the yield scaffold is not built in; the page will stop responding.\n");
	}
#endif

	_LastYield = emscripten_get_now();
	Browser_Service();
}


/// <summary>
/// Hands the thread back only when an animation frame's worth of time has passed.
/// </summary>
/// <remarks>
/// The engine reaches its callback hook far more often than it draws -- a single keyboard
/// read goes through it -- and yielding on each would cost a frame apiece. Pacing it here
/// bounds how long the page can be kept waiting without making the engine's throughput a
/// function of how often it happens to ask.
/// </remarks>
/// <returns>bool; Was the thread handed back?</returns>
bool Browser_Yield_If_Due(void)
{
	if ((emscripten_get_now() - _LastYield) < YIELD_INTERVAL) {
		return(false);
	}

	Browser_Yield();
	return(true);
}


unsigned int Browser_Blocking_Wait_Count(void)
{
	return(_BlockingWaits);
}


bool Browser_Yield_Is_Available(void)
{
#if defined(OPENTS_WASM_JSPI)
	return(true);
#else
	return(false);
#endif
}


unsigned int Browser_Frame_Serial(void)
{
	return(_FrameSerial);
}


int Browser_Canvas_Width(void)
{
	return(_CanvasWidth);
}


int Browser_Canvas_Height(void)
{
	return(_CanvasHeight);
}


int Browser_Canvas_CSS_Width(void)
{
	return(_CanvasCSSWidth);
}


int Browser_Canvas_CSS_Height(void)
{
	return(_CanvasCSSHeight);
}


int Browser_Screen_Width(void)
{
	return(_ScreenWidth);
}


int Browser_Screen_Height(void)
{
	return(_ScreenHeight);
}


BrowserDisplayPolicy Browser_Display_Policy(void)
{
	return(_DisplayPolicy);
}


int Browser_Display_Width(void)
{
	return(_DisplayWidth);
}


int Browser_Display_Height(void)
{
	return(_DisplayHeight);
}


bool Browser_Is_Hidden(void)
{
	return(_Hidden);
}


unsigned short Browser_Key_Modifiers(void)
{
	return(_Modifiers);
}


bool Browser_Key_Is_Down(unsigned short vk_key)
{
	return(_KeyDown[vk_key & 0xFF] != 0);
}


/// <summary>
/// Fetches the character the key produced when the page reported it.
/// </summary>
char Browser_Key_To_ASCII(unsigned short key)
{
	unsigned short vk_key = key & 0xFF;

	if ((key & WWKEY_SHIFT_BIT) != 0 && _ShiftedAscii[vk_key] != '\0') {
		return(_ShiftedAscii[vk_key]);
	}
	return(_Ascii[vk_key]);
}


int Browser_Mouse_X(void)
{
	return(_MouseX);
}


int Browser_Mouse_Y(void)
{
	return(_MouseY);
}


/*
** The mouse the engine drives on a page. Everything WWMouseClass does apart from finding
** the cursor works unchanged, and finding the cursor is the one thing a page cannot be
** asked: there is no cursor to query, only the position of the last event over the canvas.
*/
class BrowserMouseClass : public WWMouseClass
{
	public:
		BrowserMouseClass(HWND window) : WWMouseClass(window) {}

		virtual int Get_Mouse_X(void) const override {return(Browser_Mouse_X());}
		virtual int Get_Mouse_Y(void) const override {return(Browser_Mouse_Y());}
		virtual Point2D Get_Mouse_Point(void) const override {return(Point2D(Browser_Mouse_X(), Browser_Mouse_Y()));}
};


Mouse * Browser_Create_Mouse(HWND window)
{
	return(new BrowserMouseClass(window));
}


extern "C" {


/// <summary>
/// Reports how many animation frames the engine has been handed back and resumed on.
/// </summary>
/// <remarks>
/// A page cannot otherwise tell an engine that is running from one that started and then
/// stopped, and neither can an automated check, so this is what says the loop is alive.
/// </remarks>
EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Frames(void)
{
	return((int)_FrameSerial);
}


/// <summary>
/// Reports how many times the engine has reached a wait that only the scaffold carries.
/// </summary>
EMSCRIPTEN_KEEPALIVE int OpenTS_Browser_Waits(void)
{
	return((int)_BlockingWaits);
}


}


/// <summary>
/// Attaches the engine to the page.
/// </summary>
/// <returns>bool; Is the page in a state the engine can run on?</returns>
bool Browser_Init(void)
{
	if (_Initialized) {
		return(true);
	}

	_EngineEntered = true;
	_LastYield = emscripten_get_now();

	memset(_KeyDown, 0, sizeof(_KeyDown));
	memset(_Ascii, 0, sizeof(_Ascii));
	memset(_ShiftedAscii, 0, sizeof(_ShiftedAscii));

	Read_Page_Configuration();

	if (!Measure_Canvas()) {
		DebugString("Browser: the canvas at %s has not been laid out.\n", CANVAS_SELECTOR);
		return(false);
	}

	EmscriptenVisibilityChangeEvent visibility;
	_Hidden = (emscripten_get_visibility_status(&visibility) == EMSCRIPTEN_RESULT_SUCCESS) && (visibility.hidden != 0);
	GameInFocus = !_Hidden;

	/*
	 * The keyboard is taken from the window rather than from the canvas, because a canvas
	 * only receives key events while it holds the focus and the page may put the focus
	 * anywhere. The mouse is taken from the canvas, because a position is only meaningful
	 * over the thing the frame is drawn on.
	 */
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, Key_Callback);
	emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, Key_Callback);

	emscripten_set_mousemove_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);
	emscripten_set_mousedown_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);
	emscripten_set_mouseup_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);
	emscripten_set_dblclick_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Callback);

	emscripten_set_mouseleave_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Boundary_Callback);
	emscripten_set_mouseenter_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Mouse_Boundary_Callback);

	emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, Blur_Callback);
	emscripten_set_visibilitychange_callback(nullptr, EM_TRUE, Visibility_Callback);

	// The page draws its own context menu over the game otherwise, and the right button is
	// the engine's primary command.
	EM_ASM({
		var canvas = document.querySelector(UTF8ToString($0));
		if (canvas) {
			canvas.addEventListener("contextmenu", function (e) { e.preventDefault(); });
		}
	}, CANVAS_SELECTOR);

	_LastYield = emscripten_get_now();
	_Initialized = true;

	DebugString("Browser: canvas %s is %dx%d CSS pixels and %dx%d device pixels on a %dx%d screen; the frame %s the window; yield scaffold is %s.\n",
		CANVAS_SELECTOR, _CanvasCSSWidth, _CanvasCSSHeight, _CanvasWidth, _CanvasHeight, _ScreenWidth, _ScreenHeight,
		(_DisplayPolicy == BROWSER_DISPLAY_NATIVE) ? "follows" : "is scaled into",
		Browser_Yield_Is_Available() ? "built in" : "absent");

	return(true);
}

#endif	// __EMSCRIPTEN__
