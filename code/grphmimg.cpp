/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "grphmimg.h"

#include "_surface.h"
#include "ccfile.h"
#include "ini.h"
#include "msanim.h"
#include "mschoice.h"


/// <summary>
/// Creates an image based menu item from its INI description.
/// This routine is used while a graphic menu is being loaded. The position and active
/// area given in the INI are specified relative to the menu backdrop, so the backdrop
/// offset is added to both before the item is created.
/// </summary>
/// <param name="name">The INI section that describes the item.</param>
/// <param name="image_size">The backdrop offset that the item coordinates are relative to.</param>
/// <returns>Returns with a pointer to the item created. If the section names no item
/// identifier, then NULL is returned.</returns>
GraphicMenuItem * GM_Read_Image_Item(const char * name, INIClass const & ini, MSEngine & engine, Point2D & image_size)
{
	int id = ini.Get_Int(name, "ID", -1);
	if (id == -1) {
		return(NULL);
	}

	Point2D origin(0,0);
	origin = ini.Get_Point(name, "Origin", origin);
	origin += image_size;

	Rect active_rect(0,0,0,0);
	active_rect = ini.Get_Rect(name, "ActiveRect", active_rect);
	active_rect += image_size;

	char image[256];
	char highlighted[256];
	char disabled[256];
	char highlight_sound[256];
	char select_vq[256];

	highlighted[0] = '\0';
	image[0] = '\0';

	ini.Get_String(name, "Image", "", image, sizeof(image));
	ini.Get_String(name, "Highlighted", "", highlighted, sizeof(highlighted));
	ini.Get_String(name, "Disabled", "", disabled, sizeof(disabled));
	ini.Get_String(name, "HighlightSound", "", highlight_sound, sizeof(highlight_sound));
	ini.Get_String(name, "SelectVQ", "", select_vq, sizeof(select_vq));

	return(new GraphicMenuImageItem(id, engine, origin, active_rect, image, highlighted, disabled, highlight_sound, select_vq));
}


/// <summary>
/// Constructs an image based menu item.
/// This routine loads the normal, highlighted and disabled artwork as animations and
/// hands them to the menu engine to display. Only the normal image starts out visible;
/// the others are activated as the item gains the selection or is disabled. Any of the
/// images may be omitted, in which case that state simply shows nothing.
/// </summary>
/// <param name="origin">The screen position to display the artwork at.</param>
/// <param name="rect">The screen area the mouse must be within to select this item.</param>
/// <param name="image">Filename of the artwork shown normally.</param>
/// <param name="highlight_image">Filename of the artwork shown while selected.</param>
/// <param name="disabled_image">Filename of the artwork shown while disabled.</param>
/// <param name="highlight_sound">Filename of the sound to play as this item is selected.</param>
/// <param name="select_vq">Filename of the movie to play when this item is chosen.</param>
GraphicMenuImageItem::GraphicMenuImageItem(int id, MSEngine & engine, Point2D const & origin, Rect const & rect, const char * image, const char * highlight_image, const char * disabled_image, char * highlight_sound, const char * select_vq) :
	GraphicMenuItem(id),
	Engine(&engine),
	ActiveRect(rect)
{
	Image = NULL;
	HighlightImage = NULL;
	DisabledImage = NULL;
	HighlightSound = NULL;

	MSSfxEntry * snd = NULL;
	if (strlen(highlight_sound)) {
		snd = new MSSfxEntry("HighlightSound", highlight_sound);
	} else {
		snd = NULL;
	}
	HighlightSound = snd;

	strncpy(SelectVQ, select_vq != NULL ? select_vq : "", sizeof(SelectVQ));

	if (strlen(highlight_image)) {
		HighlightImage = new MSPCXAnim(highlight_image, engine.Get_Anims(), origin, true);
		if (HighlightImage != NULL) {
			HighlightImage->Set_Active(false);
			engine.Add_Animation(HighlightImage);
		}
	}

	if (strlen(image)) {
		Image = new MSPCXAnim(image, engine.Get_Anims(), origin, true);
		if (Image != NULL) {
			engine.Add_Animation(Image);
		}
	}

	if (strlen(disabled_image)) {
		DisabledImage = new MSPCXAnim(disabled_image, engine.Get_Anims(), origin, true);
		if (DisabledImage != NULL) {
			DisabledImage->Set_Active(false);
			engine.Add_Animation(DisabledImage);
		}
	}
}


/// <summary>
/// Destroys this menu item.
/// The images belong to the menu engine and are disposed of along with it, so only the
/// highlight sound is freed here.
/// </summary>
GraphicMenuImageItem::~GraphicMenuImageItem(void)
{
	delete(HighlightSound);
}


/// <summary>
/// Is the mouse over this menu item?
/// </summary>
/// <returns>bool; Is the mouse within the active area of an item that can be selected?</returns>
bool GraphicMenuImageItem::Is_Mouse_Over(Point2D const & mouse)
{
	return(Enabled && ActiveRect.Is_Point_Within(mouse));
}


/// <summary>
/// Handles this menu item gaining or losing the selection.
/// This routine swaps between the normal and highlighted images, refreshes the part of
/// the screen this item occupies, and plays the highlight sound as the item is selected.
/// </summary>
/// <param name="selected">Is this item now the selected one?</param>
void GraphicMenuImageItem::On_Selected_Change(bool selected)
{
	Update_Images();
	Engine->Restore_Anims(ActiveRect);
	Engine->Restore_And_Advance();
	if (selected) {
		if (HighlightSound != NULL) {
			HighlightSound->Play();
		}
	}
}


/// <summary>
/// Handles this menu item becoming available or unavailable.
/// This routine swaps the disabled image in or out and then refreshes the part of the
/// screen this item occupies so that the change is visible right away.
/// </summary>
void GraphicMenuImageItem::On_Enabled_Change(bool)
{
	Update_Images();
	Engine->Restore_Anims(ActiveRect);
	Engine->Restore_And_Advance();
}


/// <summary>
/// Handles this item being taken off the page or put back on it.
/// This routine takes all of the item's artwork off the screen, disabled artwork
/// included, and then refreshes the area it occupied so that the backdrop shows through.
/// </summary>
void GraphicMenuImageItem::On_Visible_Change(bool)
{
	Update_Images();
	Engine->Restore_Anims(ActiveRect);
	Engine->Restore_And_Advance();
}


/// <summary>
/// Activates the one piece of artwork this item's current state calls for.
/// A hidden item shows none of it; otherwise the disabled image stands in for an
/// unavailable item, and an available one is drawn highlighted or not according to
/// whether it holds the menu selection.
/// </summary>
void GraphicMenuImageItem::Update_Images(void)
{
	if (Image != NULL) {
		Image->Set_Active(Visible && Enabled && !Selected);
	}
	if (HighlightImage != NULL) {
		HighlightImage->Set_Active(Visible && Enabled && Selected);
	}
	if (DisabledImage != NULL) {
		DisabledImage->Set_Active(Visible && !Enabled);
	}
}


/// <summary>
/// Performs this menu item's action.
/// The normal item action is performed first. If this item has a selection movie
/// available, the movie is then played and the menu waits for it to finish before
/// carrying on.
/// </summary>
void GraphicMenuImageItem::Action(MSEngine * engine)
{
	GraphicMenuItem::Action(engine);
	if (CCFileClass(SelectVQ).Is_Available()) {
		MSAnim * anim = new MSVQAnim(SelectVQ, AlternateSurface, engine->Get_Anims(), true);
		if (anim != NULL) {
			engine->Wait_For_Anim(anim);
		}
	}
}
