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
#include "_rect.h"
#include "_tactica.h"
#include "dbgprint.h"
#include "globals.h"
#include "keyboard.h"
#include "misc.h"
#include "movies.h"
#include "tactical.h"
#include "video.h"
#include "vidscale.h"
#include "win.h"
#include "wwmouse.h"

#include "facing.hh"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
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

	// Was this synthesised from a finger? A tap is the only thing a finger can offer a movie,
	// and what a movie reads is not a button.
	bool IsTouch;
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

/*
 * Is that position one a pointer is resting at? A mouse leaves one wherever it stops and a
 * finger leaves nothing at all, so this follows whichever of the two reported last. The engine
 * reads it wherever it would otherwise take a leftover position for a hover: the edge scroll,
 * the tooltip, and the placement cursor that follows the pointer about. Nothing has reported
 * one until something does, and the corner the position starts at is an edge like any other.
 */
static bool _MouseHovering = false;

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
	// and it is the only answer a browser offers for a printable key. Anything longer is a
	// name: most, such as "Shift" or "ArrowUp", produce no character, but the few below do
	// carry one on Windows, and text entry reads them through To_ASCII like any other.
	char character = '\0';

	if (event->key[0] != '\0' && event->key[1] == '\0') {
		character = event->key[0];
	} else {
		switch (key & 0xFF) {
			case VK_RETURN:	character = '\r';	break;
			case VK_BACK:	character = '\b';	break;
			case VK_TAB:	character = '\t';	break;
			case VK_ESCAPE:	character = 27;		break;
			default:							break;
		}
	}

	if (character != '\0') {
		if ((_Modifiers & WWKEY_SHIFT_BIT) != 0) {
			_ShiftedAscii[key & 0xFF] = character;
		} else {
			_Ascii[key & 0xFF] = character;
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
	queued.IsTouch = false;
	Queue_Event(queued);

	// Tab, the function keys, and the arrows all scroll or navigate the page unless the
	// event is claimed, and the game wants every one of them.
	return(EM_TRUE);
}


static EM_BOOL Mouse_Callback(int type, EmscriptenMouseEvent const * event, void *)
{
	Note_Modifiers(event->shiftKey != 0, event->ctrlKey != 0, event->altKey != 0);

	// A mouse is resting wherever it stopped, so a hover the last finger took away comes back
	// the moment one is moved. Both are on the same tablet, and each says what it is.
	_MouseHovering = true;

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
	queued.IsTouch = false;
	Queue_Event(queued);

	// A double click reaches the engine as a second press and release of the same button,
	// which is what the Windows handler makes of WM_LBUTTONDBLCLK.
	if (type == EMSCRIPTEN_EVENT_DBLCLICK) {
		queued.IsRelease = true;
		Queue_Event(queued);
	}

	return(EM_TRUE);
}


/*
 * -----------------------------------------------------------------------------------------
 * The wheel.
 *
 * A wheel is not a button, so it has no place in the event queue above: the engine reads it
 * as a window message alone, and scrolls the sidebar's build list a step for each one. The
 * page reports what a wheel or a trackpad has travelled rather than how many notches it
 * turned, so what arrives is accumulated here and a message is posted for each notch's worth.
 * A trackpad reports a stream of small deltas where a wheel reports one large one, and
 * passing those straight through would fly the list past whatever the player was reaching for.
 * -----------------------------------------------------------------------------------------
 */

/*
 * What one notch has travelled, and what a browser reporting lines or pages means by one of
 * those. Which of the three it reports is its own and the device's business, and a wheel's
 * notch is a hundred pixels or three lines depending on which, so both come to exactly one
 * notch here and a whole turn of the wheel leaves nothing over.
 */
static const double WHEEL_NOTCH = 100.0;
static const double WHEEL_LINE = WHEEL_NOTCH / 3.0;
static const double WHEEL_PAGE = WHEEL_NOTCH * 3.0;

// What a Windows message calls one notch. Only the sign of it is read, but the message says
// what it always said.
static const int WHEEL_MESSAGE_DELTA = 120;

static double _WheelPending = 0.0;


static EM_BOOL Wheel_Callback(int, EmscriptenWheelEvent const * event, void *)
{
	Note_Modifiers(event->mouse.shiftKey != 0, event->mouse.ctrlKey != 0, event->mouse.altKey != 0);

	// Only a pointing device has a wheel, so one has reported itself along with the scroll.
	_MouseHovering = true;

	Canvas_Point_To_Game(event->mouse.targetX, event->mouse.targetY, _MouseX, _MouseY);

	double travel = event->deltaY;
	switch (event->deltaMode) {
		case DOM_DELTA_LINE:	travel *= WHEEL_LINE;	break;
		case DOM_DELTA_PAGE:	travel *= WHEEL_PAGE;	break;
		default:										break;
	}

	// A turn back the other way starts afresh. What was left over was too little to move
	// anything, and it is not what the player is asking for now.
	if ((travel > 0.0 && _WheelPending < 0.0) || (travel < 0.0 && _WheelPending > 0.0)) {
		_WheelPending = 0.0;
	}

	_WheelPending += travel;
	return(EM_TRUE);
}


/// <summary>
/// Posts a window message for each notch the wheel has travelled since the last pass.
/// </summary>
/// <remarks>
/// The message carries a position measured from the corner of the screen rather than from the
/// window, which is what msgroute.cpp expects of this one message, and it is posted to the
/// main window so that the routing there hands it to whichever window covers that spot.
/// </remarks>
static void Service_Wheel(void)
{
	if (MainWindow == NULL) {
		_WheelPending = 0.0;
		return;
	}

	while (_WheelPending >= WHEEL_NOTCH || _WheelPending <= -WHEEL_NOTCH) {

		bool away = (_WheelPending > 0.0);
		_WheelPending += away ? -WHEEL_NOTCH : WHEEL_NOTCH;

		/*
		 * A page counts a scroll away from the player as positive and a window message counts
		 * it as negative, which is the sign the sidebar reads to decide which way to go.
		 */
		short delta = (short)(away ? -WHEEL_MESSAGE_DELTA : WHEEL_MESSAGE_DELTA);

		POINT screen;
		screen.x = _MouseX;
		screen.y = _MouseY;
		Game_Point_To_Window(screen);

		PostMessage(MainWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, (unsigned short)delta),
			MAKELPARAM((short)screen.x, (short)screen.y));
	}
}


