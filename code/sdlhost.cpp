/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The native host. It answers the same contract browser.h states for the WebAssembly
// target -- a drawing target, an input drain, the yield trio -- out of an SDL window
// instead of a page, and supplies the process entry point. The Win32 substitute in the
// win32* files reads input state and window measurements from here exactly as it does
// from browser.cpp on a page.

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include "browser.h"
#include "_keyboar.h"
#include "keyboard.h"
#include "vidscale.h"
#include "video.h"
#include "win.h"
#include "win32timer.h"
#include "wwmouse.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdio>
#include <cstring>

int CALLBACK WinMain(HINSTANCE instance, HINSTANCE previous, char * command, int show);
bool Movie_Is_Playing(void);

namespace {

int const EVENT_QUEUE_SIZE = 256;

struct HostEvent
{
	unsigned short Key;
	short X;
	short Y;
	bool IsMouse;
	bool IsRelease;
};

SDL_Window * _Window = nullptr;
bool _Initialized = false;
bool _Fullscreen = false;

// What to restore the window to when fullscreen is turned off. The engine's configured
// size, or what the window was before it was made fullscreen.
int _WindowedWidth = 1280;
int _WindowedHeight = 800;

int _CanvasWidth = 0;
int _CanvasHeight = 0;
int _CanvasCSSWidth = 0;
int _CanvasCSSHeight = 0;

HostEvent _Events[EVENT_QUEUE_SIZE];
int _EventHead = 0;
int _EventTail = 0;
BrowserEventHook _EventHook = nullptr;

char _KeyDown[256];
char _Ascii[256];
char _ShiftedAscii[256];
unsigned short _Modifiers = 0;

int _MouseX = 0;
int _MouseY = 0;
bool _MouseHovering = false;

unsigned int _FrameSerial = 0;
unsigned int _BlockingWaits = 0;
unsigned int _LastYield = 0;


void Queue_Event(HostEvent const & event)
{
	int next = (_EventTail + 1) % EVENT_QUEUE_SIZE;
	if (next == _EventHead) {
		return;
	}

	_Events[_EventTail] = event;
	_EventTail = next;
}


/// <summary>
/// Maps an SDL scancode onto the virtual key the engine reads, by keyboard position, which
/// is also how the page's KeyboardEvent codes are mapped.
/// </summary>
unsigned short Virtual_Key_For_Scancode(SDL_Scancode code)
{
	if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
		return((unsigned short)('A' + (code - SDL_SCANCODE_A)));
	}
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
		return((unsigned short)('1' + (code - SDL_SCANCODE_1)));
	}
	if (code == SDL_SCANCODE_0) {
		return('0');
	}
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
		return((unsigned short)(VK_F1 + (code - SDL_SCANCODE_F1)));
	}
	if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) {
		static unsigned short const numpad[9] = {
			VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5,
			VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9
		};
		return(numpad[code - SDL_SCANCODE_KP_1]);
	}

	switch (code) {
		case SDL_SCANCODE_ESCAPE:		return(VK_ESCAPE);
		case SDL_SCANCODE_RETURN:		return(VK_RETURN);
		case SDL_SCANCODE_KP_ENTER:		return(VK_RETURN);
		case SDL_SCANCODE_SPACE:		return(VK_SPACE);
		case SDL_SCANCODE_BACKSPACE:	return(VK_BACK);
		case SDL_SCANCODE_TAB:			return(VK_TAB);
		case SDL_SCANCODE_LSHIFT:		return(VK_SHIFT);
		case SDL_SCANCODE_RSHIFT:		return(VK_SHIFT);
		case SDL_SCANCODE_LCTRL:		return(VK_CONTROL);
		case SDL_SCANCODE_RCTRL:		return(VK_CONTROL);
		case SDL_SCANCODE_LALT:			return(VK_MENU);
		case SDL_SCANCODE_RALT:			return(VK_MENU);
		case SDL_SCANCODE_LGUI:			return(VK_CONTROL);
		case SDL_SCANCODE_RGUI:			return(VK_CONTROL);
		case SDL_SCANCODE_UP:			return(VK_UP);
		case SDL_SCANCODE_DOWN:			return(VK_DOWN);
		case SDL_SCANCODE_LEFT:			return(VK_LEFT);
		case SDL_SCANCODE_RIGHT:		return(VK_RIGHT);
		case SDL_SCANCODE_HOME:			return(VK_HOME);
		case SDL_SCANCODE_END:			return(VK_END);
		case SDL_SCANCODE_PAGEUP:		return(VK_PRIOR);
		case SDL_SCANCODE_PAGEDOWN:		return(VK_NEXT);
		case SDL_SCANCODE_INSERT:		return(VK_INSERT);
		case SDL_SCANCODE_DELETE:		return(VK_DELETE);
		case SDL_SCANCODE_PAUSE:		return(VK_PAUSE);
		case SDL_SCANCODE_CAPSLOCK:		return(VK_CAPITAL);
		case SDL_SCANCODE_NUMLOCKCLEAR:	return(VK_NUMLOCK);
		case SDL_SCANCODE_SCROLLLOCK:	return(VK_SCROLL);
		case SDL_SCANCODE_MINUS:		return(VK_NONE_BD);
		case SDL_SCANCODE_EQUALS:		return(VK_NONE_BB);
		case SDL_SCANCODE_COMMA:		return(VK_NONE_BC);
		case SDL_SCANCODE_PERIOD:		return(VK_NONE_BE);
		case SDL_SCANCODE_SLASH:		return(VK_NONE_BF);
		case SDL_SCANCODE_SEMICOLON:	return(VK_NONE_BA);
		case SDL_SCANCODE_APOSTROPHE:	return(VK_NONE_DE);
		case SDL_SCANCODE_LEFTBRACKET:	return(VK_NONE_DB);
		case SDL_SCANCODE_RIGHTBRACKET:	return(VK_NONE_DD);
		case SDL_SCANCODE_BACKSLASH:	return(VK_NONE_DC);
		case SDL_SCANCODE_GRAVE:		return(VK_NONE_C0);
		case SDL_SCANCODE_KP_0:			return(VK_NUMPAD0);
		case SDL_SCANCODE_KP_PLUS:		return(VK_ADD);
		case SDL_SCANCODE_KP_MINUS:		return(VK_SUBTRACT);
		case SDL_SCANCODE_KP_MULTIPLY:	return(VK_MULTIPLY);
		case SDL_SCANCODE_KP_DIVIDE:	return(VK_DIVIDE);
		case SDL_SCANCODE_KP_PERIOD:	return(VK_DECIMAL);
		default:						break;
	}

	return(0);
}


