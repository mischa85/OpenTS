/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The stock control classes. win32ctrl.h states what they stand in for; what follows is
// why they are shaped the way they are.
//
// These are models, not painters. ownrdraw.cpp displaces each control's class procedure
// with one of its own and draws the control itself onto the engine's surfaces, but it
// keeps calling the displaced procedure to ask what to draw: where a trackbar's thumb
// belongs, whether a check box is checked, what a list box's rows say. So each class here
// answers the messages that carry that state and paints nothing.
//
// Two consequences are worth stating.
//
// A control still has to report its own state changes. A press is the clearest case: an
// owner-draw button that goes down tells its parent so with WM_DRAWITEM, exactly as
// Windows does, and it is that message -- not the press -- that ends up redrawing the
// button pressed. Nothing here would be seen at all if the notifications were left out.
//
// A control that draws nothing still owns its geometry. WM_NCHITTEST is answered the way
// Windows answers it, so a static caption or a group box frame lets a click through to
// whatever sits under it, and a button does not.

#include "always.h"

#include "win32ctrl.h"

#if !defined(_WIN32)

#include "keyboard.h"
#include "win32user.h"

#include <cstring>
#include <string>
#include <vector>


/*
** Where a control keeps the pointer to its state, in the window extra bytes. The first
** thirty-two bytes of window extra are reserved for the dialog words on every window, so
** the pointer sits above them.
*/
static int const CONTROL_STATE_OFFSET = 32;

// win32compat.h names none of these. BST_PUSHED is the BM_GETSTATE bit that says the
// button is held down; STN_CLICKED and DLGC_STATIC are what a static reports and answers.
static UINT const BST_PUSHED_STATE = 0x0004;
static UINT const STN_CLICKED_CODE = 0;
static UINT const DLGC_STATIC_CODE = 0x0100;

// What a list box row is worth before anything sets a height. Windows measures the font;
// nothing here has one, and ownrdraw.cpp overwrites this from its own font on subclassing.
static int const DEFAULT_ITEM_HEIGHT = 13;

/*
** How much frame a combo box's closed box carries above and below the line of text in it.
** Windows measures this out of the system metrics; the front end paints its own frame and
** this is what it paints.
*/
static int const COMBO_BOX_FRAME = 4;


/*
** One control's state. The classes differ in which fields they use rather than in shape,
** because a dialog holds few enough controls that a per class allocation would buy
** nothing and a union would only hide which class owns what.
*/
struct ControlState
{
	int Check;
	bool Pressed;
	bool Tracking;

	int Minimum;
	int Maximum;
	int Position;
	int LineSize;
	int PageSize;

	std::vector<std::string> Items;
	std::vector<LONG_PTR> ItemData;
	std::vector<char> Selected;
	int CurrentSelection;
	int TopIndex;
	int ItemHeight;
	int DroppedHeight;
	bool Dropped;

	int SelectionStart;
	int SelectionEnd;
	int TextLimit;

	WORD HotKey;
};


static ControlState * State_Of(HWND window)
{
	return((ControlState *)GetWindowLongPtrA(window, CONTROL_STATE_OFFSET));
}


/// <summary>
/// Builds a control's state, or tears it down with the control.
/// </summary>
/// <returns>bool; true when the message was one of the two this handles.</returns>
static bool Manage_State(HWND window, UINT message)
{
	if (message == WM_CREATE) {
		ControlState * state = new ControlState;
		state->Check = 0;
		state->Pressed = false;
		state->Tracking = false;
		state->Minimum = 0;
		state->Maximum = 100;
		state->Position = 0;
		state->LineSize = 1;
		state->PageSize = 10;
		state->CurrentSelection = -1;
		state->TopIndex = 0;
		state->ItemHeight = DEFAULT_ITEM_HEIGHT;
		state->DroppedHeight = 0;
		state->Dropped = false;
		state->SelectionStart = 0;
		state->SelectionEnd = 0;
		state->TextLimit = 0;
		state->HotKey = 0;

		SetWindowLongPtrA(window, CONTROL_STATE_OFFSET, (LONG_PTR)state);
		return(true);
	}

	if (message == WM_NCDESTROY) {
		delete State_Of(window);
		SetWindowLongPtrA(window, CONTROL_STATE_OFFSET, 0);
		return(true);
	}

	return(false);
}


/// <summary>
/// Tells the parent that something happened to one of its controls.
/// </summary>
static void Notify_Parent(HWND window, int code)
{
	HWND parent = GetParent(window);
	if (parent == nullptr) {
		return;
	}

	int id = GetDlgCtrlID(window);
	SendMessageA(parent, WM_COMMAND, MAKEWPARAM((WORD)id, (WORD)code), (LPARAM)window);
}


static bool Point_In_Client(HWND window, LPARAM lparam)
{
	RECT client;
	GetClientRect(window, &client);

	POINT point;
	point.x = (short)LOWORD(lparam);
	point.y = (short)HIWORD(lparam);
	return(PtInRect(&client, point) != FALSE);
}


