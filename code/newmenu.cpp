/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "newmenu.h"

#include "_pk.h"
#include "addon.h"
#include "ccfile.h"
#include "grphmenu.h"
#include "init.h"
#include "loaddlg.h"
#include "mixfile.h"
#include "movie.h"
#include "vector.h"


/// <summary>
/// Fetches the one and only new menu object.
/// This routine is used by the menu code and by the background draw helper to reach the
/// graphical menu without having to pass it around.
/// </summary>
/// <returns>Returns with a pointer to the global new menu object.</returns>
NewMenuClass * Get_New_Menu(void)
{
	static NewMenuClass _menu;

	return(&_menu);
}


/// <summary>
/// Draws the title page behind the menus.
/// This routine is used by the various menu and dialog screens to restore the backdrop
/// before they draw themselves over it.
/// </summary>
void Draw_Menu_Background(void)
{
	Load_Title_Page(Get_New_Menu()->Background, true);
}


/// <summary>
/// Constructor for the new menu object.
/// This routine will attach the graphical menu mix file if it is installed and pick the
/// title page that the menus draw behind themselves. A missing mix file is how the game
/// detects that it must fall back to the old menu system.
/// </summary>
NewMenuClass::NewMenuClass(void) :
	MixFile(NULL),
	GameMode(-1),
	Background(NULL)
{
	if (CCFileClass("GMENU.MIX").Is_Available()) {
		MixFile = new MFCD("GMENU.MIX", &FastKey);
		if (CCFileClass("Loading.PCX").Is_Available()) {
			Background = new char[16];
			strcpy(Background, "Loading.PCX");
		}
	}

	if (Background == NULL) {
		Background = new char[10];
		strcpy(Background, "Title.PCX");
	}
}


/// <summary>
/// Destructor for the new menu object.
/// This routine will release the graphical menu mix file, which detaches its contents
/// from the file system.
/// </summary>
NewMenuClass::~NewMenuClass(void)
{
	delete [] Background;
	delete MixFile;
}


/// <summary>
/// Handles the top level loop of the graphical menu.
/// This routine will move between the game select page and the Tiberian Sun or Firestorm
/// main menus as the player picks a game or backs out of one, playing the appropriate
/// title movie on the way in. It only returns once the player has chosen something that
/// the caller must act upon.
/// </summary>
/// <returns>Returns with the menu selection the player made.</returns>
__forceinline int NewMenuClass::Game_Select_Loop(NewMenuClass * menu)
{
	int item = 0;

	while (true) {
		switch (menu->GameMode) {
			default:
				if (Addon_Installed(ADDON_FIRESTORM)) {
					item = menu->Select_Game_Type();
				} else {
					item = GMENU_TIBSUN;
				}
				switch (item) {
					case GMENU_TIBSUN:
						menu->GameMode = 0;
						if (CCFileClass("TS_Title.VQA").Is_Available()) {
							Play_Movie("TS_Title.VQA", THEME_NONE, false, true, false);
						}
						continue;

					case GMENU_FIRESTORM:
						menu->GameMode = 1;
						if (CCFileClass("FS_Title.VQA").Is_Available()) {
							Play_Movie("FS_Title.VQA", THEME_NONE, false, true, false);
						}
						continue;

					case GMENU_BACK:
						continue;
				}

				switch (item) {
					case NSEL_EXIT:
						return(NSEL_EXIT);

					case NSEL_VERSION:
						return(NSEL_VERSION);

					case NSEL_VIEW_CREDITS:
						return(NSEL_VIEW_CREDITS);
				}
				break;

			case 0:
				item = menu->Display_Tiberian_Sun_Menu();
				if (item == GMENU_BACK) {
					menu->GameMode = -1;
					continue;
				}
				return(item);

			case 1:
				item = menu->Display_Firestorm_Menu();
				if (item == GMENU_BACK) {
					menu->GameMode = -1;
					continue;
				}
				return(item);
		}
	}
}


/// <summary>
/// Runs the graphical main menu.
/// This routine is the entry point the startup code uses to display the new menu. If the
/// graphical menu data was never installed, it bows out immediately so that the caller
/// can fall back to the old menu system.
/// </summary>
/// <returns>Returns with the menu selection the player made, or NSEL_OLD_MENU if the
/// graphical menu is unavailable.</returns>
int NewMenuClass::Process_Game_Select(void)
{
	if (MixFile == NULL) {
		return(NSEL_OLD_MENU);
	}

	return(Game_Select_Loop(this));
}


/// <summary>
/// Displays a menu page with every option enabled.
/// </summary>
/// <param name="section">The NewMenu.INI section that describes the menu page.</param>
/// <returns>Returns with the selection the player made.</returns>
int NewMenuClass::Display_Game_Select_Menu(char const * section)
{
	static DynamicVectorClass<int> options;
	DynamicVectorClass<int> hidden;

	/*
	 * A page has nothing to quit back to, and a canvas the engine has stopped drawing to
	 * is worse than no choice at all, so the exit is left off this page entirely.
	 */
#if defined(__EMSCRIPTEN__)
	hidden.Add(NSEL_EXIT);
#endif

	return(Display_Menu(section, options, hidden));
}