/*
 * -----------------------------------------------------------------------------------------
 * Typed text.
 *
 * A device whose only keyboard is drawn on its screen shows it for one reason: something that
 * takes text holds the focus. The engine has waits that typed text alone ends -- the hall of
 * fame name is one, and it runs until Return arrives -- so those say while they are waiting
 * and an off-picture element is focused for as long as they do.
 *
 * The element takes no part in what is typed. Keys reach the engine by bubbling to the window
 * listener as they always have, and the element is only ever the reason a keyboard is on the
 * screen at all. The exception is a key that arrives without a keydown worth the name, which
 * is what a software keyboard does while it is correcting or composing; those are read off
 * the element's own input event, and Browser_Service picks them up in engine context rather
 * than the page handing them over from a callback.
 * -----------------------------------------------------------------------------------------
 */

static bool _TextInputWanted = false;


/// <summary>
/// Puts the focus on the element a keyboard is raised for, building it the first time.
/// </summary>
static void Focus_Text_Input(void)
{
	EM_ASM({
		var el = document.getElementById("opents-text");

		if (!el) {
			el = document.createElement("input");
			el.id = "opents-text";
			el.type = "text";
			el.opentsQueue = [];
			el.setAttribute("autocomplete", "off");
			el.setAttribute("autocorrect", "off");
			el.setAttribute("autocapitalize", "off");
			el.setAttribute("spellcheck", "false");
			el.setAttribute("enterkeyhint", "done");
			el.setAttribute("aria-hidden", "true");

			/* Out of the picture but not out of the layout: nothing raises a keyboard for an
			   element it cannot focus, and it cannot focus one that is not displayed. */
			el.setAttribute("style", "position:fixed;left:0;top:0;width:1px;height:1px;" +
				"opacity:0;border:0;padding:0;margin:0;background:transparent;" +
				"color:transparent;caret-color:transparent;z-index:-1;");

			el.addEventListener("input", function (e) {
				var text = el.value;
				el.value = "";
				if (e.inputType === "deleteContentBackward") {
					el.opentsQueue.push(8);
				} else if (e.inputType === "insertLineBreak") {
					el.opentsQueue.push(13);
				} else {
					for (var index = 0; index < text.length; index++) {
						el.opentsQueue.push(text.charCodeAt(index));
					}
				}
			});

			document.body.appendChild(el);
		}

		el.value = "";
		try {
			el.focus({ preventScroll: true });
		} catch (e) {
			el.focus();
		}
	});
}


