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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/ini.h                                  $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 11/14/01 1:32a                                              $*
 *                                                                                             *
 *                    $Revision:: 16                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "crc.h"
#include "index.h"
#include "listnode.h"

#ifdef _WIN32
#ifdef _WIN32
#include <comdef.h>
#else
#include "win32compat.h"
#endif
#else
#include "platform.h"
#endif
#include <cstdlib>

class FileClass;
class Pipe;
class PKey;
class Straw;
template<class T> class TRect;
typedef TRect<int> Rect;
template<class T> class TPoint2D;
template<class T> class TPoint3D;

/*
**	This is an INI database handler class. It handles a database with a disk format identical
**	to the INI files commonly used by Windows.
*/
class INIClass {
	public:
		INIClass(void) : TailComment(NULL) {}
		virtual ~INIClass(void);

		/*
		**	Fetch and store INI data.
		*/
		int Load(FileClass & file, bool keepcomments = false);
		int Load(Straw & file, bool keepcomments = false);
		int Save(FileClass & file) const;
		int Save(Pipe & file) const;

		/*
		**	Erase all data within this INI file manager.
		*/
		bool Clear(char const * section = NULL, char const * entry = NULL);

//		int Line_Count(char const * section) const;
		bool Is_Loaded(void) const {return(!SectionList.Is_Empty());}
		int Size(void) const;
		bool Is_Present(char const * section, char const * entry = NULL) const {if (entry == 0) return(Find_Section(section) != 0);return(Find_Entry(section, entry) != 0);}

		/*
		**	Fetch the number of sections in the INI file or verify if a specific
		**	section is present.
		*/
		int Section_Count(void) const;
		bool Section_Present(char const * section) const {return(Find_Section(section) != NULL);}

		/*
		**	Fetch the number of entries in a section or get a particular entry in a section.
		*/
		int Entry_Count(char const * section) const;
		char const * Get_Entry(char const * section, int index) const;

		/*
		**	Get the various data types from the section and entry specified.
		*/
		PKey Get_PKey(bool fast) const;
		bool Get_Bool(char const * section, char const * entry, bool defvalue=false) const;
		double Get_Float(char const * section, char const * entry, double defvalue=0.0) const;
		int Get_Hex(char const * section, char const * entry, int defvalue=0) const;
		int Get_Int(char const * section, char const * entry, int defvalue=0) const;
		int Get_String(char const * section, char const * entry, char const * defvalue, char * buffer, int size) const;
		int Get_TextBlock(char const * section, char * buffer, int len) const;
		int Get_UUBlock(char const * section, void * buffer, int len) const;
		Rect const Get_Rect(char const * section, char const * entry, Rect const & defvalue) const;
		TPoint3D<int> const Get_Point(char const * section, char const * entry, TPoint3D<int> const & defvalue) const;
		TPoint2D<int> const Get_Point(char const * section, char const * entry, TPoint2D<int> const & defvalue) const;
		TPoint3D<float> const Get_Point(char const * section, char const * entry, TPoint3D<float> const & defvalue) const;
		CLSID const Get_CLSID(char const * section, char const * entry, CLSID defvalue) const;

		/*
		**	Put a data type to the section and entry specified.
		*/
		bool Put_Bool(char const * section, char const * entry, bool value);
		bool Put_Float(char const * section, char const * entry, double number);
		bool Put_Hex(char const * section, char const * entry, int number);
		bool Put_Int(char const * section, char const * entry, int number, int format=0);
		bool Put_PKey(PKey const & key);
		bool Put_String(char const * section, char const * entry, char const * string);
		bool Put_TextBlock(char const * section, char const * text);
		bool Put_UUBlock(char const * section, void const * block, int len);
		bool Put_Rect(char const * section, char const * entry, Rect const & value);
		bool Put_Point(char const * section, char const * entry, TPoint3D<int> const & value);
		bool Put_Point(char const * section, char const * entry, TPoint3D<float> const & value);
		bool Put_Point(char const * section, char const * entry, TPoint2D<int> const & value);
		bool Put_CLSID(char const * section, char const * entry, CLSID const & value);

		enum {MAX_LINE_LENGTH=512};

		struct INIComment {
			INIComment(void) :
				Comment(NULL),
				Next(NULL)
			{
			}

