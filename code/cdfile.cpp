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

/* $Header: /CounterStrike/CDFILE.CPP 1     3/03/97 10:24a Joe_bostic $ */
/***********************************************************************************************
 ***             C O N F I D E N T I A L  ---  W E S T W O O D   S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Westwood Library                                             *
 *                                                                                             *
 *                    File Name : CDFILE.CPP                                                   *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : October 18, 1994                                             *
 *                                                                                             *
 *                  Last Update : September 22, 1995 [JLB]                                     *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   CDFileClass::Clear_Search_Drives -- Removes all record of a search path.                  *
 *   CDFileClass::Open -- Opens the file object -- with path search.                           *
 *   CDFileClass::Open -- Opens the file wherever it can be found.                             *
 *   CDFileClass::Set_Name -- Performs a multiple directory scan to set the filename.          *
 *   CDFileClass::Set_Search_Drives -- Sets a list of search paths for file access.            *
 *   Is_Disk_Inserted -- Checks to see if a disk is inserted in specified drive.               *
 *   harderr_handler -- Handles hard DOS errors.                                               *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "cdfile.h"
#include "mixfile.h"

#if defined(__EMSCRIPTEN__)
#include "win32compat.h"
#endif

#include <cstring>
#include <string>
#include <vector>

/*
**	Pointer to the first search path record.
*/
CDFileClass::SearchDriveType * CDFileClass::First = NULL;


/// <summary>
/// Constructs a CD file object for the file specified.
/// The name is searched in the current directory and every configured path, so the object
/// refers to the first matching local file.
/// </summary>
/// <param name="filename">The name of the file this object should refer to.</param>
CDFileClass::CDFileClass(char const *filename) :
	IsDisabled(false)
{
	CDFileClass::Set_Name(filename);
//	memset (RawPath, 0, sizeof(RawPath));
}


/// <summary>
/// Constructs a CD file object with no file attached to it yet.
/// Use Set_Name to give the object a file to work with before trying to open it.
/// </summary>
CDFileClass::CDFileClass(void) :
	IsDisabled(false)
{
}


/***********************************************************************************************
 * CDFileClass::Open -- Opens the file object -- with path search.                             *
 *                                                                                             *
 *    This will open the file object, but since the file object could have been constructed    *
 *    with a pathname, this routine will try to find the file first. For files opened for      *
 *    writing, then use the existing filename without performing a path search.                *
 *                                                                                             *
 * INPUT:   rights   -- The access rights to use when opening the file                         *
 *                                                                                             *
 * OUTPUT:  bool; Was the open successful?                                                     *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/18/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
int CDFileClass::Open(int rights)
{
	if (Is_Image_File() && (rights & WRITE) == 0) {
		Close();

		if (!ISOFile.Open(rights)) return(false);

		/*
		**	Biased files must be positioned past the bias start position.
		*/
		if (BiasStart != 0 || BiasLength != -1) {
			Hint_Extent(ISO_HINT_SEQUENTIAL);
			CDFileClass::Seek(0, SEEK_SET);
		}

		return(true);
	}

	return(BASECLASS::Open(rights));
}


/// <summary>Reports whether the file can be opened.</summary>
/// <param name="forced">Should the check keep retrying until the file becomes available?</param>
/// <returns>bool; Is the file available for opening?</returns>
bool CDFileClass::Is_Available(int forced)
{
	if (Is_Image_File()) return(true);

	return(BASECLASS::Is_Available(forced));
}


bool CDFileClass::Is_Open(void) const
{
	if (ISOFile.Is_Open()) return(true);

	return(BASECLASS::Is_Open());
}


/// <summary>Creates an empty file.</summary>
/// <returns>bool; Was the file created?</returns>
/// <remarks>A file that resolved into an image is read only, so the request fails.</remarks>
int CDFileClass::Create(void)
{
	if (Is_Image_File()) return(false);

	return(BASECLASS::Create());
}


/// <summary>Deletes the file.</summary>
/// <returns>bool; Was the file deleted?</returns>
/// <remarks>A file that resolved into an image is read only, so the request fails.</remarks>
int CDFileClass::Delete(void)
{
	if (Is_Image_File()) return(false);

	return(BASECLASS::Delete());
}