void Note_Modifiers(Uint16 mod)
{
	_Modifiers = 0;
	if ((mod & KMOD_SHIFT) != 0) _Modifiers |= WWKEY_SHIFT_BIT;
	if ((mod & (KMOD_CTRL | KMOD_GUI)) != 0) _Modifiers |= WWKEY_CTRL_BIT;
	if ((mod & KMOD_ALT) != 0) _Modifiers |= WWKEY_ALT_BIT;
}


void Handle_Key(SDL_KeyboardEvent const & event)
{
	Note_Modifiers(event.keysym.mod);

	unsigned short key = Virtual_Key_For_Scancode(event.keysym.scancode);
	if (key == 0) {
		return;
	}

	// The layout's own character for this key, recorded for the To_ASCII path the text
	// fields read. SDL reports the unshifted symbol; the shifted one is derived, which is
	// exact for letters and the US layout's punctuation.
	SDL_Keycode sym = event.keysym.sym;
	if (sym >= 32 && sym < 127) {
		char character = (char)sym;
		char shifted = character;

		if (character >= 'a' && character <= 'z') {
			shifted = (char)(character - 'a' + 'A');
		} else {
			static char const plain[] = "1234567890-=[]\\;',./`";
			static char const upper[] = "!@#$%^&*()_+{}|:\"<>?~";
			char const * hit = strchr(plain, character);
			if (hit != nullptr) {
				shifted = upper[hit - plain];
			}
		}

		_Ascii[key & 0xFF] = character;
		_ShiftedAscii[key & 0xFF] = shifted;
	} else {
		switch (key & 0xFF) {
			case VK_RETURN:	_Ascii[key & 0xFF] = '\r';	break;
			case VK_BACK:	_Ascii[key & 0xFF] = '\b';	break;
			case VK_TAB:	_Ascii[key & 0xFF] = '\t';	break;
			case VK_ESCAPE:	_Ascii[key & 0xFF] = 27;	break;
			default:									break;
		}
	}

	bool release = (event.type == SDL_KEYUP);

	// Windows suppresses typematic repeats through the previous key state bit; SDL reports
	// them as a repeat flag.
	if (!release && event.repeat != 0) {
		return;
	}

	_KeyDown[key & 0xFF] = release ? 0 : 1;

	HostEvent queued;
	queued.Key = key;
	queued.X = 0;
	queued.Y = 0;
	queued.IsMouse = false;
	queued.IsRelease = release;
	Queue_Event(queued);
}