/// <summary>
/// Takes the next character the page has that no key event carried.
/// </summary>
/// <returns>The character, or zero when there is none waiting.</returns>
static int Fetch_Text_Character(void)
{
	return(EM_ASM_INT({
		var el = document.getElementById("opents-text");
		if (!el || !el.opentsQueue || el.opentsQueue.length === 0) {
			return 0;
		}
		return el.opentsQueue.shift() | 0;
	}));
}


/// <summary>
/// Names the key a character is delivered on.
/// </summary>
/// <remarks>
/// To_ASCII answers out of a table written as the key went down, so a character with no key
/// of its own has to borrow one. The borrowed key is the one that produces the character on a
/// US layout, which is what keeps a hotkey reading the same thing the player typed.
/// </remarks>
/// <returns>The virtual key, or VK_NONE for a character no key here produces.</returns>
static unsigned short Virtual_Key_For_Character(int character)
{
	if (character >= 'a' && character <= 'z') return((unsigned short)(VK_A + (character - 'a')));
	if (character >= 'A' && character <= 'Z') return((unsigned short)(VK_A + (character - 'A')));
	if (character >= '0' && character <= '9') return((unsigned short)(VK_0 + (character - '0')));

	switch (character) {
		case ' ':	return(VK_SPACE);
		case '\r':	return(VK_RETURN);
		case '\n':	return(VK_RETURN);
		case '\b':	return(VK_BACK);
		case '\t':	return(VK_TAB);
		case 27:	return(VK_ESCAPE);
		case '-':	return(VK_NONE_BD);
		case '=':	return(VK_NONE_BB);
		case '[':	return(VK_NONE_DB);
		case ']':	return(VK_NONE_DD);
		case '\\':	return(VK_NONE_DC);
		case ';':	return(VK_NONE_BA);
		case '\'':	return(VK_NONE_DE);
		case '`':	return(VK_NONE_C0);
		case ',':	return(VK_NONE_BC);
		case '.':	return(VK_NONE_BE);
		case '/':	return(VK_NONE_BF);
		default:	break;
	}

	return(VK_NONE);
}


/// <summary>
/// Hands the engine whatever the page's own keyboard typed without a key event.
/// </summary>
static void Service_Text_Input(void)
{
	if (!_TextInputWanted) {
		return;
	}

	for (int character = Fetch_Text_Character(); character != 0; character = Fetch_Text_Character()) {

		unsigned short key = Virtual_Key_For_Character(character);
		if (key == VK_NONE) {
			continue;
		}

		// Recorded under both shift states, because the key is only a carrier here and the
		// modifiers the page last reported are stamped onto it on the way into the buffer.
		_Ascii[key & 0xFF] = (char)character;
		_ShiftedAscii[key & 0xFF] = (char)character;

		BrowserEvent queued;
		queued.Key = key;
		queued.X = 0;
		queued.Y = 0;
		queued.IsMouse = false;
		queued.IsRelease = false;
		queued.IsTouch = false;
		Queue_Event(queued);

		queued.IsRelease = true;
		Queue_Event(queued);
	}
}


