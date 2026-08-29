/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "iso9660.h"
#include "wwfile.h"

#include <memory>
#include <string>


/*
**	A file that lives inside a mounted ISO9660 volume. The volume is shared, because one
**	image contributes several entries to the search path and every file opened from it
**	reads through the same block source and sector cache.
**
**	A disc is read only. Create, Delete and Write report failure rather than pretending to
**	have written anything, and an open for writing is refused outright.
*/
class ISOFileClass : public FileClass
{
	public:
		ISOFileClass(void);
		virtual ~ISOFileClass(void) override;

		void Attach(std::shared_ptr<ISOVolumeClass> const & volume, char const * filename, ISOEntryClass const & entry);
		void Detach(void);
		bool Is_Attached(void) const {return(Volume != nullptr && Entry.Is_Valid());}

		virtual char const * File_Name(void) const override;
		virtual char const * Set_Name(char const * filename) override;
		virtual int Create(void) override;
		virtual int Delete(void) override;
		virtual bool Is_Available(int forced=false) override;
		virtual bool Is_Open(void) const override;
		virtual int Open(char const * filename, int rights=READ) override;
		virtual int Open(int rights=READ) override;
		virtual int Read(void * buffer, int size) override;
		virtual int Seek(int pos, int dir=SEEK_CUR) override;
		virtual int Size(void) override;
		virtual int Write(void const * buffer, int size) override;
		virtual void Close(void) override;
		virtual unsigned int Get_Date_Time(void) override;
		virtual bool Set_Date_Time(unsigned int datetime) override;
		virtual void Error(int error, int canretry = false, char const * filename=NULL) override;

	private:

		std::shared_ptr<ISOVolumeClass> Volume;
		ISOEntryClass Entry;
		std::string Filename;
		int Position;
		bool IsOpen;
};
