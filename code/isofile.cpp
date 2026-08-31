/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "isofile.h"


ISOFileClass::ISOFileClass(void) :
	Position(0),
	IsOpen(false)
{
}


ISOFileClass::~ISOFileClass(void)
{
	Detach();
}


/// <summary>Binds this object to a file already located within a volume.</summary>
/// <param name="volume">The mounted volume holding the file.</param>
/// <param name="filename">The name the caller asked for, reported by File_Name.</param>
/// <param name="entry">The directory entry located for that name.</param>
void ISOFileClass::Attach(std::shared_ptr<ISOVolumeClass> const & volume, char const * filename, ISOEntryClass const & entry)
{
	Close();

	Volume = volume;
	Entry = entry;
	Filename = filename != nullptr ? filename : "";
	Position = 0;
}


/// <summary>Releases the file and the volume it was read from.</summary>
void ISOFileClass::Detach(void)
{
	Close();

	Volume.reset();
	Entry.Reset();
	Filename.clear();
}


/// <summary>Says what a run of this file is about to be used for.</summary>
void ISOFileClass::Hint(ISOHintType kind, int offset, int length)
{
	if (!Is_Attached() || offset < 0) return;

	std::uint32_t const size = Entry.Size;
	std::uint32_t const at = (std::uint32_t)offset;

	if (at >= size) return;

	std::uint32_t const span = (length < 0) ? (size - at) : (std::uint32_t)length;

	Volume->Hint(Entry, kind, at, span);
}


char const * ISOFileClass::File_Name(void) const
{
	return(Filename.c_str());
}


/// <summary>Renames the object without disturbing the file it is reading.</summary>
/// <param name="filename">The name to report from now on.</param>
/// <returns>The name now attached to this object.</returns>
/// <remarks>A disc entry is located by search rather than by name, so a rename changes
/// only what File_Name reports. This mirrors what the raw file class does with an open
/// handle, which the mixfile system depends on.</remarks>
char const * ISOFileClass::Set_Name(char const * filename)
{
	Filename = filename != nullptr ? filename : "";
	return(Filename.c_str());
}


int ISOFileClass::Create(void)
{
	return(false);
}


int ISOFileClass::Delete(void)
{
	return(false);
}


bool ISOFileClass::Is_Available(int)
{
	return(Is_Attached());
}


bool ISOFileClass::Is_Open(void) const
{
	return(IsOpen);
}


int ISOFileClass::Open(char const * filename, int rights)
{
	Set_Name(filename);
	return(Open(rights));
}


/// <summary>Opens the attached file for reading.</summary>
/// <param name="rights">The access wanted. Any request to write is refused.</param>
/// <returns>bool; Was the file opened?</returns>
int ISOFileClass::Open(int rights)
{
	Close();

	if ((rights & WRITE) != 0) return(false);
	if (!Is_Attached()) return(false);

	Position = 0;
	IsOpen = true;

	/*
	**	An open says what a directory lookup cannot: these bytes are one file and are about
	**	to be read from front to back. A caller that then reads only part of it -- the
	**	mixfile system biases an object to one embedded file -- narrows the run with a hint
	**	of its own.
	*/
	Volume->Hint(Entry, ISO_HINT_SEQUENTIAL, 0, Entry.Size);
	return(true);
}


int ISOFileClass::Read(void * buffer, int size)
{
	if (!IsOpen || buffer == NULL || size <= 0) return(0);

	int actual = Volume->Read(Entry, (std::uint32_t)Position, buffer, (unsigned int)size);
	Position += actual;
	return(actual);
}


int ISOFileClass::Seek(int pos, int dir)
{
	if (!IsOpen) return(0);

	int length = (int)Entry.Size;

	switch (dir) {
		case SEEK_SET:
			Position = pos;
			break;

		case SEEK_END:
			Position = length + pos;
			break;

		case SEEK_CUR:
		default:
			Position += pos;
			break;
	}

	if (Position < 0) Position = 0;
	if (Position > length) Position = length;

	return(Position);
}


int ISOFileClass::Size(void)
{
	if (!Is_Attached()) return(0);

	return((int)Entry.Size);
}


int ISOFileClass::Write(void const *, int)
{
	return(0);
}


void ISOFileClass::Close(void)
{
	IsOpen = false;
	Position = 0;
}


unsigned int ISOFileClass::Get_Date_Time(void)
{
	return(Entry.DateTime);
}


bool ISOFileClass::Set_Date_Time(unsigned int)
{
	return(false);
}


void ISOFileClass::Error(int, int, char const *)
{
}