/// <summary>Reads data from the file into the buffer specified.</summary>
/// <param name="buffer">Buffer to read the data into.</param>
/// <param name="size">Number of bytes wanted.</param>
/// <returns>The number of bytes read, which is short when the file is exhausted.</returns>
int CDFileClass::Read(void *buffer, int size)
{
	if (!Is_Image_File()) return(BASECLASS::Read(buffer, size));

	/*
	**	Reading from a file that is not open opens it as a convenience, the way the raw
	**	file class does, and closes it again afterwards.
	*/
	bool opened = false;
	if (!ISOFile.Is_Open()) {
		if (!ISOFile.Open(READ)) return(0);
		opened = true;

		if (BiasStart != 0 || BiasLength != -1) {
			Hint_Extent(ISO_HINT_SEQUENTIAL);
			CDFileClass::Seek(0, SEEK_SET);
		}
	}

	/*
	**	A biased file has the requested read length limited to the bias length.
	*/
	if (BiasLength != -1) {
		int remainder = BiasLength - CDFileClass::Seek(0);
		size = size < remainder ? size : remainder;
	}

	int bytesread = size > 0 ? ISOFile.Read(buffer, size) : 0;

	if (opened) ISOFile.Close();
	return(bytesread);
}


/// <summary>Moves the current file position.</summary>
/// <param name="pos">Offset relative to the origin given.</param>
/// <param name="dir">Origin of the seek.</param>
/// <returns>The position the seek ended up at.</returns>
int CDFileClass::Seek(int pos, int dir)
{
	if (!Is_Image_File()) return(BASECLASS::Seek(pos, dir));

	/*
	**	A file that is biased will have a seek operation modified so that the file appears to
	**	exist only within the bias range. All bytes outside of this range appear to be
	**	non-existant.
	*/
	if (BiasLength != -1) {
		switch (dir) {
			case SEEK_SET:
				if (pos > BiasLength) {
					pos = BiasLength;
				}
				pos += BiasStart;
				break;

			case SEEK_CUR:
				break;

			case SEEK_END:
				dir = SEEK_SET;
				pos += BiasStart + BiasLength;
				break;
		}

		int newpos = Raw_Seek_Image(pos, dir) - BiasStart;

		if (newpos < 0) {
			newpos = Raw_Seek_Image(BiasStart, SEEK_SET) - BiasStart;
		}
		if (newpos > BiasLength) {
			newpos = Raw_Seek_Image(BiasStart+BiasLength, SEEK_SET) - BiasStart;
		}
		return(newpos);
	}

	return(Raw_Seek_Image(pos, dir));
}


int CDFileClass::Raw_Seek_Image(int pos, int dir)
{
	return(ISOFile.Seek(pos, dir));
}


/// <summary>Determines the size of the file in bytes.</summary>
/// <returns>The number of bytes the file contains.</returns>
int CDFileClass::Size(void)
{
	if (!Is_Image_File()) return(BASECLASS::Size());

	/*
	**	A biased file already has its length determined.
	*/
	if (BiasLength != -1) {
		return(BiasLength);
	}

	BiasLength = ISOFile.Size() - BiasStart;
	return(BiasLength);
}


/// <summary>Writes data to the file.</summary>
/// <param name="buffer">Buffer holding the data to write.</param>
/// <param name="size">Number of bytes to write.</param>
/// <returns>The number of bytes written.</returns>
/// <remarks>A file that resolved into an image is read only, so nothing is written.</remarks>
int CDFileClass::Write(void const *buffer, int size)
{
	if (Is_Image_File()) return(0);

	return(BASECLASS::Write(buffer, size));
}


void CDFileClass::Close(void)
{
	ISOFile.Close();
	BASECLASS::Close();
}


unsigned int CDFileClass::Get_Date_Time(void)
{
	if (Is_Image_File()) return(ISOFile.Get_Date_Time());

	return(BASECLASS::Get_Date_Time());
}


bool CDFileClass::Set_Date_Time(unsigned int datetime)
{
	if (Is_Image_File()) return(false);

	return(BASECLASS::Set_Date_Time(datetime));
}