/*
 * -----------------------------------------------------------------------------------------
 * Touch.
 *
 * A page reports a finger as a touch and, for a tap that does not move, follows it with a
 * synthesised mouse click. A drag gets no mouse events at all, so without what follows the
 * map cannot be moved and nothing can be band selected by finger. Two rules decide every
 * gesture, and what they produce is the input the engine already understands:
 *
 *   one finger is the left button
 *     tap                   press and release where the finger landed
 *     hold                  the right button instead, which is the engine's cascading cancel
 *     drag                  the button held and carried, which the map reads as a rubber band
 *   two fingers are the view
 *     drag                  the map panned one for one under them
 *     tap                   the right button as well, kept because a tablet player knows it
 *
 * Where the finger is decides nothing. The engine already knows what a button means over the
 * tactical view, the sidebar, the tab bar and a shell screen, and touch must neither permit
 * what a mouse is forbidden nor forbid what it is allowed; only the pan is carried out here
 * rather than handed over, because it reaches the map directly. A tap during a movie is the
 * exception the movie player forces: what it reads is not a button.
 *
 * Nothing a finger does leaves a hover behind. Browser_Mouse_Is_Hovering is how the engine is
 * told so, because a pointer resting somewhere is a thing only a mouse has.
 *
 * The callbacks run while the engine may be suspended part way through a frame, so like the
 * mouse ones they only write scalars and queue events; Browser_Service applies them.
 * -----------------------------------------------------------------------------------------
 */

// How far a finger may travel and still be resting, and how long it may rest before it is the
// right button rather than a tap. Both are in CSS pixels and milliseconds, which is what a
// finger is measured in whatever a display carries per pixel.
static const double TOUCH_SLOP = 10.0;
static const double TOUCH_HOLD = 450.0;

enum BrowserGesture {
	GESTURE_NONE,		// Nothing is on the glass.
	GESTURE_UNDECIDED,	// One finger is down and has neither travelled nor rested long enough.
	GESTURE_DRAG,		// One finger is holding the left button and carrying it.
	GESTURE_MULTI,		// Two fingers are moving the view.
	GESTURE_SPENT,		// The gesture is over, but a finger is still down.
};

static BrowserGesture _Gesture = GESTURE_NONE;
static long _GestureID = 0;
static double _GestureTime = 0.0;
static double _GestureStartX = 0.0;
static double _GestureStartY = 0.0;
static double _GestureLastX = 0.0;
static double _GestureLastY = 0.0;

static double _MultiTime = 0.0;
static double _MultiStartX = 0.0;
static double _MultiStartY = 0.0;
static double _MultiLastX = 0.0;
static double _MultiLastY = 0.0;
static bool _MultiMoved = false;

// What the finger has asked the map to travel and the engine has yet to be told about. It is
// carried as a fraction because a slow drag moves less than a whole pixel between two events
// and dropping the remainder would leave the map behind the finger.
static double _PanPendingX = 0.0;
static double _PanPendingY = 0.0;



/// <summary>
/// Scales a distance measured across the canvas into one measured across the game's frame.
/// </summary>
static void Canvas_Delta_To_Game(double cssdx, double cssdy, double & dx, double & dy)
{
	double ratio = emscripten_get_device_pixel_ratio();

	dx = cssdx * ratio;
	dy = cssdy * ratio;

	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	if (scale.DestWidth > 0 && scale.DestHeight > 0) {
		dx = dx * (double)scale.GameWidth / (double)scale.DestWidth;
		dy = dy * (double)scale.GameHeight / (double)scale.DestHeight;
	}
}


/// <summary>
/// Is the tactical map on screen and in a state that a gesture may move it?
/// </summary>
/// <remarks>
/// A pan reaches the map directly rather than through the messages the engine drops while it
/// is driving, so what suppresses a mouse has to be asked here. That is IgnoreInput, which a
/// scripted sequence and an open in game dialog both raise and which ScrollClass reads for
/// the same purpose. Every other gesture is handed over as a button and gated by the engine.
/// </remarks>
static bool Touch_Tactical_Ready(void)
{
	return(TacticalMap != nullptr && TacticalActive && ScenarioActive && GameActive
		&& !Movie_Is_Playing() && !IgnoreInput);
}