/*
** ---------------------------------------------------------------------------------------
** The button.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// Asks the parent to redraw an owner-draw button in the state it now stands in.
/// </summary>
/// <remarks>
/// This is the whole of the button's drawing on this target. ownrdraw.cpp paints the
/// button out of its own artwork when the parent hands the item back down, and the state
/// carried here -- pressed, disabled, focused -- is what picks the artwork.
/// </remarks>
static void Draw_Owner_Draw_Button(HWND window, ControlState const * state)
{
	HWND parent = GetParent(window);
	if (parent == nullptr) {
		return;
	}

	DRAWITEMSTRUCT draw;
	memset(&draw, 0, sizeof(draw));

	draw.CtlType = ODT_BUTTON;
	draw.CtlID = (UINT)GetDlgCtrlID(window);
	draw.itemID = 0;
	draw.itemAction = ODA_DRAWENTIRE;
	draw.itemState = 0;
	draw.hwndItem = window;
	draw.hDC = nullptr;
	GetClientRect(window, &draw.rcItem);

	if (state->Pressed) draw.itemState |= ODS_SELECTED;
	if (!IsWindowEnabled(window)) draw.itemState |= ODS_DISABLED;
	if (GetFocus() == window) draw.itemState |= ODS_FOCUS;

	SendMessageA(parent, WM_DRAWITEM, (WPARAM)draw.CtlID, (LPARAM)&draw);
}


/// <summary>
/// Marks a button for repainting, by whichever route its style calls for.
/// </summary>
static void Refresh_Button(HWND window, ControlState const * state, LONG style)
{
	if ((style & 0x0F) == BS_OWNERDRAW) {
		Draw_Owner_Draw_Button(window, state);
	} else {
		InvalidateRect(window, nullptr, TRUE);
	}
}


/// <summary>
/// Applies the click a button has just taken.
/// The automatic styles carry their own state, so the check box toggles here before the
/// parent is told; the plain styles only report.
/// </summary>
static void Click_Button(HWND window, ControlState * state, LONG style)
{
	switch (style & 0x0F) {
		case BS_AUTOCHECKBOX:
			state->Check = (state->Check != 0) ? BST_UNCHECKED : BST_CHECKED;
			break;

		case BS_AUTORADIOBUTTON: {
			/*
			** A radio button in a group turns the rest of the group off. Group boundaries
			** are not tracked here: no template in the shipped resources holds a radio
			** button, so the siblings of one are the whole of its group.
			*/
			HWND parent = GetParent(window);
			for (HWND sibling = GetTopWindow(parent); sibling != nullptr; sibling = GetWindow(sibling, GW_HWNDNEXT)) {
				if (sibling == window) {
					continue;
				}
				if ((GetWindowLongA(sibling, GWL_STYLE) & 0x0F) != BS_AUTORADIOBUTTON) {
					continue;
				}
				SendMessageA(sibling, BM_SETCHECK, BST_UNCHECKED, 0);
			}
			state->Check = BST_CHECKED;
			break;
		}

		default:
			break;
	}

	Refresh_Button(window, state, style);
	Notify_Parent(window, BN_CLICKED);
}