/// <summary>Makes a portion of the file appear to be the whole file.</summary>
/// <param name="start">Offset that is to be considered the start of the file.</param>
/// <param name="length">Forced length of the file, or -1 for the remainder.</param>
void CDFileClass::Bias(int start, int length)
{
	if (!Is_Image_File()) {
		BASECLASS::Bias(start, length);
		Hint_Extent(ISO_HINT_SEQUENTIAL);
		return;
	}

	if (start == 0) {
		BiasStart = 0;
		BiasLength = -1;
		return;
	}

	BiasLength = CDFileClass::Size();
	BiasStart += start;
	if (length != -1) {
		BiasLength = BiasLength < length ? BiasLength : length;
	}
	BiasLength = BiasLength > 0 ? BiasLength : 0;

	Hint_Extent(ISO_HINT_SEQUENTIAL);

	if (ISOFile.Is_Open()) {
		CDFileClass::Seek(0, SEEK_SET);
	}
}


/// <summary>Tells the image the run of bytes this object will actually read.</summary>
/// <remarks>An open on the image says the whole file is coming, which is right until the
/// mixfile system biases the object to one embedded file. The bias is the narrower truth
/// and replaces it, so what is read ahead of the reading stops at the end of the embedded
/// file rather than running on into the next one.</remarks>
void CDFileClass::Hint_Extent(ISOHintType kind)
{
	if (Is_Image_File()) {
		if (BiasLength != -1) {
			ISOFile.Hint(kind, BiasStart, BiasLength);
		} else {
			ISOFile.Hint(kind, 0, -1);
		}
		return;
	}

#if defined(__EMSCRIPTEN__)
	/*
	**	A browser build reads its discs through the file API, so the object holding the
	**	archive is an ordinary open file as far as this class is concerned and the handle
	**	is what names the run.
	*/
	if (!BASECLASS::Is_Open()) return;

	Win32_Hint_Handle(Get_File_Handle(), kind,
		(unsigned int)((BiasStart > 0) ? BiasStart : 0),
		(unsigned int)((BiasLength > 0) ? BiasLength : 0));
#else
	(void)kind;
#endif
}


/// <summary>Says this object has stopped reading, whatever it said it would read.</summary>
void CDFileClass::Abandon(void)
{
	Hint_Extent(ISO_HINT_DONE);
}