void Point_To_Game(int windowx, int windowy, int & gamex, int & gamey)
{
	// Mouse coordinates arrive in window points; the frame runs in drawable pixels on a
	// display that carries more than one pixel per point.
	int width = 1;
	int height = 1;
	SDL_GetWindowSize(_Window, &width, &height);

	POINT point;
	point.x = windowx * _CanvasWidth / (width > 0 ? width : 1);
	point.y = windowy * _CanvasHeight / (height > 0 ? height : 1);

	// The engine reads these as frame positions, so the frame's own place in the
	// canvas is applied here, as the browser host does.
	Window_Point_To_Game(point);
	Clamp_To_Game(point);

	gamex = (int)point.x;
	gamey = (int)point.y;
}


void Handle_Mouse_Button(SDL_MouseButtonEvent const & event)
{
	Point_To_Game(event.x, event.y, _MouseX, _MouseY);

	unsigned short key;
	switch (event.button) {
		case SDL_BUTTON_MIDDLE:
			key = VK_MBUTTON;
			break;

		case SDL_BUTTON_RIGHT:
			key = VK_RBUTTON;
			break;

		default:
			key = VK_LBUTTON;
			break;
	}

	bool release = (event.type == SDL_MOUSEBUTTONUP);
	_KeyDown[key & 0xFF] = release ? 0 : 1;

	HostEvent queued;
	queued.Key = key;
	queued.X = (short)_MouseX;
	queued.Y = (short)_MouseY;
	queued.IsMouse = true;
	queued.IsRelease = release;
	Queue_Event(queued);
}


/// <summary>
/// Reads the window's sizes and reports whether they changed since the last look.
/// </summary>
bool Measure_Window(void)
{
	int drawablew = 0;
	int drawableh = 0;
	int pointw = 0;
	int pointh = 0;

	SDL_GL_GetDrawableSize(_Window, &drawablew, &drawableh);
	if (drawablew <= 0 || drawableh <= 0) {
		SDL_GetWindowSize(_Window, &drawablew, &drawableh);
	}
	SDL_GetWindowSize(_Window, &pointw, &pointh);

	bool changed = (drawablew != _CanvasWidth) || (drawableh != _CanvasHeight)
		|| (pointw != _CanvasCSSWidth) || (pointh != _CanvasCSSHeight);

	_CanvasWidth = drawablew;
	_CanvasHeight = drawableh;
	_CanvasCSSWidth = pointw;
	_CanvasCSSHeight = pointh;

	return(changed);
}

}	// namespace


bool Browser_Init(void)
{
	if (_Initialized) {
		return(true);
	}

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "OpenTS: SDL could not start: %s\n", SDL_GetError());
		return(false);
	}

	_Window = SDL_CreateWindow("OpenTS",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

	if (_Window == nullptr) {
		fprintf(stderr, "OpenTS: the window could not be created: %s\n", SDL_GetError());
		return(false);
	}

	Measure_Window();
	_Initialized = true;
	return(true);
}


/// <summary>
/// Hands the renderer what it needs to attach to the host's window.
/// </summary>
void * Host_Native_Window_Handle(void)
{
	if (_Window == nullptr) {
		return(nullptr);
	}

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (SDL_GetWindowWMInfo(_Window, &info) == SDL_FALSE) {
		return(nullptr);
	}

#if defined(__APPLE__)
	return((void *)info.info.cocoa.window);
#elif defined(SDL_VIDEO_DRIVER_X11)
	return((void *)(uintptr_t)info.info.x11.window);
#else
	return(nullptr);
#endif
}