/// <summary>
/// Queues a synthesised button event at a position in the frame.
/// </summary>
static void Queue_Touch_Button(unsigned short key, int x, int y, bool release)
{
	BrowserEvent queued;
	queued.Key = key;
	queued.X = (short)x;
	queued.Y = (short)y;
	queued.IsMouse = true;
	queued.IsRelease = release;
	queued.IsTouch = true;
	Queue_Event(queued);
}


/// <summary>
/// Queues a synthesised button press and release at a position in the frame.
/// </summary>
static void Queue_Touch_Click(unsigned short key, int x, int y)
{
	Queue_Touch_Button(key, x, y, false);
	Queue_Touch_Button(key, x, y, true);
}


/// <summary>
/// Moves the map by what the finger has travelled since the last pass.
/// </summary>
/// <remarks>
/// The map follows the finger rather than leading it, which is the way every other surface a
/// finger drags behaves. There is no inertia: a pan that carries on after the finger has gone
/// is a pan the player cannot stop over a unit, and the engine's own coast scroll already
/// occupies the other idiom.
/// </remarks>
static void Touch_Service_Pan(void)
{
	if (_PanPendingX == 0.0 && _PanPendingY == 0.0) {
		return;
	}

	if (!Touch_Tactical_Ready()) {
		_PanPendingX = 0.0;
		_PanPendingY = 0.0;
		return;
	}

	int stepx = (int)_PanPendingX;
	int stepy = (int)_PanPendingY;

	_PanPendingX -= (double)stepx;
	_PanPendingY -= (double)stepy;

	if (stepx > 0) {
		TacticalMap->Scroll_Map(FACING_W, stepx);
	} else if (stepx < 0) {
		TacticalMap->Scroll_Map(FACING_E, -stepx);
	}

	if (stepy > 0) {
		TacticalMap->Scroll_Map(FACING_N, stepy);
	} else if (stepy < 0) {
		TacticalMap->Scroll_Map(FACING_S, -stepy);
	}
}


/// <summary>
/// Turns a finger that has rested without travelling into the right button.
/// </summary>
/// <remarks>
/// This is read here rather than in the callback because a finger that rests reports nothing
/// at all; the timer has to be looked at by something that runs anyway. The button is sent
/// while the finger is still down, so what it cancels goes away under the finger rather than
/// after it has been lifted.
/// </remarks>
static void Touch_Service_Hold(void)
{
	if (_Gesture != GESTURE_UNDECIDED) {
		return;
	}

	if ((emscripten_get_now() - _GestureTime) < TOUCH_HOLD) {
		return;
	}

	int x;
	int y;
	Canvas_Point_To_Game(_GestureStartX, _GestureStartY, x, y);
	_MouseX = x;
	_MouseY = y;

	Queue_Touch_Click(VK_RBUTTON, x, y);
	_Gesture = GESTURE_SPENT;
}


/// <summary>
/// Presses the left button where the finger landed and starts carrying it.
/// </summary>
/// <remarks>
/// The press is placed where the finger came down rather than where it has reached, because
/// that is the corner a rubber band is anchored at and the control a drag started on.
/// </remarks>
static void Touch_Begin_Drag(void)
{
	int x;
	int y;
	Canvas_Point_To_Game(_GestureStartX, _GestureStartY, x, y);
	_MouseX = x;
	_MouseY = y;

	_KeyDown[VK_LBUTTON] = 1;
	Queue_Touch_Button(VK_LBUTTON, x, y, false);

	_Gesture = GESTURE_DRAG;
}


/// <summary>
/// Releases the left button a finger was carrying.
/// </summary>
static void Touch_End_Drag(double cssx, double cssy)
{
	int x;
	int y;
	Canvas_Point_To_Game(cssx, cssy, x, y);
	_MouseX = x;
	_MouseY = y;

	_KeyDown[VK_LBUTTON] = 0;
	Queue_Touch_Button(VK_LBUTTON, x, y, true);
}