static LRESULT CALLBACK Button_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	LONG style = GetWindowLongA(window, GWL_STYLE);
	bool frame = ((style & 0x0F) == BS_GROUPBOX);

	switch (message) {
		case WM_NCHITTEST:
			/*
			** A group box is a frame drawn around other controls, so a click on it belongs
			** to whatever it frames.
			*/
			return(frame ? HTTRANSPARENT : HTCLIENT);

		case WM_GETDLGCODE:
			return(((style & 0x0F) == BS_DEFPUSHBUTTON) ? (DLGC_BUTTON | DLGC_DEFPUSHBUTTON) : DLGC_BUTTON);

		case BM_GETCHECK:
			return(state->Check);

		case BM_SETCHECK:
			if (state->Check != (int)wparam) {
				state->Check = (int)wparam;
				Refresh_Button(window, state, style);
			}
			return(0);

		case BM_GETSTATE:
			return(state->Check | (state->Pressed ? BST_PUSHED_STATE : 0));

		case BM_SETSTATE: {
			bool pressed = (wparam != 0);
			if (pressed != state->Pressed) {
				state->Pressed = pressed;
				Refresh_Button(window, state, style);
			}
			return(0);
		}

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			if (frame || !IsWindowEnabled(window)) {
				return(0);
			}
			SetFocus(window);
			SetCapture(window);
			state->Tracking = true;
			state->Pressed = true;
			Refresh_Button(window, state, style);
			return(0);

		case WM_MOUSEMOVE: {
			if (!state->Tracking) {
				return(0);
			}
			bool inside = Point_In_Client(window, lparam);
			if (inside != state->Pressed) {
				state->Pressed = inside;
				Refresh_Button(window, state, style);
			}
			return(0);
		}

		case WM_LBUTTONUP: {
			if (!state->Tracking) {
				return(0);
			}
			state->Tracking = false;
			ReleaseCapture();

			bool clicked = state->Pressed;
			state->Pressed = false;

			if (clicked) {
				Click_Button(window, state, style);
			} else {
				Refresh_Button(window, state, style);
			}
			return(0);
		}

		case WM_ENABLE:
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
			Refresh_Button(window, state, style);
			return(0);

		case WM_SETTEXT: {
			LRESULT result = DefWindowProcA(window, message, wparam, lparam);
			InvalidateRect(window, nullptr, TRUE);
			return(result);
		}

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			if ((style & 0x0F) == BS_OWNERDRAW) {
				Draw_Owner_Draw_Button(window, state);
			}
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The static.
** ---------------------------------------------------------------------------------------
*/


static LRESULT CALLBACK Static_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	LONG style = GetWindowLongA(window, GWL_STYLE);

	switch (message) {
		case WM_NCHITTEST:
			// A caption is scenery unless it was asked to report clicks.
			return(((style & SS_NOTIFY) != 0) ? HTCLIENT : HTTRANSPARENT);

		case WM_GETDLGCODE:
			return(DLGC_STATIC_CODE);

		case WM_LBUTTONDOWN:
			if ((style & SS_NOTIFY) != 0) {
				Notify_Parent(window, STN_CLICKED_CODE);
			}
			return(0);

		case WM_SETTEXT: {
			LRESULT result = DefWindowProcA(window, message, wparam, lparam);
			InvalidateRect(window, nullptr, TRUE);
			return(result);
		}

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The edit box.
** ---------------------------------------------------------------------------------------
*/


static void Clamp_Selection(HWND window, ControlState * state)
{
	int length = (int)SendMessageA(window, WM_GETTEXTLENGTH, 0, 0);

	if (state->SelectionStart > length) state->SelectionStart = length;
	if (state->SelectionEnd > length) state->SelectionEnd = length;
	if (state->SelectionStart < 0) state->SelectionStart = 0;
	if (state->SelectionEnd < state->SelectionStart) state->SelectionEnd = state->SelectionStart;
}


/// <summary>
/// Replaces the selected run of an edit box with the text supplied.
/// </summary>
static void Replace_Selection(HWND window, ControlState * state, char const * insert)
{
	char buffer[1024];
	buffer[0] = '\0';
	GetWindowTextA(window, buffer, sizeof(buffer));

	std::string text = buffer;
	Clamp_Selection(window, state);

	text.erase((size_t)state->SelectionStart, (size_t)(state->SelectionEnd - state->SelectionStart));

	std::string added = (insert != nullptr) ? insert : "";
	if (state->TextLimit > 0 && (int)(text.size() + added.size()) > state->TextLimit) {
		size_t room = (size_t)state->TextLimit - text.size();
		added.resize(std::min(added.size(), room));
	}

	text.insert((size_t)state->SelectionStart, added);

	state->SelectionStart += (int)added.size();
	state->SelectionEnd = state->SelectionStart;

	DefWindowProcA(window, WM_SETTEXT, 0, (LPARAM)text.c_str());
	InvalidateRect(window, nullptr, TRUE);
	Notify_Parent(window, EN_CHANGE);
}


static LRESULT CALLBACK Edit_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	switch (message) {
		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_GETDLGCODE:
			return(DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL);

		case EM_SETSEL:
			state->SelectionStart = (int)wparam;
			state->SelectionEnd = (int)lparam;
			if (state->SelectionStart < 0 || state->SelectionEnd < 0) {
				state->SelectionStart = (int)SendMessageA(window, WM_GETTEXTLENGTH, 0, 0);
				state->SelectionEnd = state->SelectionStart;
			}
			Clamp_Selection(window, state);
			return(1);

		case EM_GETSEL:
			Clamp_Selection(window, state);
			if (wparam != 0) *(DWORD *)wparam = (DWORD)state->SelectionStart;
			if (lparam != 0) *(DWORD *)lparam = (DWORD)state->SelectionEnd;
			return(MAKELONG(state->SelectionStart, state->SelectionEnd));

		case EM_LIMITTEXT:
			state->TextLimit = (int)wparam;
			return(0);

		case EM_REPLACESEL:
			Replace_Selection(window, state, (char const *)lparam);
			return(0);

		case WM_SETTEXT: {
			LRESULT result = DefWindowProcA(window, message, wparam, lparam);
			state->SelectionStart = (int)SendMessageA(window, WM_GETTEXTLENGTH, 0, 0);
			state->SelectionEnd = state->SelectionStart;
			InvalidateRect(window, nullptr, TRUE);
			return(result);
		}

		case WM_CHAR: {
			char character = (char)wparam;

			if (character == '\b') {
				Clamp_Selection(window, state);
				if (state->SelectionStart == state->SelectionEnd && state->SelectionStart > 0) {
					state->SelectionStart--;
				}
				Replace_Selection(window, state, "");
				return(0);
			}

			if (character < ' ') {
				return(0);
			}

			char insert[2] = { character, '\0' };
			Replace_Selection(window, state, insert);
			return(0);
		}

		case WM_KEYDOWN: {
			int length = (int)SendMessageA(window, WM_GETTEXTLENGTH, 0, 0);

			switch (wparam) {
				case VK_LEFT:
					if (state->SelectionStart > 0) state->SelectionStart--;
					state->SelectionEnd = state->SelectionStart;
					break;

				case VK_RIGHT:
					if (state->SelectionStart < length) state->SelectionStart++;
					state->SelectionEnd = state->SelectionStart;
					break;

				case VK_HOME:
					state->SelectionStart = 0;
					state->SelectionEnd = 0;
					break;

				case VK_END:
					state->SelectionStart = length;
					state->SelectionEnd = length;
					break;

				case VK_DELETE:
					Clamp_Selection(window, state);
					if (state->SelectionStart == state->SelectionEnd && state->SelectionStart < length) {
						state->SelectionEnd = state->SelectionStart + 1;
					}
					Replace_Selection(window, state, "");
					return(0);

				default:
					return(0);
			}

			InvalidateRect(window, nullptr, TRUE);
			return(0);
		}

		case WM_LBUTTONDOWN:
			SetFocus(window);
			return(0);

		case WM_SETFOCUS:
			Notify_Parent(window, EN_SETFOCUS);
			InvalidateRect(window, nullptr, TRUE);
			return(0);

		case WM_KILLFOCUS:
			Notify_Parent(window, EN_KILLFOCUS);
			InvalidateRect(window, nullptr, TRUE);
			return(0);

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The item list behind the list box and the combo box.
** ---------------------------------------------------------------------------------------
*/


static int Insert_Item(ControlState * state, int index, char const * text)
{
	if (index < 0 || index > (int)state->Items.size()) {
		index = (int)state->Items.size();
	}

	state->Items.insert(state->Items.begin() + index, (text != nullptr) ? text : "");
	state->ItemData.insert(state->ItemData.begin() + index, 0);
	state->Selected.insert(state->Selected.begin() + index, 0);

	if (state->CurrentSelection >= index) {
		state->CurrentSelection++;
	}

	return(index);
}


static LRESULT Delete_Item(ControlState * state, int index)
{
	if (index < 0 || index >= (int)state->Items.size()) {
		return(LB_ERR);
	}

	state->Items.erase(state->Items.begin() + index);
	state->ItemData.erase(state->ItemData.begin() + index);
	state->Selected.erase(state->Selected.begin() + index);

	if (state->CurrentSelection == index) {
		state->CurrentSelection = -1;
	} else if (state->CurrentSelection > index) {
		state->CurrentSelection--;
	}

	return((LRESULT)state->Items.size());
}


static void Reset_Items(ControlState * state)
{
	state->Items.clear();
	state->ItemData.clear();
	state->Selected.clear();
	state->CurrentSelection = -1;
	state->TopIndex = 0;
}


/// <summary>
/// Copies one item's text into a caller's buffer.
/// </summary>
/// <returns>Returns with the length written, or LB_ERR when there is no such item.</returns>
static LRESULT Fetch_Item_Text(ControlState const * state, int index, char * buffer)
{
	if (index < 0 || index >= (int)state->Items.size() || buffer == nullptr) {
		return(LB_ERR);
	}

	std::string const & text = state->Items[(size_t)index];
	memcpy(buffer, text.c_str(), text.size() + 1);
	return((LRESULT)text.size());
}


/// <summary>
/// Finds the first item at or after a starting point whose text matches.
/// </summary>
/// <param name="exact">Must the whole text match, or only its beginning?</param>
/// <returns>Returns with the item index, or LB_ERR when nothing matched.</returns>
static LRESULT Find_Item(ControlState const * state, int after, char const * text, bool exact)
{
	int count = (int)state->Items.size();
	if (count == 0 || text == nullptr) {
		return(LB_ERR);
	}

	size_t length = strlen(text);

	for (int step = 0; step < count; step++) {
		int index = ((after + 1 + step) % count + count) % count;
		std::string const & item = state->Items[(size_t)index];

		if (exact) {
			if (item.size() == length && strncasecmp(item.c_str(), text, length) == 0) {
				return(index);
			}
		} else if (item.size() >= length && strncasecmp(item.c_str(), text, length) == 0) {
			return(index);
		}
	}

	return(LB_ERR);
}


/*
** ---------------------------------------------------------------------------------------
** The list box.
** ---------------------------------------------------------------------------------------
*/


static LRESULT CALLBACK List_Box_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	int count = (int)state->Items.size();

	switch (message) {
		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_GETDLGCODE:
			return(DLGC_WANTARROWS);

		case LB_ADDSTRING:
			return(Insert_Item(state, count, (char const *)lparam));

		case LB_INSERTSTRING:
			return(Insert_Item(state, (int)wparam, (char const *)lparam));

		case LB_DELETESTRING:
			return(Delete_Item(state, (int)wparam));

		case LB_RESETCONTENT:
			Reset_Items(state);
			return(0);

		case LB_GETCOUNT:
			return(count);

		case LB_SETCURSEL:
			if ((int)wparam >= count) {
				return(LB_ERR);
			}
			state->CurrentSelection = ((int)wparam < 0) ? -1 : (int)wparam;
			for (int index = 0; index < count; index++) {
				state->Selected[(size_t)index] = (index == state->CurrentSelection) ? 1 : 0;
			}
			InvalidateRect(window, nullptr, TRUE);
			return(state->CurrentSelection);

		case LB_GETCURSEL:
			return((count > 0) ? state->CurrentSelection : LB_ERR);

		case LB_SETSEL: {
			int index = (int)lparam;
			if (index < 0) {
				for (int scan = 0; scan < count; scan++) {
					state->Selected[(size_t)scan] = (wparam != 0) ? 1 : 0;
				}
			} else if (index < count) {
				state->Selected[(size_t)index] = (wparam != 0) ? 1 : 0;
				if (wparam != 0) {
					state->CurrentSelection = index;
				}
			} else {
				return(LB_ERR);
			}
			InvalidateRect(window, nullptr, TRUE);
			return(0);
		}

		case LB_GETSEL:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(LB_ERR);
			}
			return(state->Selected[wparam] != 0);

		case LB_SELITEMRANGE: {
			int first = (int)LOWORD(lparam);
			int last = (int)HIWORD(lparam);
			for (int index = first; index <= last && index < count; index++) {
				if (index >= 0) {
					state->Selected[(size_t)index] = (wparam != 0) ? 1 : 0;
				}
			}
			return(0);
		}

		case LB_GETSELCOUNT: {
			int selected = 0;
			for (int index = 0; index < count; index++) {
				if (state->Selected[(size_t)index] != 0) selected++;
			}
			return(selected);
		}

		case LB_GETSELITEMS: {
			int * buffer = (int *)lparam;
			int written = 0;
			for (int index = 0; index < count && written < (int)wparam; index++) {
				if (state->Selected[(size_t)index] != 0) {
					buffer[written++] = index;
				}
			}
			return(written);
		}

		case LB_GETTEXT:
			return(Fetch_Item_Text(state, (int)wparam, (char *)lparam));

		case LB_GETTEXTLEN:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(LB_ERR);
			}
			return((LRESULT)state->Items[wparam].size());

		case LB_FINDSTRING:
			return(Find_Item(state, (int)wparam, (char const *)lparam, false));

		case LB_SELECTSTRING: {
			LRESULT found = Find_Item(state, (int)wparam, (char const *)lparam, false);
			if (found != LB_ERR) {
				SendMessageA(window, LB_SETCURSEL, (WPARAM)found, 0);
			}
			return(found);
		}

		case LB_SETITEMDATA:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(LB_ERR);
			}
			state->ItemData[wparam] = lparam;
			return(0);

		case LB_GETITEMDATA:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(LB_ERR);
			}
			return(state->ItemData[wparam]);

		case LB_GETTOPINDEX:
			return(state->TopIndex);

		case LB_SETTOPINDEX:
			state->TopIndex = (int)wparam;
			if (state->TopIndex < 0) state->TopIndex = 0;
			if (state->TopIndex > count) state->TopIndex = count;
			InvalidateRect(window, nullptr, TRUE);
			return(0);

		case LB_GETITEMHEIGHT:
			return(state->ItemHeight);

		case LB_SETITEMHEIGHT:
			state->ItemHeight = (int)lparam;
			return(0);

		case LB_GETITEMRECT: {
			RECT * rect = (RECT *)lparam;
			if (rect == nullptr || (int)wparam < 0 || (int)wparam >= count) {
				return(LB_ERR);
			}
			RECT client;
			GetClientRect(window, &client);
			rect->left = client.left;
			rect->right = client.right;
			rect->top = client.top + ((int)wparam - state->TopIndex) * state->ItemHeight;
			rect->bottom = rect->top + state->ItemHeight;
			return(0);
		}

		case WM_LBUTTONDOWN: {
			SetFocus(window);
			int index = state->TopIndex + (int)(short)HIWORD(lparam) / std::max(state->ItemHeight, 1);
			if (index >= 0 && index < count) {
				SendMessageA(window, LB_SETCURSEL, (WPARAM)index, 0);
				Notify_Parent(window, LBN_SELCHANGE);
			}
			return(0);
		}

		case WM_LBUTTONDBLCLK:
			Notify_Parent(window, LBN_DBLCLK);
			return(0);

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The combo box.
** ---------------------------------------------------------------------------------------
*/


