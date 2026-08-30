/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "grphmenu.h"


class GraphicMenuItem
{

	public:
		GraphicMenuItem(int id = -1);
		virtual ~GraphicMenuItem(void);
		virtual bool Is_Input_Key(KeyNumType key);
		virtual bool Is_Mouse_Over(Point2D const & mouse);
		virtual void Action(MSEngine * engine);
		virtual void On_Selected_Change(bool);
		virtual void On_Enabled_Change(bool);
		virtual void On_Visible_Change(bool);

		void Set_Selected(bool selected);
		void Set_Enabled(bool enabled);
		void Set_Visible(bool visible);

		int Get_ID(void) const { return(ID); }
		bool Is_Visible(void) const { return(Visible); }
		void Set_Select_Sound(MSSfxEntry * sound) { SelectSound = sound; }

	protected:
		/*
		 * This is the identifier the menu reports when this item is the one chosen, and
		 * the value the menu matches against when it is told to enable or disable a
		 * choice. An item that stands for no choice at all carries -1.
		 */
		int ID;

		/*
		 * If the menu highlight is currently resting on this item, then this flag will be
		 * true. Only a real change is passed on through On_Selected_Change, so a derived
		 * item is never notified twice for the same state.
		 */
		bool Selected;

		/*
		 * If this item may be chosen, then this flag will be true. Choices that do not
		 * apply to the page being displayed are turned off rather than removed, so the
		 * menu keeps its shape from page to page.
		 */
		bool Enabled;

		/*
		 * If this item is part of the page at all, then this flag will be true. A hidden
		 * item is neither drawn nor offered to the mouse or the keyboard, so a page can
		 * drop a choice entirely rather than merely showing it unavailable.
		 */
		bool Visible;

		/*
		 * Pointer to the sound effect to play when this item is chosen, or NULL if it is
		 * to be chosen silently. The item takes ownership of the sound.
		 */
		MSSfxEntry * SelectSound;
};