#if defined(__EMSCRIPTEN__)
/// <summary>
/// Withholds the menu options a page cannot play.
/// </summary>
/// <remarks>
/// A page has no sockets and no serial line, so the three network games and the tour that
/// is played over them are shown but not offered. The exit goes further and is dropped
/// entirely: there is nothing to quit back to, and a canvas the engine has stopped drawing
/// to is worse than no choice at all.
/// </remarks>
/// <param name="options">The menu items that should be shown disabled.</param>
/// <param name="hidden">The menu items that should not appear at all.</param>
static void Hide_Unplayable_Options(DynamicVectorClass<int> & options, DynamicVectorClass<int> & hidden)
{
	options.Add(NSEL_LAN);
	options.Add(NSEL_INTERNET);
	options.Add(NSEL_SERIAL_MODEM);
	options.Add(NSEL_WDT);

	hidden.Add(NSEL_EXIT);
}
#endif


/// <summary>
/// Displays a menu page and waits for the player to choose.
/// This routine will build the menu described by the INI section, adopt the background
/// page that comes with it, gray out the options that are not available, drop the ones
/// that do not belong on this build at all, and then run the menu until the player picks
/// something.
/// </summary>
/// <param name="section">The NewMenu.INI section that describes the menu page.</param>
/// <param name="options">The menu items that should be shown disabled.</param>
/// <param name="hidden">The menu items that should not appear at all.</param>
/// <returns>Returns with the selection the player made, or NSEL_OLD_MENU if the menu
/// could not be built.</returns>
int NewMenuClass::Display_Menu(char const * section, DynamicVectorClass<int> & options, DynamicVectorClass<int> & hidden)
{
	GraphicMenu * menu = Do_Graphic_Menu("NewMenu.INI", section);

	if (menu == NULL) {
		return(NSEL_OLD_MENU);
	}

	delete Background;
	char const * bgname = menu->BackgroundName.Peek();
	if (bgname == NULL) {
		bgname = "Title.PCX";
	}
	Background = new char[strlen(bgname) + 1];
	strcpy(Background, bgname);

	for (int option : options) {
		menu->Set_Item_Enabled(option, false);
	}

	for (int option : hidden) {
		menu->Set_Item_Visible(option, false);
	}

	int result = menu->Presentation();
	delete menu;
	return((int)result);
}


/// <summary>
/// Handles the choice between Tiberian Sun and Firestorm.
/// This routine will display the game select page and put the addon system into the state
/// that matches the player's choice. Firestorm needs its disc, so the page is shown again
/// if the player cannot supply one. The scenario descriptions are reloaded before
/// returning, since they differ between the two games.
/// </summary>
/// <returns>Returns with the game the player selected.</returns>
int NewMenuClass::Select_Game_Type(void)
{
	int result;

	Disable_Addon(ADDON_ANY);

	bool retry = true;
	while (retry) {
		result = Display_Game_Select_Menu("MainMenu");
		switch (result) {
			case GMENU_TIBSUN:
				Disable_Addon(ADDON_ANY);
				Set_Required_Addon(ADDON_BASE_GAME);
				retry = false;
				break;

			case GMENU_FIRESTORM:
				Disable_Addon(ADDON_ANY);
				Enable_Addon(ADDON_FIRESTORM);
				Set_Required_Addon(ADDON_FIRESTORM);
				retry = false;
				break;

			default:
				retry = false;
				break;
		}
	}

	Session.Read_Scenario_Descriptions();
	return(result);
}


/// <summary>
/// Displays the Tiberian Sun main menu.
/// This routine will switch the addon system over to the base game before showing the
/// menu, and disable any option that would lead nowhere -- offering to load a mission
/// when there are no saved games, for instance.
/// </summary>
/// <returns>Returns with the selection the player made.</returns>
int NewMenuClass::Display_Tiberian_Sun_Menu(void)
{
	Disable_Addon(ADDON_ANY);
	Set_Required_Addon(ADDON_BASE_GAME);

	DynamicVectorClass<int> options;
	DynamicVectorClass<int> hidden;

	if (!Addon_Installed(ADDON_ANY)) {
		options.Add(102);
	}

	if (!LoadOptionsClass().Files_Present()) {
		options.Add(NSEL_LOAD_MISSION);
	}

#if defined(__EMSCRIPTEN__)
	Hide_Unplayable_Options(options, hidden);
#endif

	return(Display_Menu("TiberianSunMenu", options, hidden));
}


/// <summary>
/// Displays the Firestorm main menu.
/// This routine will enable the Firestorm addon before showing the menu, and disable the
/// load option when there are no saved games to offer.
/// </summary>
/// <returns>Returns with the selection the player made.</returns>
int NewMenuClass::Display_Firestorm_Menu(void)
{
	Disable_Addon(ADDON_ANY);
	Enable_Addon(ADDON_FIRESTORM);
	Set_Required_Addon(ADDON_FIRESTORM);

	DynamicVectorClass<int> options;
	DynamicVectorClass<int> hidden;

	if (!LoadOptionsClass().Files_Present()) {
		options.Add(NSEL_LOAD_MISSION);
	}

#if defined(__EMSCRIPTEN__)
	Hide_Unplayable_Options(options, hidden);
#endif

	return(Display_Menu("FirestormMenu", options, hidden));
}


GraphicMenu * Do_Graphic_Menu(const char * ini, const char * name);