/// <summary>
/// How tall a combo box's closed box is.
/// </summary>
/// <remarks>
/// The box holds one line of the dialog font, which the dialog base units measure because
/// they are this target's stand-in for the system font, plus the frame around it.
/// </remarks>
static int Combo_Box_Closed_Height(void)
{
	return((int)HIWORD(GetDialogBaseUnits()) + 2 * COMBO_BOX_FRAME);
}


/// <summary>
/// Takes the dropped height out of a combo box's window and leaves the closed box.
/// </summary>
/// <remarks>
/// A template sizes a combo box to the box plus the list it drops, and Windows keeps only
/// the box as the window: the height asked for is remembered and reported back through
/// CB_GETDROPPEDCONTROLRECT instead. The front end depends on both halves of that -- it
/// drops its list one client height below the box, and windlg.cpp's layout pass reads the
/// dropped rectangle rather than the window rectangle for exactly this reason.
/// </remarks>
static void Size_Combo_Box(HWND window, int width, int height)
{
	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return;
	}

	int closed = Combo_Box_Closed_Height();

	state->DroppedHeight = (height > closed) ? height : closed;

	if (height != closed) {
		SetWindowPos(window, nullptr, 0, 0, width, closed, SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
	}
}


static LRESULT CALLBACK Combo_Box_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		if (message == WM_CREATE) {
			CREATESTRUCTA const * create = (CREATESTRUCTA const *)lparam;
			if (create != nullptr) {
				Size_Combo_Box(window, create->cx, create->cy);
			}
		}
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	int count = (int)state->Items.size();

	/*
	** A list style combo box has no edit field, so its window text is whatever it has
	** selected. ownrdraw.cpp paints the closed box out of GetWindowText and would otherwise
	** paint an empty box over a live selection.
	*/
	bool listonly = ((GetWindowLongA(window, GWL_STYLE) & 3) == CBS_DROPDOWNLIST);

	switch (message) {
		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_GETDLGCODE:
			return(DLGC_WANTARROWS);

		case WM_GETTEXT:
			if (listonly) {
				char * buffer = (char *)lparam;
				if (buffer == nullptr || (int)wparam <= 0) {
					return(0);
				}
				buffer[0] = '\0';
				if (state->CurrentSelection < 0 || state->CurrentSelection >= count) {
					return(0);
				}
				std::string const & text = state->Items[state->CurrentSelection];
				int length = (int)text.size();
				if (length > (int)wparam - 1) {
					length = (int)wparam - 1;
				}
				memcpy(buffer, text.c_str(), (size_t)length);
				buffer[length] = '\0';
				return(length);
			}
			return(DefWindowProcA(window, message, wparam, lparam));

		case WM_GETTEXTLENGTH:
			if (listonly) {
				if (state->CurrentSelection < 0 || state->CurrentSelection >= count) {
					return(0);
				}
				return((LRESULT)state->Items[state->CurrentSelection].size());
			}
			return(DefWindowProcA(window, message, wparam, lparam));

		case WM_SETTEXT:
			// Windows selects the matching item rather than keeping text a list cannot show.
			if (listonly) {
				return(SendMessageA(window, CB_SELECTSTRING, (WPARAM)-1, lparam) != CB_ERR ? TRUE : FALSE);
			}
			return(DefWindowProcA(window, message, wparam, lparam));

		case CB_ADDSTRING:
			return(Insert_Item(state, count, (char const *)lparam));

		case CB_INSERTSTRING:
			return(Insert_Item(state, (int)wparam, (char const *)lparam));

		case CB_DELETESTRING:
			return(Delete_Item(state, (int)wparam));

		case CB_RESETCONTENT:
			Reset_Items(state);
			InvalidateRect(window, nullptr, TRUE);
			return(0);

		case CB_GETCOUNT:
			return(count);

		case CB_SETCURSEL:
			if ((int)wparam >= count) {
				return(CB_ERR);
			}
			state->CurrentSelection = ((int)wparam < 0) ? -1 : (int)wparam;
			InvalidateRect(window, nullptr, TRUE);
			return(state->CurrentSelection);

		case CB_GETCURSEL:
			return((state->CurrentSelection >= 0) ? state->CurrentSelection : CB_ERR);

		case CB_GETLBTEXT:
			return(Fetch_Item_Text(state, (int)wparam, (char *)lparam));

		case CB_GETLBTEXTLEN:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(CB_ERR);
			}
			return((LRESULT)state->Items[wparam].size());

		case CB_FINDSTRING:
			return(Find_Item(state, (int)wparam, (char const *)lparam, false));

		case CB_SELECTSTRING: {
			LRESULT found = Find_Item(state, (int)wparam, (char const *)lparam, false);
			if (found != CB_ERR) {
				SendMessageA(window, CB_SETCURSEL, (WPARAM)found, 0);
			}
			return(found);
		}

		case CB_SETITEMDATA:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(CB_ERR);
			}
			state->ItemData[wparam] = lparam;
			return(0);

		case CB_GETITEMDATA:
			if ((int)wparam < 0 || (int)wparam >= count) {
				return(CB_ERR);
			}
			return(state->ItemData[wparam]);

		case CB_GETTOPINDEX:
			return(state->TopIndex);

		case CB_SETTOPINDEX:
			state->TopIndex = (int)wparam;
			return(0);

		case CB_GETITEMHEIGHT:
			return(state->ItemHeight);

		case CB_SETITEMHEIGHT:
			/*
			** Windows measures the closed box and the dropped rows separately, and the
			** front end only ever asks the two to agree. One height serves both.
			*/
			state->ItemHeight = (int)lparam;
			return(0);

		case CB_LIMITTEXT:
			state->TextLimit = (int)wparam;
			return(0);

		case CB_SHOWDROPDOWN:
			state->Dropped = (wparam != 0);
			return(0);

		case CB_GETDROPPEDSTATE:
			return(state->Dropped ? TRUE : FALSE);

		case CB_GETDROPPEDCONTROLRECT: {
			// The box plus the list it drops, which is the height the template asked for.
			RECT * rect = (RECT *)lparam;
			if (rect != nullptr) {
				GetWindowRect(window, rect);
				if (state->DroppedHeight > 0) {
					rect->bottom = rect->top + state->DroppedHeight;
				}
			}
			return(0);
		}

		case WM_SIZE: {
			// A layout pass sizes the whole dropped rectangle, as it does on Windows. The
			// size this control gives itself back is not one of those.
			int height = (int)(short)HIWORD(lparam);
			if (height != Combo_Box_Closed_Height()) {
				Size_Combo_Box(window, (int)(short)LOWORD(lparam), height);
			}
			return(0);
		}

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The scroll bar, the track bar and the progress bar.
** ---------------------------------------------------------------------------------------
*/