static EM_BOOL Touch_Callback(int type, EmscriptenTouchEvent const * event, void *)
{
	double now = emscripten_get_now();

	// Whatever the gesture turns out to be, a finger is on the glass and nothing is resting
	// anywhere. Only a mouse event puts the hover back.
	_MouseHovering = false;

	/*
	 * A page will not raise its own keyboard for anything but a gesture, so the request the
	 * engine made is renewed here, where there is one to spend.
	 */
	if (type == EMSCRIPTEN_EVENT_TOUCHSTART && _TextInputWanted) {
		Focus_Text_Input();
	}

	/*
	 * A browser names every finger it knows about on every event, the ones that have just
	 * left included, so what has to be counted is the fingers still on the glass. The first
	 * two of them decide the gesture; a third is ignored rather than being allowed to change
	 * what is already under way.
	 */
	bool ending = (type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL);

	int count = 0;
	EmscriptenTouchPoint const * points[2] = { nullptr, nullptr };

	for (int index = 0; index < event->numTouches; index++) {
		EmscriptenTouchPoint const * point = &event->touches[index];

		if (ending && point->isChanged != 0) {
			continue;
		}

		if (count < 2) {
			points[count] = point;
		}
		count++;
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHSTART) {

		if (count >= 2) {

			// A second finger takes the gesture over. Whatever one finger was doing is
			// finished off first, so a button that was held is not left down.
			if (_Gesture == GESTURE_DRAG) {
				Touch_End_Drag(_GestureLastX, _GestureLastY);
			}

			_Gesture = GESTURE_MULTI;
			_MultiTime = now;
			_MultiStartX = (points[0]->targetX + points[1]->targetX) / 2.0;
			_MultiStartY = (points[0]->targetY + points[1]->targetY) / 2.0;
			_MultiLastX = _MultiStartX;
			_MultiLastY = _MultiStartY;
			_MultiMoved = false;
			return(EM_TRUE);
		}

		if (points[0] != nullptr) {
			_Gesture = GESTURE_UNDECIDED;
			_GestureID = points[0]->identifier;
			_GestureTime = now;
			_GestureStartX = points[0]->targetX;
			_GestureStartY = points[0]->targetY;
			_GestureLastX = _GestureStartX;
			_GestureLastY = _GestureStartY;
			_PanPendingX = 0.0;
			_PanPendingY = 0.0;
		}
		return(EM_TRUE);
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHMOVE) {

		if (_Gesture == GESTURE_MULTI) {
			if (count >= 2) {
				double cx = (points[0]->targetX + points[1]->targetX) / 2.0;
				double cy = (points[0]->targetY + points[1]->targetY) / 2.0;

				if (fabs(cx - _MultiStartX) > TOUCH_SLOP || fabs(cy - _MultiStartY) > TOUCH_SLOP) {
					_MultiMoved = true;
				}

				double dx;
				double dy;
				Canvas_Delta_To_Game(cx - _MultiLastX, cy - _MultiLastY, dx, dy);
				_PanPendingX += dx;
				_PanPendingY += dy;

				_MultiLastX = cx;
				_MultiLastY = cy;
			}
			return(EM_TRUE);
		}

		EmscriptenTouchPoint const * finger = nullptr;
		for (int index = 0; index < event->numTouches; index++) {
			if (event->touches[index].identifier == _GestureID) {
				finger = &event->touches[index];
				break;
			}
		}
		if (finger == nullptr) {
			return(EM_TRUE);
		}

		double x = finger->targetX;
		double y = finger->targetY;

		/*
		 * A finger that travels before it has rested is carrying the left button, wherever it
		 * came down. What that draws is the engine's business: a rubber band over the tactical
		 * view, a slider in a dialog, and nothing at all over a cameo.
		 */
		if (_Gesture == GESTURE_UNDECIDED) {
			double travel = fabs(x - _GestureStartX) + fabs(y - _GestureStartY);
			if (travel > TOUCH_SLOP) {
				Touch_Begin_Drag();
			}
		}

		if (_Gesture == GESTURE_DRAG) {
			int gx;
			int gy;
			Canvas_Point_To_Game(x, y, gx, gy);
			_MouseX = gx;
			_MouseY = gy;
		}

		_GestureLastX = x;
		_GestureLastY = y;
		return(EM_TRUE);
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHEND) {

		if (_Gesture == GESTURE_MULTI && count < 2) {

			// Two fingers that neither travelled nor lingered are the right button as well.
			if (!_MultiMoved && (now - _MultiTime) < TOUCH_HOLD) {
				int x;
				int y;
				Canvas_Point_To_Game(_MultiStartX, _MultiStartY, x, y);
				_MouseX = x;
				_MouseY = y;
				Queue_Touch_Click(VK_RBUTTON, x, y);
			}
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_DRAG) {
			Touch_End_Drag(_GestureLastX, _GestureLastY);
			_Gesture = GESTURE_SPENT;

		} else if (_Gesture == GESTURE_UNDECIDED) {
			int x;
			int y;
			Canvas_Point_To_Game(_GestureStartX, _GestureStartY, x, y);
			_MouseX = x;
			_MouseY = y;
			Queue_Touch_Click(VK_LBUTTON, x, y);
			_Gesture = GESTURE_SPENT;
		}
	}

	if (type == EMSCRIPTEN_EVENT_TOUCHCANCEL) {
		if (_Gesture == GESTURE_DRAG) {
			Touch_End_Drag(_GestureLastX, _GestureLastY);
		}
		_Gesture = GESTURE_SPENT;
	}

	if (ending && count == 0) {
		_Gesture = GESTURE_NONE;
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
		_MouseHovering = true;
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

		/*
		 * The window the engine believes it has covers the screen, and on a page the screen
		 * is the canvas. Mouse messages are hit tested against that window's rectangle in
		 * the canvas's own pixels, so a canvas that has outgrown it drops every event past
		 * the old edge -- the cursor still tracks, because its position never came from a
		 * message, and the clicks simply stop working.
		 */
		if (MainWindow != NULL) {
			MoveWindow(MainWindow, 0, 0, _CanvasWidth, _CanvasHeight, FALSE);
		}

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

	Service_Text_Input();

	while (_EventHead != _EventTail) {
		BrowserEvent const event = _Events[_EventHead];
		_EventHead = (_EventHead + 1) % EVENT_QUEUE_SIZE;

		/*
		 * A finger is a button everywhere except over a movie, where the only thing the player
		 * can ask for is that it stop and the only thing the movie player reads is escape.
		 * The escape goes to the keyboard buffer alone and never to the key state, so it
		 * cannot reach the window messages and open the options dialog behind the movie. A
		 * movie started with its break disallowed still ignores it.
		 */
		if (event.IsTouch && Movie_Is_Playing()) {
			if (Keyboard != nullptr) {
				Keyboard->Post_Key_Event(VK_ESCAPE, event.IsRelease);
			}
			continue;
		}

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

	Service_Wheel();

	Touch_Service_Hold();
	Touch_Service_Pan();
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


bool Browser_Mouse_Is_Hovering(void)
{
	return(_MouseHovering);
}


/// <summary>
/// Says that the engine is waiting on typed text.
/// </summary>
void Browser_Begin_Text_Input(void)
{
	_TextInputWanted = true;
	Focus_Text_Input();
}


/// <summary>
/// Says that the engine has stopped waiting on typed text.
/// </summary>
void Browser_End_Text_Input(void)
{
	if (!_TextInputWanted) {
		return;
	}

	_TextInputWanted = false;

	EM_ASM({
		var el = document.getElementById("opents-text");
		if (el) {
			el.value = "";
			el.opentsQueue = [];
			el.blur();
		}
	});
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
		virtual bool Is_Hovering(void) const override {return(Browser_Mouse_Is_Hovering());}
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

	// The wheel is claimed for the same reason the touches below are: a page left to its own
	// devices scrolls itself, and what the player is turning it over is the game.
	emscripten_set_wheel_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Wheel_Callback);

	/*
	 * Touch is taken from the canvas for the same reason the mouse is, and every event is
	 * claimed: a page that is still allowed its own idea of what a drag means will scroll,
	 * zoom, or start selecting text underneath the gesture.
	 */
	emscripten_set_touchstart_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);
	emscripten_set_touchmove_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);
	emscripten_set_touchend_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);
	emscripten_set_touchcancel_callback(CANVAS_SELECTOR, nullptr, EM_TRUE, Touch_Callback);

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