/// <summary>Says a file will probably be wanted before long.</summary>
/// <remarks>
/// The engine knows what it is about to need well before it opens it -- a menu reads the
/// names of the videos its items play when the menu is built, and the player then spends
/// seconds reading the screen. Naming one here turns that pause into the time the bytes
/// arrive in, which on a distant server is the difference between a click that plays and a
/// click that stalls.
///
/// Nothing here fetches. The resolution is the whole of it: a name becomes the run of bytes
/// it occupies on an image, and the image decides what that is worth. A file the engine
/// already holds in memory, or that no search entry supplies, resolves to nothing.
///
/// A name the caller says will be streamed is cut back to its front. An archive keeps its
/// directory there and the registration reads it the moment it opens the archive, so that
/// much of one is worth having whatever the entries behind it come to.
/// </remarks>
void CDFileClass::Prefetch(char const * filename, PrefetchType how)
{
	/*
	**	How much of a name counts as its front. A mixfile directory is twelve bytes an entry
	**	behind a short header, so this covers one holding many thousands of them and the
	**	first of the files it describes besides.
	*/
	static int const _head = 2 * 1024 * 1024;

	if (filename == NULL || *filename == '\0') return;

	/*
	**	Mixfile lookup uppercases what it is given, so it is given a copy. The name here
	**	belongs to the caller and is commonly a literal.
	*/
	char name[_MAX_PATH];

	std::strncpy(name, filename, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';

	int start = 0;
	int length = -1;

	void * resident = NULL;
	MixFileClass * mixfile = NULL;

	if (MixFileClass::Offset(name, &resident, &mixfile, &start, &length)) {

		/*
		**	An embedded file is a run of the mixfile that carries it, and the offset the
		**	lookup reports is measured from the start of that mixfile. One already cached
		**	into memory needs nothing fetched.
		*/
		if (resident != NULL || mixfile == NULL || mixfile->Filename == NULL) return;
		if (length <= 0) return;

		std::strncpy(name, mixfile->Filename, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
	} else {
		start = 0;
		length = -1;
	}

	if (how == PREFETCH_STREAMED && (length < 0 || length > _head)) {
		length = _head;
	}

	CDFileClass locator;

	locator.Set_Name(name);

	if (locator.Is_Image_File()) {
		locator.ISOFile.Hint(ISO_HINT_SOON, start, length);
		return;
	}

#if defined(__EMSCRIPTEN__)
	/*
	**	A browser build reads its discs through the file API rather than through a search
	**	entry, so the name is put to the image the same way an open would put it.
	*/
	Win32_Hint_File(name, ISO_HINT_SOON, (unsigned int)((start > 0) ? start : 0),
		(unsigned int)((length > 0) ? length : 0));
#endif
}


/// <summary>
/// Adds a list of directories to search when a file is not in the current directory.
/// The list is written the way DOS wrote a PATH -- entries separated by semicolons, with
/// or without a trailing backslash. Each entry is appended to the search chain in the
/// order given, so the first directory named is the first one tried. An entry naming an
/// ISO9660 image is mounted and searched in place of a directory.
/// </summary>
/// <param name="pathlist">The semicolon separated list of directories and images to add.</param>
/// <returns>int; Zero if at least one directory was added, or 1 if the list held none.</returns>
int CDFileClass::Set_Search_Drives(char * pathlist)
{
	bool found = false;

	/*
	**	If there is no pathlist to add, then just return.
	*/
	if (!pathlist) return(0);

	char *copy_pathlist = strdup(pathlist);

	char const * ptr = strtok(copy_pathlist, ";");
	while (ptr != NULL) {
		if (strlen(ptr) > 0) {

			char path[MAX_PATH];						// Working path buffer.

			/*
			**	Fixup the path to be legal. Legal is defined as all that is necessary to
			**	create a pathname is to append the actual filename submitted to the
			**	file system. This means that it must have either a trailing ':' or '\'
			**	character.
			*/
			strcpy(path, ptr);
			switch (path[strlen(path)-1]) {
				case ':':
				case '\\':
					break;

				default:
					strcat(path, "\\");
					break;
			}

			found	= true;
			Add_Search_Drive(path);
		}

		/*
		**	Find the next path string and resubmit.
		*/
		ptr = strtok(NULL, ";");
	}

	free(copy_pathlist);

	if (!found) return(1);
	return(0);
}


/***********************************************************************************************
 * CDFC::Add_Search_Drive -- Add a new path to the search path list                            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    path                                                                              *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    5/22/96 10:12AM ST : Created                                                             *
 *=============================================================================================*/
void CDFileClass::Add_Search_Drive(char *path)
{
	if (path == NULL) return;

	/*
	**	A path may name an ISO9660 image instead of a directory. Mounting it here means the
	**	game data is read out of the image itself, so a disc needs no installation step. An
	**	image contributes one entry per data directory it carries; anything else contributes
	**	the single directory entry it names.
	*/
	std::shared_ptr<ISOVolumeClass> volume;
	std::vector<std::string> directories;

	std::string trimmed(path);
	while (trimmed.size() > 1 && (trimmed.back() == '\\' || trimmed.back() == '/')) {
		trimmed.pop_back();
	}

	std::shared_ptr<ISOVolumeClass> candidate = std::make_shared<ISOVolumeClass>();
	if (candidate->Open(trimmed.c_str())) {
		volume = candidate;
		ISO_Search_Directories(*volume, directories);
	} else {
		directories.push_back(std::string());
	}

	for (std::string const & directory : directories) {

		SearchDriveType *srch;					// Working pointer to path object.
		/*
		**	Allocate a record structure.
		*/
		srch	= new SearchDriveType;

		/*
		**	Attach the path to this structure.
		*/
		srch->Path = strdup(path);
		srch->Next = NULL;
		srch->Volume = volume;
		srch->Directory = directory;

		/*
		**	Attach this path record to the end of the path chain.
		*/
		if (!First) {
			First = srch;
		} else {
			SearchDriveType * chain = First;

			while (chain->Next) {
				chain = (SearchDriveType *)chain->Next;
			}
			chain->Next = srch;
		}
	}
}


/***********************************************************************************************
 * CDFileClass::Clear_Search_Drives -- Removes all record of a search path.                    *
 *                                                                                             *
 *    Use this routine to clear out any previous path(s) set with Set_Search_Drives()          *
 *    function.                                                                                *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/18/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
void CDFileClass::Clear_Search_Drives(void)
{
	SearchDriveType	* chain;			// Working pointer to path chain.

	chain = First;
	while (chain) {
		SearchDriveType	*next;

		next = (SearchDriveType *)chain->Next;
		if (chain->Path) {
			free((char *)chain->Path);
		}
		delete chain;

		chain = next;
	}
	First = 0;
}


/// <summary>
/// Searches the current directory and configured local data paths for a file.
/// The first match becomes this object's filename; if none is found, the raw filename is kept.
/// A match inside a mounted image keeps the plain filename, since there is no local path
/// that would reach it, and the object reads from the image instead.
/// </summary>
/// <param name="filename">The file name to search for.</param>
/// <returns>The selected file name, including a configured path when one supplies the match.</returns>
char const * CDFileClass::Set_Name(char const *filename)
{
	/*
	**	Renaming a file that is already reading out of an image leaves that file alone. The
	**	mixfile system renames an open object to make an embedded file look like a whole
	**	one, and expects the open file to survive it.
	*/
	if (ISOFile.Is_Open()) {
		ISOFile.Set_Name(filename);
		return(BASECLASS::Set_Name(filename));
	}

	ISOFile.Detach();

	/*
	**	Try to find the file in the current directory first. If it can be found, then
	**	just return with the normal file name setting process. Do the same if there is
	**	no multi-drive search path.
	*/
	BASECLASS::Set_Name(filename);
	if (IsDisabled || !First || filename == NULL || BASECLASS::Is_Available()) return(File_Name());

	/*
	**	Attempt to find the file first. Check the current directory. If not found there, then
	**	search all the path specifications available. If it still can't be found, then just
	**	fall into the normal raw file filename setting system.
	*/
	SearchDriveType * srch = First;

	while (srch) {

		if (srch->Volume) {

			/*
			**	Build the path within the image and look it up there.
			*/
			std::string inside = srch->Directory;
			if (!inside.empty()) inside += '\\';
			inside += filename;

			ISOEntryClass entry;
			if (srch->Volume->Find(inside.c_str(), entry) && !entry.IsDirectory) {
				ISOFile.Attach(srch->Volume, filename, entry);
				BASECLASS::Set_Name(filename);
				return(File_Name());
			}

		} else {

			char path[_MAX_PATH];

			/*
			**	Build a pathname to search for.
			*/
			strcpy(path, srch->Path);
			strcat(path, filename);

			// Check this path. Is_Available returns false when the file cannot be opened,
			// allowing the search to continue with the next configured path.
			BASECLASS::Set_Name(path);
			if (BASECLASS::Is_Available()) {
				return(File_Name());
			}
		}

		/*
		**	It wasn't found, so try the next path entry.
		*/
		srch = (SearchDriveType *)srch->Next;
	}

	/*
	**	At this point, all path searching has failed. Just set the file name to the
	**	plain text passed to this routine and be done with it.
	*/
	BASECLASS::Set_Name(filename);
	return(File_Name());
}


/***********************************************************************************************
 * CDFileClass::Open -- Opens the file wherever it can be found.                               *
 *                                                                                             *
 *    This routine is similar to the RawFileClass open except that if the file is being        *
 *    opened only for READ access, it will search all specified directories looking for the    *
 *    file. If after a complete search the file still couldn't be found, then it is opened     *
 *    using the normal BufferIOFileClass system -- resulting in normal error procedures.       *
 *                                                                                             *
 * INPUT:   filename -- Pointer to the override filename to supply for this file object. It    *
 *                      would be the base filename (sans any directory specification).         *
 *                                                                                             *
 *          rights   -- The access rights to use when opening the file.                        *
 *                                                                                             *
 * OUTPUT:  bool; Was the file opened successfully? If so then the filename may be different   *
 *                than requested. The location of the file can be determined by examining the  *
 *                filename of this file object. The filename will contain the complete         *
 *                pathname used to open the file.                                              *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/18/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
int CDFileClass::Open(char const *filename, int rights)
{
	CDFileClass::Close();

	/*
	**	Verify that there is a filename associated with this file object. If not, then this is a
	**	big error condition.
	*/
	if (!filename) {
		Error(ENOENT, false);
	}

	/*
	**	If writing is requested, then multiple drive searching is not performed.
	*/
	if (IsDisabled || rights == WRITE) {

		ISOFile.Detach();
		BASECLASS::Set_Name( filename );
		return( BASECLASS::Open( rights ) );
	}

	/*
	**	Perform normal multiple drive searching for the filename and open
	**	using the normal procedure.
	*/
	Set_Name(filename);
	return(CDFileClass::Open(rights));
}


