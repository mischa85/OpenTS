/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#include "always.h"

#include "voc.h"

#include "_mixfile.h"
#include "_rect.h"
#include "_tactica.h"
#include "ccini.h"
#include "sounddriver.h"
#include "globals.h"
#include "goptions.h"
#include "mixfile.h"
#include "tactical.h"
#include "vector.h"

#include <algorithm>


DynamicVectorClass<VocClass *> Vocs;


/// <summary>
/// Creates a sound effect for the sample name specified.
/// The new sound adds itself to the master sound list and fetches its sample from the
/// mixfiles right away. The priority and volume ratings stay at their defaults until the
/// rules are read.
/// </summary>
/// <param name="filename">The root name of the sound sample, without any extension.</param>
VocClass::VocClass(const char *filename) :
	Priority(10),
	Volume(1.0),
	FilePtr(NULL)
{
	char name[_MAX_FNAME+_MAX_EXT];

	strcpy(Name, filename);

	Vocs.Add(this);

	_makepath(name, NULL, NULL, Name, ".AUD");
	FilePtr = MFCD::Retrieve(name);
}


/// <summary>
/// Removes this sound effect from the master sound list.
/// </summary>
VocClass::~VocClass(void)
{
	Vocs.Delete(this);
}


/// <summary>
/// Fetches this sound effect's settings from the rules.
/// This routine will look for a section named after the sound and take its priority and
/// volume from there. A sound with no section of its own falls back to the defaults. The
/// sample itself is retrieved from the mixfiles either way.
/// </summary>
/// <param name="ini">The rules database to fetch the settings from.</param>
/// <returns>bool; Did the sound have a section of its own?</returns>
bool VocClass::Fill_In(CCINIClass const &ini)
{
	char name[_MAX_FNAME+_MAX_EXT];

	if (ini.Is_Present(Name)) {
		Priority = ini.Get_Int(Name, "Priority", Priority);
		Volume = ini.Get_Float(Name, "Volume", Volume);
		_makepath(name, NULL, NULL, Name, ".AUD");
		FilePtr = MFCD::Retrieve(name);
		return(true);
	}

	Priority = 10;
	Volume = 1.0;
	_makepath(name, NULL, NULL, Name, ".AUD");
	FilePtr = MFCD::Retrieve(name);

	return(false);
}


/// <summary>
/// Plays this sound effect at the volume specified.
/// The volume requested is scaled by both this sound's own volume rating and the player's
/// sound effect option setting, so the sound falls silent when the effects are turned off.
/// </summary>
/// <param name="vol">The volume to play at, where 1.0 is this sound's own full volume.</param>
/// <param name="var">The variation number for sound effects that have variations.</param>
/// <returns>Returns with the sound handle, or -1 if no sound was played.</returns>
int VocClass::Play(float vol, int var)
{
	if (Options.SoundVolume > 0.0) {
		if (Can_Play() && Audio_Available()) {
			vol = std::min(Options.SoundVolume * Volume * vol, 1.0f);
			return(Audio.Play_Sample(FilePtr, Priority * vol, std::min(int(255.0 * vol), 255)));
		}
	}
	return(-1);
}


/// <summary>
/// Plays this sound effect at the volume specified.
/// This routine is used by the voice sound effects, where the caller has already worked
/// out the final volume and the sound effect option setting does not apply.
/// </summary>
/// <param name="vol">The volume to play at, where 1.0 is this sound's own full volume.</param>
/// <returns>Returns with the sound handle, or -1 if no sound was played.</returns>
int VocClass::Play(float vol)
{
	if (vol > 0.0) {
		if (Can_Play() && Audio_Available()) {
			vol = std::min(Volume * vol, 1.0f);
			return(Audio.Play_Sample(FilePtr, Priority * vol, std::min(int(255.0 * vol), 255)));
		}
	}
	return(-1);
}