void * Host_Native_Display_Handle(void)
{
#if defined(SDL_VIDEO_DRIVER_X11)
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (_Window != nullptr && SDL_GetWindowWMInfo(_Window, &info) == SDL_TRUE) {
		return((void *)info.info.x11.display);
	}
#endif
	return(nullptr);
}


/*
 * SDL_WINDOW_FULLSCREEN_DESKTOP rather than a real mode change: it keeps the desktop's
 * resolution and covers it, which is what the engine already means by fullscreen -- see
 * the WS_POPUP branch of Create_Main_Window. A mode change would also cost every other
 * window on the display its layout.
 */
void Browser_Set_Window_Mode(bool fullscreen, int width, int height)
{
	if (_Window == nullptr) {
		return;
	}

	if (width > 0 && height > 0) {
		_WindowedWidth = width;
		_WindowedHeight = height;
	}

	if (fullscreen == _Fullscreen) {
		if (!fullscreen) {
			SDL_SetWindowSize(_Window, _WindowedWidth, _WindowedHeight);
		}
		return;
	}

	if (SDL_SetWindowFullscreen(_Window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
		fprintf(stderr, "OpenTS: the window mode could not be changed: %s\n", SDL_GetError());
		return;
	}

	_Fullscreen = fullscreen;

	if (!fullscreen) {
		SDL_SetWindowSize(_Window, _WindowedWidth, _WindowedHeight);
		SDL_SetWindowPosition(_Window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
}


void Browser_Service(void)
{
	if (!_Initialized) {
		return;
	}

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				// The close box asks the process to end; the engine has no other ear for it.
				exit(EXIT_SUCCESS);
				break;

			case SDL_KEYDOWN:
			case SDL_KEYUP:
				// Alt+Enter is the host's, as the close box is. Swallowing it keeps the
				// engine from also reading a Return while the window is changing mode.
				if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT) != 0) {
					if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
						Browser_Set_Window_Mode(!_Fullscreen, 0, 0);
					}
					break;
				}
				Handle_Key(event.key);
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				Handle_Mouse_Button(event.button);
				break;

			case SDL_MOUSEMOTION:
				_MouseHovering = true;
				Point_To_Game(event.motion.x, event.motion.y, _MouseX, _MouseY);
				break;

			default:
				break;
		}
	}

	if (Measure_Window()) {
		if (MainWindow != NULL) {
			MoveWindow(MainWindow, 0, 0, _CanvasWidth, _CanvasHeight, FALSE);
		}

		Video_On_Resize(_CanvasWidth, _CanvasHeight);
		Video_Request_Frame_Size(_CanvasCSSWidth, _CanvasCSSHeight);
	}

	GameInFocus = ((SDL_GetWindowFlags(_Window) & SDL_WINDOW_MINIMIZED) == 0);

	// The multimedia timers the engine arms have no thread of their own here, so they
	// fire on the host's pump, which every wait in the engine comes through.
	Win32_Timer_Service();

	while (_EventHead != _EventTail) {
		HostEvent const queued = _Events[_EventHead];
		_EventHead = (_EventHead + 1) % EVENT_QUEUE_SIZE;

		if (_EventHook != nullptr && _EventHook(queued.Key, queued.X, queued.Y, queued.IsMouse, queued.IsRelease)) {
			continue;
		}

		if (Keyboard != nullptr) {
			if (queued.IsMouse) {
				Keyboard->Post_Mouse_Event(queued.Key, queued.X, queued.Y, queued.IsRelease);
			} else {
				Keyboard->Post_Key_Event(queued.Key, queued.IsRelease);
			}
		}
	}
}


void Browser_Set_Event_Hook(BrowserEventHook hook)
{
	_EventHook = hook;
}


void Browser_Yield(void)
{
	_BlockingWaits++;
	Browser_Service();
	SDL_Delay(1);
	_FrameSerial++;
	_LastYield = SDL_GetTicks();
}


bool Browser_Yield_If_Due(void)
{
	// A frame's worth of time between yields keeps a polling loop from busying a core
	// without making any single wait noticeable.
	if ((SDL_GetTicks() - _LastYield) < 16) {
		return(false);
	}

	Browser_Yield();
	return(true);
}