HANDLE FindFileHandle = INVALID_HANDLE_VALUE;

/*
**	Names gathered from a mounted image by the search in progress. The system file scan has
**	no handle for an image, so the matches are collected up front and handed out from here.
*/
static std::vector<std::string> FindImageNames;
static std::size_t FindImageIndex = 0;

/// <summary>
/// Begins a search for the files matching the wildcard specified.
/// This routine will look in the current directory first and then work along the search
/// drive list, settling on the first drive that has a match. Only ordinary files qualify;
/// directories and system, hidden, or temporary files are passed over. A search entry that
/// names a mounted image is matched against the image's own directory, without regard to
/// case. Any search still in progress is closed off first.
/// </summary>
/// <param name="fname">The wildcard to search for; filled in with the file found.</param>
/// <returns>bool; Was a matching file found?</returns>
/// <remarks>Be sure that the buffer is big enough to hold the filename returned.</remarks>
bool CDFileClass::Find_First_File(char *fname)
{
	WIN32_FIND_DATAA fb;
	char scan_path[MAX_PATH];
	SearchDriveType *entry;

	if (fname) {

		Find_Close();

		strcpy(scan_path, fname);

		HANDLE file_handle = ::FindFirstFile(scan_path, &fb);
		if (file_handle != INVALID_HANDLE_VALUE && !(fb.dwFileAttributes & (FILE_ATTRIBUTE_TEMPORARY|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_HIDDEN))) {

			strcpy(fname, fb.cFileName);
			FindFileHandle = file_handle;

			return(true);
		}

		entry = First;

		while (entry != NULL) {

			if (entry->Volume) {

				/*
				**	An image has no system search handle, so every match in the entry's
				**	directory is collected now and handed out by Find_Next_File.
				*/
				ISOEntryClass directory;
				if (entry->Volume->Find(entry->Directory.c_str(), directory) && directory.IsDirectory) {

					std::vector<std::string> names;
					entry->Volume->Enumerate(directory, names);

					for (std::string const & name : names) {
						if (ISO_Match_Wildcard(fname, name.c_str())) {
							FindImageNames.push_back(name);
						}
					}
				}

				if (!FindImageNames.empty()) {
					strcpy(fname, FindImageNames.front().c_str());
					FindImageIndex = 1;
					return(true);
				}

			} else {

				strcpy(scan_path, entry->Path);
				strcat(scan_path, fname);

				file_handle = ::FindFirstFile(scan_path, &fb);
				if (file_handle != INVALID_HANDLE_VALUE && !(fb.dwFileAttributes & (FILE_ATTRIBUTE_TEMPORARY|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_HIDDEN))) {
					strcpy(fname, fb.cFileName);
					FindFileHandle = file_handle;
					return(true);
				}
			}

			entry = (SearchDriveType *)entry->Next;
		}
	}
	return(false);
}