/***********************************************************************************************
 * Sound_Effect -- General purpose sound player.                                               *
 *                                                                                             *
 *    This is used for general purpose sound effects. These are sounds that occur outside      *
 *    of the game world. They do not have a corresponding game world location as their source. *
 *                                                                                             *
 * INPUT:   voc      -- The sound effect number to play.                                       *
 *                                                                                             *
 *          volume   -- The volume to assign to this sound effect.                             *
 *                                                                                             *
 *          variation   -- This is the optional variation number to use when playing special   *
 *                         sound effects that have variations. For normal sound effects, this  *
 *                         parameter is ignored.                                               *
 *                                                                                             *
 *          house -- This specifies the optional house override value to use when playing      *
 *                   sound effects that have a variation. If not specified, then the current   *
 *                   player is examined for the house variation to use.                        *
 *                                                                                             *
 * OUTPUT:  Returns with the sound handle (-1 if no sound was played).                         *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/12/1994 JLB : Created.                                                                 *
 *   11/12/1994 JLB : Handles cache logic.                                                     *
 *   05/04/1995 JLB : Variation adjustments.                                                   *
 *   11/01/1996 JLB : House override control.                                                  *
 *=============================================================================================*/
int Sound_Effect(VocType voc, float volume, int var)
{
	if (voc != VOC_NONE) {
		return(Vocs[voc]->Play(volume, var));
	}
	return(-1);
}


/// <summary>
/// Plays a voice sound effect.
/// This routine is used for the spoken responses and announcements. Unlike a general
/// purpose sound effect, the volume is not scaled by the sound effect option setting.
/// </summary>
/// <param name="voc">The sound effect to play.</param>
/// <param name="volume">The volume to assign to this sound effect.</param>
/// <returns>Returns with the sound handle, or -1 if no sound was played.</returns>
int Voice_Sound_Effect(VocType voc, float volume)
{
	if (voc != VOC_NONE) {
		return(Vocs[voc]->Play(volume));
	}
	return(-1);
}

/***********************************************************************************************
 * Sound_Effect -- Plays a sound effect in the tactical map.                                   *
 *                                                                                             *
 *    This routine is used when a sound effect occurs in the game world. It handles fading     *
 *    the sound according to distance.                                                         *
 *                                                                                             *
 * INPUT:   voc   -- The sound effect number to play.                                          *
 *                                                                                             *
 *          coord -- The world location that the sound originates from.                        *
 *                                                                                             *
 *          variation   -- This is the optional variation number to use when playing special   *
 *                         sound effects that have variations. For normal sound effects, this  *
 *                         parameter is ignored.                                               *
 *                                                                                             *
 *          house -- This specifies the optional house override value to use when playing      *
 *                   sound effects that have a variation. If not specified, then the current   *
 *                   player is examined for the house variation to use.                        *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/12/1994 JLB : Created.                                                                 *
 *   01/05/1995 JLB : Reduces sound more dramatically when off screen.                         *
 *   09/15/1996 JLB : Revamped volume logic.                                                   *
 *   11/01/1996 JLB : House override control.                                                  *
 *=============================================================================================*/
int Sound_Effect(VocType voc, Coord const & coord)
{
	if (voc != VOC_NONE) {
		float vol = 1.0;
		Point2D point;
		if (!TacticalMap->Coord_To_Pixel(coord, point)) {

			int px = point.X;
			int x = px < 0 ? abs(px) : 0;
			int w = px > TacticalRect.Width ? px - TacticalRect.Width : 0;

			int py = point.Y;
			int y = py < 0 ? abs(py) : 0;
			int h = py > TacticalRect.Height ? py - TacticalRect.Height : 0;

			x = std::max(abs(x), abs(w));
			y = std::max(abs(y), abs(h));

			const float v = (1/1360.0f);

			vol = 1.0 - (std::max(x, y) * v);
			vol = vol >= 0.0 ? vol : 0;
		}
		float volume = vol;
		return(Sound_Effect(voc, volume, 0));
	}
	return(-1);
}


/// <summary>
/// Creates the master sound effect list from the rules.
/// This routine will fetch every sound named in the sound list section, creating a sound
/// effect for any that does not exist yet, and then let each one fill itself in from its
/// own section.
/// </summary>
/// <param name="ini">The rules database to fetch the sound list from.</param>
void Init_Vocs(CCINIClass const &ini)
{
	char const * const SECTION = "SoundList";

	if (ini.Is_Present(SECTION)) {
		int count = ini.Entry_Count(SECTION);
		for (int i = 0; i < count; i++) {
			char name[32];
			if (ini.Get_String(SECTION, ini.Get_Entry(SECTION, i), "", name, sizeof(name)) != 0) {

				VocClass *voc = NULL;
				VocType type = VocClass::From_Name(name);
				if (type == VOC_NONE) {
					voc = new VocClass(name);
				} else {
					voc = Vocs[type];
				}
				voc->Fill_In(ini);
			}
		}
	}
}