bool Browser_Yield_Is_Available(void)
{
	return(true);
}


unsigned int Browser_Blocking_Wait_Count(void)
{
	return(_BlockingWaits);
}


unsigned int Browser_Frame_Serial(void)
{
	return(_FrameSerial);
}


BrowserDisplayPolicy Browser_Display_Policy(void)
{
	return(BROWSER_DISPLAY_NATIVE);
}


// A nonzero size here asks for a fixed frame that stops following the window,
// so no override is reported.
int Browser_Display_Width(void)
{
	return(0);
}


int Browser_Display_Height(void)
{
	return(0);
}


char const * Browser_Canvas_Selector(void)
{
	return("");
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
	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
		return(mode.w);
	}
	return(_CanvasCSSWidth);
}


int Browser_Screen_Height(void)
{
	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
		return(mode.h);
	}
	return(_CanvasCSSHeight);
}


bool Browser_Is_Hidden(void)
{
	if (_Window == nullptr) {
		return(false);
	}
	return((SDL_GetWindowFlags(_Window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0);
}


unsigned short Browser_Key_Modifiers(void)
{
	return(_Modifiers);
}


bool Browser_Key_Is_Down(unsigned short vk_key)
{
	return(_KeyDown[vk_key & 0xFF] != 0);
}


char Browser_Key_To_ASCII(unsigned short key)
{
	if ((key & WWKEY_SHIFT_BIT) != 0) {
		char shifted = _ShiftedAscii[key & 0xFF];
		if (shifted != '\0') {
			return(shifted);
		}
	}
	return(_Ascii[key & 0xFF]);
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


void Browser_Begin_Text_Input(void)
{
	SDL_StartTextInput();
}


void Browser_End_Text_Input(void)
{
	SDL_StopTextInput();
}


// The host's mouse position is already a frame position, so finding the cursor skips
// WWMouseClass's own conversion, as the browser host's mouse does.
class SDLMouseClass : public WWMouseClass
{
	public:
		SDLMouseClass(HWND window) : WWMouseClass(window) {}

		virtual int Get_Mouse_X(void) const override {return(Browser_Mouse_X());}
		virtual int Get_Mouse_Y(void) const override {return(Browser_Mouse_Y());}
		virtual Point2D Get_Mouse_Point(void) const override {return(Point2D(Browser_Mouse_X(), Browser_Mouse_Y()));}
		virtual bool Is_Hovering(void) const override {return(Browser_Mouse_Is_Hovering());}
};


Mouse * Browser_Create_Mouse(HWND window)
{
	return(new SDLMouseClass(window));
}


void Host_Apply_Cursor(unsigned int const * pixels, int width, int height, int hotx, int hoty)
{
	static SDL_Cursor * applied = nullptr;

	if (pixels == nullptr || width <= 0 || height <= 0) {
		SDL_SetCursor(SDL_GetDefaultCursor());
		if (applied != nullptr) {
			SDL_FreeCursor(applied);
			applied = nullptr;
		}
		return;
	}

	SDL_Surface * surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
	if (surface == nullptr) {
		return;
	}

	memcpy(surface->pixels, pixels, (size_t)width * (size_t)height * 4);

	SDL_Cursor * cursor = SDL_CreateColorCursor(surface, hotx, hoty);
	SDL_FreeSurface(surface);

	if (cursor != nullptr) {
		SDL_SetCursor(cursor);
		if (applied != nullptr) {
			SDL_FreeCursor(applied);
		}
		applied = cursor;
	}
}


int main(int argc, char ** argv)
{
	static char command_line[1024];

	command_line[0] = '\0';
	for (int index = 1; index < argc; index++) {
		if (command_line[0] != '\0') {
			strncat(command_line, " ", sizeof(command_line) - strlen(command_line) - 1);
		}
		strncat(command_line, argv[index], sizeof(command_line) - strlen(command_line) - 1);
	}

	if (!Browser_Init()) {
		return(EXIT_FAILURE);
	}

	return(WinMain(NULL, NULL, command_line, SW_SHOWNORMAL));
}

#endif