			/*
			 * This is one line held exactly as it was read out of the file -- a comment
			 * complete with the semicolon that introduced it, or a blank line that spaced
			 * the file out. It is NULL for a node whose line turned out to be an entry.
			 */
			char * Comment;

			/*
			 * Pointer to the next line of the comment block, or NULL at the end of it. Lines
			 * that sat together in the file are chained up as one block, so that the whole
			 * of it can be written back ahead of whatever it introduces.
			 */
			INIComment * Next;
		};

		/*
		**	The value entries for the INI file are stored as objects of this type.
		**	The entry identifier and value string are combined into this object.
		*/
		struct INIEntry : public Node<INIEntry *> {
			INIEntry(char * entry = NULL, char * value = NULL, INIComment * prefix_comment = NULL, char * line_comment = NULL, int comment_col = 0, int assign_col = 0, int value_col = 0) :
				Entry(entry),
				Value(value),
				PrefixComment(prefix_comment),
				LineComment(line_comment),
				AssignColumn(assign_col),
				ValueColumn(value_col),
				CommentColumn(comment_col)
			{
			}

			~INIEntry(void);
			int Index_ID(void) const {return(CRCEngine()(Entry, strlen(Entry)));};

			char * Entry;
			char * Value;

			/*
			 * This is the chain of comment lines that sat immediately above this entry in
			 * the file. Save writes them back out ahead of the entry, so that a comment
			 * stays with the line it was written for.
			 */
			INIComment * PrefixComment;

			/*
			 * This is the comment text that trailed this entry on its own line, with the
			 * semicolon that introduced it stripped off. If the entry carried no trailing
			 * comment, then this is NULL.
			 */
			char * LineComment;

			/*
			 * These are the columns that the assignment character, the value, and the
			 * trailing comment stood at in the file this entry was read from. Save pads each
			 * line out with spaces to put them back, so that rewriting a database preserves
			 * the layout its author gave it.
			 */
			int AssignColumn;
			int ValueColumn;
			int CommentColumn;

			private:
				/*
				**	Ensure that the copy constructor and assignment operator never exist.
				*/
				INIEntry(INIEntry const & rvalue);
				INIEntry operator = (INIEntry const & rvalue);
		};

		/*
		**	Each section (bracketed) is represented by an object of this type. All entries
		**	subordinate to this section are attached.
		*/
		struct INISection : public Node<INISection *> {
			INISection(char * section, INIComment * prefixcomment = NULL) :
				Section(section),
				PrefixComment(prefixcomment)
			{
			}
			~INISection(void);
			INIEntry * Find_Entry(char const * entry) const;
			int Index_ID(void) const {return(CRCEngine()(Section, strlen(Section)));};

			char * Section;
			List<INIEntry *> EntryList;
			IndexClass<int, INIEntry *> EntryIndex;

			/*
			 * This is the chain of comment lines that sat immediately above this section's
			 * header in the file. Save writes them back out ahead of the header, so that a
			 * comment stays with the section it was written for.
			 */
			INIComment * PrefixComment;

			private:
				/*
				**	Ensure that the copy constructor and assignment operator never exist.
				*/
				INISection(INISection const & rvalue);
				INISection operator = (INISection const & rvalue);
		};

		/*
		**	Utility routines to help find the appropriate section and entry objects.
		*/
		static bool Is_A_Section(char * buffer);
		INISection * Find_Section(char const * section) const;
		INIEntry * Find_Entry(char const * section, char const * entry) const;
		static void Strip_Comments(char * buffer);
		static char * Scan_Line_For_Columns(char * buffer, int & assign_pos, int & value_pos, int & comment_pos);

		/*
		**	This is the list of all sections within this INI file.
		*/
		List<INISection *> SectionList;

		IndexClass<int, INISection *> SectionIndex;

	private:

		/*
		 * This is the chain of comment lines that trailed the last section of the file, or
		 * the whole of a file that held no sections at all. Save writes them back out after
		 * everything else, so that nothing is lost off the end of the file.
		 */
		INIComment * TailComment;

		/*
		**	Ensure that the copy constructor and assignment operator never exist.
		*/
		INIClass(INIClass const & rvalue);
		INIClass operator = (INIClass const & rvalue);

};