/// <summary>
/// Destroys every sound effect in the master sound list.
/// This routine is used when shutting the game down or before the sound list is rebuilt
/// from a fresh set of rules.
/// </summary>
void Free_Vocs(void)
{
	while (Vocs.Count() > 0) {
		VocClass *voc = Vocs[0];
		delete voc;
	}
}


/***********************************************************************************************
 * Voc_From_Name -- Fetch VocType from ASCII name specified.                                   *
 *                                                                                             *
 *    This will find the corresponding VocType from the ASCII string specified. It does this   *
 *    by finding a root filename that matches the string.                                      *
 *                                                                                             *
 * INPUT:   name  -- Pointer to the ASCII string that will be converted into a VocType.        *
 *                                                                                             *
 * OUTPUT:  Returns with the VocType that matches the string specified. If no match could be   *
 *          found, then VOC_NONE is returned.                                                  *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/06/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
VocType VocClass::From_Name(char const * name)
{
	if (name == NULL) return(VOC_NONE);

	for (VocType voc = VOC_FIRST; voc < Vocs.Count(); voc = VocType(voc + 1)) {
		if (stricmp(name, Vocs[voc]->Name) == 0) {
			return(voc);
		}
	}

	return(VOC_NONE);
}


/// <summary>
/// Fetches the sound effect that matches the ASCII name specified.
/// This routine is used when reading sound assignments out of the rules, where a sound is
/// named by the root of its filename. The placeholder "none" name is recognized as meaning
/// no sound at all.
/// </summary>
/// <param name="name">Pointer to the ASCII name of the sound effect to find.</param>
/// <returns>Returns with a pointer to the matching sound effect. Otherwise, NULL is
/// returned.</returns>
VocClass * VocClass_From_Name(char const * name)
{
	if (name == NULL) return(NULL);

	if (!strcmpi(name, "<none>")) return(NULL);

	for (VocType voc = VOC_FIRST; voc < Vocs.Count(); voc = VocType(voc + 1)) {
		if (stricmp(name, Vocs[voc]->Name) == 0) {
			return(Vocs[voc]);
		}
	}

	return(NULL);
}


/***********************************************************************************************
 * Voc_Name -- Fetches the name for the sound effect.                                          *
 *                                                                                             *
 *    This routine returns the descriptive name of the sound effect. Currently, this is just   *
 *    the root of the file name.                                                               *
 *                                                                                             *
 * INPUT:   voc   -- The VocType that the corresponding name is requested.                     *
 *                                                                                             *
 * OUTPUT:  Returns with a pointer to the text string the represents the sound effect.         *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/06/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
char const * Voc_Name(VocType voc)
{
	if (voc != VOC_NONE && voc < Vocs.Count()) {
		return(Vocs[voc]->Name);
	}
	return("<none>");
}


/// <summary>
/// Fetches the sound effect number of this sound.
/// This is the inverse of the master sound list lookup -- it recovers the VocType that the
/// rest of the game uses to refer to this sound.
/// </summary>
/// <returns>Returns with the VocType of this sound, or VOC_NONE if it is not
/// registered.</returns>
VocType VocClass::Voc_Type(void)
{
	for (int index = 0; index < Vocs.Count(); index++) {
		if (Vocs[index] == this) {
			return(VocType)(index);
		}
	}
	return(VOC_NONE);
}


/// <summary>
/// Can this sound effect be played?
/// A sound whose sample could not be found in the mixfiles is silent, as is every sound
/// while the game is running in quiet mode.
/// </summary>
/// <returns>bool; Is this sound effect available to be played?</returns>
bool VocClass::Can_Play(void)
{
	if (FilePtr != NULL && !Debug_Quiet) {
		return(true);
	}
	return(false);
}