/// <summary>
/// Fetches the next file that matches the search in progress.
/// This routine continues the scan begun by Find_First_File, working through the rest of
/// the matches on whichever drive that routine settled upon.
/// </summary>
/// <param name="buffer">Buffer to fill in with the name of the file found.</param>
/// <returns>bool; Was another matching file found?</returns>
/// <remarks>Be sure that the buffer is big enough to hold the filename returned.</remarks>
bool CDFileClass::Find_Next_File(char *buffer)
{
	WIN32_FIND_DATAA fb;

	if (buffer) {

		if (FindImageIndex < FindImageNames.size()) {
			strcpy(buffer, FindImageNames[FindImageIndex].c_str());
			FindImageIndex++;
			return(true);
		}

		if (FindFileHandle != INVALID_HANDLE_VALUE && ::FindNextFile(FindFileHandle, &fb) == TRUE) {
			strcpy(buffer, fb.cFileName);
			return(true);
		}

		buffer[0] = '\0';
	}
	return(false);
}


/// <summary>
/// Closes off the file search that is in progress.
/// Call this routine when the results of a Find_First_File scan are no longer wanted, so
/// that the search handle held on the game's behalf is given back to the system.
/// </summary>
void CDFileClass::Find_Close(void)
{
	if (FindFileHandle != INVALID_HANDLE_VALUE) {
		FindClose(FindFileHandle);
		FindFileHandle = INVALID_HANDLE_VALUE;
	}

	FindImageNames.clear();
	FindImageIndex = 0;
}
