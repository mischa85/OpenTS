/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The file layer the engine reads through. It exists because a name the engine opens may
// live in two different places -- an ordinary file on the host, or an entry inside a
// mounted disc image -- and choosing between them is the file layer's business, not the
// caller's and not any operating system's.
//
// This is the seam RawFileClass sits on, and the seam the Win32 substitute answers
// CreateFileA out of, so the choice is made in exactly one place. A backend that cannot
// serve a request reports it rather than approximating, and one whose bytes have not
// arrived yet says so through Declined, which is not a failure.

#pragma once

#include "filesystem.hh"

#include <memory>


/*
 * What a stream answers about the file behind it.
 *
 * The times count hundred-nanosecond intervals from the start of 1601, and zero means the
 * backend has no answer. That unit is not a platform's: it is what the DOS date conversion,
 * the save container, and the statistics packet are already defined in, so carrying it
 * costs no conversion at any of the boundaries that outlive a session.
 */
struct FileStatusType
{
	unsigned long long Size = 0;
	unsigned long long Creation = 0;
	unsigned long long Access = 0;
	unsigned long long Write = 0;

	/*
	 * What distinguishes this file from another on the same volume. A host answers with the
	 * inode and the device; a disc entry has neither, and answers with the first logical
	 * block of the file, which is the nearest thing the volume records.
	 */
	unsigned long long Identifier = 0;
	unsigned int Volume = 0;
	unsigned int Links = 1;

	bool IsReadOnly = false;
};


/*
 * One open file, whatever is behind it. The position is the stream's own, so a caller
 * reads and seeks without ever holding a host descriptor.
 */
class FileStreamClass
{
	public:
		virtual ~FileStreamClass(void) {}

		/// <summary>Reads from the current position and advances it.</summary>
		/// <param name="actual">Receives the count transferred, on failure included.</param>
		/// <returns>bool; false on a failure. The end of the file is not one: it reports
		/// success with a shorter count, and a further read reports success with none.</returns>
		virtual bool Read(void * buffer, unsigned int size, unsigned int & actual) = 0;

		/// <summary>Writes at the current position and advances it.</summary>
		/// <param name="actual">Receives the count transferred, on failure included.</param>
		virtual bool Write(void const * buffer, unsigned int size, unsigned int & actual) = 0;

		/// <summary>Moves the position, which may be set past the end of the file.</summary>
		/// <param name="position">Receives the position measured from the start.</param>
		virtual bool Seek(long long distance, FileOriginType origin, long long & position) = 0;

		/// <summary>Answers the size, times, and writability of the open file.</summary>
		virtual bool Status(FileStatusType & status) const = 0;

		/// <summary>Sets the file times, leaving alone every one passed as zero.</summary>
		virtual bool Set_Times(unsigned long long creation, unsigned long long access,
			unsigned long long write) = 0;

		/// <summary>Ends the file at the current position.</summary>
		virtual bool Truncate(void) = 0;

		/// <summary>Puts what has been written where a reader would find it.</summary>
		virtual bool Flush(void) = 0;

		/// <summary>Says what a run of the file is about to be used for.</summary>
		/// <param name="length">Bytes the claim covers; zero means to the end.</param>
		virtual void Hint(FileHintType hint, unsigned long long offset, unsigned long long length) = 0;

		/// <summary>Whether the file was already there when this stream opened it.</summary>
		virtual bool Existed(void) const = 0;

		/// <summary>The host error code left by the last failed call, or zero.</summary>
		virtual int Error(void) const = 0;

		/// <summary>Whether the last read was declined rather than failed.</summary>
		/// <remarks>
		/// A transport that fetches its bytes may not have them yet. It declines the read
		/// instead of stalling the frame, and the caller comes back for the rest.
		/// </remarks>
		virtual bool Declined(void) const = 0;
};


/// <summary>Opens a file by name, from the host or from a mounted disc image.</summary>
/// <param name="sequential">Says the file is about to be read front to back.</param>
/// <returns>The stream, or nothing when the file could not be opened.</returns>
std::unique_ptr<FileStreamClass> Open_File_Stream(char const * name, FileAccessType access,
	FileDispositionType disposition, bool sequential = false);

/// <summary>Whether a name can be opened for reading, without holding it open.</summary>
bool File_Entry_Exists(char const * name);

/// <summary>Removes a file from the host. A disc image is read only and answers false.</summary>
bool Delete_File_Entry(char const * name);

/// <summary>The host error code left by the last failed file-layer call.</summary>
int File_Layer_Error(void);

/// <summary>Packs a file time the way DOS did: the date high, the time low.</summary>
/// <returns>Zero for a time no DOS date can hold, which is every year before 1980.</returns>
unsigned int Dos_Date_Time_From_File_Time(unsigned long long filetime);

/// <summary>Unpacks a DOS date and time into the units FileStatusType states.</summary>
unsigned long long File_Time_From_Dos_Date_Time(unsigned int datetime);