static LRESULT CALLBACK Scroll_Bar_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	switch (message) {
		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_GETDLGCODE:
			return(DLGC_WANTARROWS);

		case SBM_SETPOS: {
			LRESULT previous = state->Position;
			state->Position = (int)wparam;
			InvalidateRect(window, nullptr, TRUE);
			return(previous);
		}

		case SBM_GETPOS:
			return(state->Position);

		case SBM_SETRANGE:
			state->Minimum = (int)wparam;
			state->Maximum = (int)lparam;
			return(0);

		case SBM_SETSCROLLINFO: {
			SCROLLINFO const * info = (SCROLLINFO const *)lparam;
			if (info != nullptr) {
				if ((info->fMask & SIF_RANGE) != 0) {
					state->Minimum = info->nMin;
					state->Maximum = info->nMax;
				}
				if ((info->fMask & SIF_POS) != 0) {
					state->Position = info->nPos;
				}
				if ((info->fMask & SIF_PAGE) != 0) {
					state->PageSize = (int)info->nPage;
				}
			}
			InvalidateRect(window, nullptr, TRUE);
			return(state->Position);
		}

		case SBM_GETSCROLLINFO: {
			SCROLLINFO * info = (SCROLLINFO *)lparam;
			if (info == nullptr) {
				return(FALSE);
			}
			info->nMin = state->Minimum;
			info->nMax = state->Maximum;
			info->nPage = (UINT)state->PageSize;
			info->nPos = state->Position;
			info->nTrackPos = state->Position;
			return(TRUE);
		}

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


static LRESULT CALLBACK Track_Bar_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	switch (message) {
		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_GETDLGCODE:
			return(DLGC_WANTARROWS);

		case TBM_GETPOS:
			return(state->Position);

		case TBM_GETRANGEMIN:
			return(state->Minimum);

		case TBM_GETRANGEMAX:
			return(state->Maximum);

		case TBM_SETPOS:
			state->Position = (int)lparam;
			if (state->Position < state->Minimum) state->Position = state->Minimum;
			if (state->Position > state->Maximum) state->Position = state->Maximum;
			if (wparam != 0) {
				InvalidateRect(window, nullptr, TRUE);
			}
			return(0);

		case TBM_SETRANGE:
			state->Minimum = (int)(short)LOWORD(lparam);
			state->Maximum = (int)(short)HIWORD(lparam);
			return(0);

		case TBM_SETRANGEMIN:
			state->Minimum = (int)lparam;
			return(0);

		case TBM_SETRANGEMAX:
			state->Maximum = (int)lparam;
			return(0);

		case TBM_SETLINESIZE: {
			LRESULT previous = state->LineSize;
			state->LineSize = (int)lparam;
			return(previous);
		}

		case TBM_SETPAGESIZE: {
			LRESULT previous = state->PageSize;
			state->PageSize = (int)lparam;
			return(previous);
		}

		case TBM_SETTICFREQ:
			// The engine draws its own slider, so the tick marks have nowhere to go.
			return(0);

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


static LRESULT CALLBACK Progress_Bar_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	switch (message) {
		case WM_NCHITTEST:
			return(HTTRANSPARENT);

		case PBM_SETRANGE:
			state->Minimum = (int)(short)LOWORD(lparam);
			state->Maximum = (int)(short)HIWORD(lparam);
			return(0);

		case PBM_SETRANGE32:
			state->Minimum = (int)wparam;
			state->Maximum = (int)lparam;
			return(0);

		case PBM_SETPOS: {
			LRESULT previous = state->Position;
			state->Position = (int)wparam;
			InvalidateRect(window, nullptr, TRUE);
			return(previous);
		}

		case PBM_DELTAPOS: {
			LRESULT previous = state->Position;
			state->Position += (int)wparam;
			InvalidateRect(window, nullptr, TRUE);
			return(previous);
		}

		case PBM_SETSTEP: {
			LRESULT previous = state->LineSize;
			state->LineSize = (int)wparam;
			return(previous);
		}

		case PBM_STEPIT: {
			LRESULT previous = state->Position;
			state->Position += state->LineSize;
			if (state->Position > state->Maximum) {
				state->Position = state->Minimum;
			}
			InvalidateRect(window, nullptr, TRUE);
			return(previous);
		}

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** The hot key box.
** ---------------------------------------------------------------------------------------
*/


static LRESULT CALLBACK Hot_Key_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (Manage_State(window, message)) {
		return(0);
	}

	ControlState * state = State_Of(window);
	if (state == nullptr) {
		return(DefWindowProcA(window, message, wparam, lparam));
	}

	switch (message) {
		case WM_NCHITTEST:
			return(HTCLIENT);

		case WM_GETDLGCODE:
			return(DLGC_WANTALLKEYS);

		case HKM_SETHOTKEY:
			state->HotKey = (WORD)wparam;
			InvalidateRect(window, nullptr, TRUE);
			return(0);

		case HKM_GETHOTKEY:
			return(state->HotKey);

		case WM_LBUTTONDOWN:
			SetFocus(window);
			return(0);

		case WM_ERASEBKGND:
			return(1);

		case WM_PAINT:
			ValidateRect(window, nullptr);
			return(0);

		default:
			return(DefWindowProcA(window, message, wparam, lparam));
	}
}


/*
** ---------------------------------------------------------------------------------------
** Registration.
** ---------------------------------------------------------------------------------------
*/


/*
** The stock classes, in the spelling GetClassName has to hand back. ownrdraw.cpp picks a
** control's paint procedure by comparing that name case sensitively against WC_BUTTON and
** its neighbors, so these are the names those macros carry rather than the uppercase forms
** a dialog template writes.
*/
struct StockClass
{
	char const * Name;
	WNDPROC Procedure;
};

static StockClass const _StockClasses[] = {
	{ "Button",				Button_Proc },
	{ "Static",				Static_Proc },
	{ "Edit",				Edit_Proc },
	{ "ListBox",			List_Box_Proc },
	{ "ComboBox",			Combo_Box_Proc },
	{ "ScrollBar",			Scroll_Bar_Proc },
	{ TRACKBAR_CLASS,		Track_Bar_Proc },
	{ PROGRESS_CLASS,		Progress_Bar_Proc },
	{ HOTKEY_CLASS,			Hot_Key_Proc },
};


void Win32_Register_Stock_Controls(void)
{
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	for (unsigned int index = 0; index < sizeof(_StockClasses) / sizeof(_StockClasses[0]); index++) {
		WNDCLASSA windowclass;
		memset(&windowclass, 0, sizeof(windowclass));

		windowclass.style = CS_DBLCLKS;
		windowclass.lpfnWndProc = _StockClasses[index].Procedure;
		windowclass.cbWndExtra = CONTROL_STATE_OFFSET + (int)sizeof(LONG_PTR);
		windowclass.lpszClassName = _StockClasses[index].Name;

		RegisterClassA(&windowclass);
	}
}


char const * Win32_Stock_Control_Class(unsigned int ordinal)
{
	switch (ordinal) {
		case 0x0080:	return("Button");
		case 0x0081:	return("Edit");
		case 0x0082:	return("Static");
		case 0x0083:	return("ListBox");
		case 0x0084:	return("ScrollBar");
		case 0x0085:	return("ComboBox");
		default:		return(nullptr);
	}
}


/*
** ---------------------------------------------------------------------------------------
** The image list.
** ---------------------------------------------------------------------------------------
*/


HIMAGELIST ImageList_Create(int, int, UINT, int, int) { return(WIN32_STUB((HIMAGELIST)nullptr)); }
BOOL ImageList_Destroy(HIMAGELIST) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_BeginDrag(HIMAGELIST, int, int, int) { return(WIN32_STUB(FALSE)); }
void ImageList_EndDrag(void) { WIN32_STUB_VOID(); }
BOOL ImageList_DragEnter(HWND, int, int) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_DragLeave(HWND) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_DragMove(int, int) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_DragShowNolock(BOOL) { return(WIN32_STUB(FALSE)); }

#endif	// __EMSCRIPTEN__
