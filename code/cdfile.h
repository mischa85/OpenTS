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

/* $Header: /CounterStrike/CDFILE.H 1     3/03/97 10:24a Joe_bostic $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Westwood LIbrary                                             *
 *                                                                                             *
 *                    File Name : CDFILE.H                                                     *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : October 18, 1994                                             *
 *                                                                                             *
 *                  Last Update : October 18, 1994   [JLB]                                     *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "bfiofile.h"
#include "isofile.h"

#include <memory>

/*
 * How the engine is going to read a name it says it will want, which is what decides how
 * much of that name is worth having before it asks for any of it.
 *
 * An archive the engine reads across is worth having whole. It opens files scattered all
 * over such an archive and, over a session, reads very nearly the whole of it, so there is
 * no run in the reading to get in front of and nothing to be gained by waiting to find out
 * which part is wanted. An archive the engine streams a single entry out of is not: a disc
 * of video is registered exactly like an archive of maps, its entries are films, and only
 * the player knows which one they are about to watch. What is worth having from one of
 * those is the directory at its front, which the registration reads at once.
 *
 * Size is not the distinction and is not consulted. What decides is what the engine will do
 * with the archive, which the code registering it is the only thing that knows.
 */
enum PrefetchType {
	PREFETCH_WHOLE,		// Read across, so every part of it is wanted before long.
	PREFETCH_STREAMED	// Streamed an entry at a time, so only its directory is.
};

/*
 * This class is derived from the BufferIOFileClass, and adds the ability to search across
 * several directories for a file. The current directory is examined first, and if the file
 * is not there then every directory in the search list is tried in turn. A file being
 * opened for writing is only ever looked for in the current directory.
 *
 * The search order is whatever order the directories were handed to Set_Search_Drives(),
 * which takes them in the same semicolon separated form the DOS PATH variable used.
 *
 * A search entry may name an ISO9660 image rather than a directory, in which case the game
 * data is read straight out of the image and nothing is installed or extracted. One image
 * contributes two entries, its installed data directory ahead of its root, so a file the
 * disc carries in both places resolves to the installed copy. Across images the caller
 * decides: whichever image was handed to Set_Search_Drives() first wins, so a session
 * meant to play the expansion lists the expansion disc ahead of the base game discs.
 *
 * Names are matched without regard to case in both kinds of entry, because the discs, the
 * engine and the host filesystem do not agree on how a name is spelled.
 */
class CDFileClass : public BufferIOFileClass
{
		typedef BufferIOFileClass BASECLASS;

	public:
		CDFileClass(char const *filename);
		CDFileClass(void);
		virtual ~CDFileClass(void) override {};

		virtual char const * Set_Name(char const *filename) override;
		virtual int Create(void) override;
		virtual int Delete(void) override;
		virtual bool Is_Available(int forced=false) override;
		virtual bool Is_Open(void) const override;
		virtual int Open(char const *filename, int rights=READ) override;
		virtual int Open(int rights=READ) override;
		virtual int Read(void *buffer, int size) override;
		virtual int Seek(int pos, int dir=SEEK_CUR) override;
		virtual int Size(void) override;
		virtual int Write(void const *buffer, int size) override;
		virtual void Close(void) override;
		virtual unsigned int Get_Date_Time(void) override;
		virtual bool Set_Date_Time(unsigned int datetime) override;

		/*
		**	RawFileClass::Bias() reaches for the operating system file handle, which a file
		**	read out of an image does not have. This declaration hides it so that the
		**	mixfile system, which biases a file object to make an embedded file look like a
		**	whole one, works the same whichever kind of entry supplied the mixfile.
		*/
		void Bias(int start, int length=-1);

		void Searching(int on) {IsDisabled = !on;};

		static int Set_Search_Drives(char * pathlist);
		static void Add_Search_Drive(char *path);
		static void Clear_Search_Drives(void);

		/// <summary>Says a file will probably be wanted before long.</summary>
		/// <param name="filename">The name the engine would open, whether it names a file
		/// of its own or one embedded in a mixfile.</param>
		/// <param name="how">How the engine is going to read it.</param>
		/// <remarks>Advisory and free. The name is resolved to the run of bytes it occupies
		/// on whichever search entry supplies it, and that entry is told; an entry with the
		/// bytes already at hand does nothing with it. A name that resolves to nothing, or
		/// to something already in memory, costs the resolution and no more.</remarks>
		static void Prefetch(char const * filename, PrefetchType how = PREFETCH_WHOLE);

		/// <summary>Says this object has stopped reading, whatever it said it would read.</summary>
		/// <remarks>A file is normally read to the end it declared when it was opened, and
		/// what the search entry fetched in front of that reading is taken by it. A player
		/// who skips a cutscene ends the reading early, and saying so here is what stops the
		/// rest of the film being fetched. Advisory and free, and harmless on an object that
		/// declared nothing.</remarks>
		void Abandon(void);

		static bool Find_First_File(char *buffer);
		static bool Find_Next_File(char *buffer);
		static void Find_Close(void);

	private:

		bool Is_Image_File(void) const {return(ISOFile.Is_Attached());}
		int Raw_Seek_Image(int pos, int dir=SEEK_CUR);
		void Hint_Extent(ISOHintType kind);

		/*
		**	Is multi-drive searching disabled for this file object?
		*/
		bool IsDisabled;

		/*
		**	The file this object resolved to inside an image, if it resolved to one at all.
		*/
		ISOFileClass ISOFile;

		/*
		**	This is the control record for each of the drives specified in the search
		**	path. There can be many such search paths available.
		*/
		struct SearchDriveType {
			void * Next;        // Pointer to next search record.
			char const * Path;  // Pointer to path string.

			/*
			**	Set only for an entry that names a directory inside a mounted image. The
			**	volume is shared by every entry and every open file that image supplies.
			*/
			std::shared_ptr<ISOVolumeClass> Volume;
			std::string Directory;
		};

		/*
		**	This points to the first path record.
		*/
		static SearchDriveType * First;
};
