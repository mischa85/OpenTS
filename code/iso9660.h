/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// An ISO9660 reader for the game discs. Nothing here touches the operating system beyond
// the block source it is handed, so the same parser serves a local image file and, in the
// browser, an image fetched with HTTP range requests.

#pragma once

#include "iso9660.hh"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>


/*
 * Where the bytes of an image come from. The only requirement is a synchronous read at an
 * absolute offset, which is the smallest contract a range-request transport can satisfy.
 */
class ISOBlockSourceClass
{
	public:
		virtual ~ISOBlockSourceClass(void) {}

		/// <summary>Reads a run of bytes from an absolute offset within the image.</summary>
		/// <param name="offset">Byte offset from the start of the image.</param>
		/// <param name="buffer">Destination for the bytes read.</param>
		/// <param name="length">Number of bytes wanted.</param>
		/// <returns>bool; true only when every requested byte was delivered.</returns>
		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) = 0;

		/// <summary>Reports how large the image is.</summary>
		/// <returns>The size of the image in bytes, or zero when it is not known.</returns>
		virtual std::uint64_t Total_Size(void) = 0;

		/// <summary>Says what a run of the image is about to be used for.</summary>
		/// <param name="kind">Whether the run is being read now or may be wanted later.</param>
		/// <param name="offset">Byte offset from the start of the image.</param>
		/// <param name="length">How many bytes the run covers.</param>
		/// <remarks>Advisory, and answered by doing nothing at all. A source whose bytes are
		/// already at hand has nothing to gain from one; a source that fetches them over a
		/// network reads ahead within the run rather than inferring which way it is going,
		/// and never past its end.</remarks>
		virtual void Hint(ISOHintType kind, std::uint64_t offset, std::uint64_t length)
			{(void)kind; (void)offset; (void)length;}
};


/*
 * A scope in which a read of an image may answer that the bytes are not here yet instead of
 * going and getting them.
 *
 * Every read below this is synchronous, and on a page that means the engine stops until a
 * server answers. For a map or a cameo that is the right trade, because the game cannot go
 * on without them. For music it is the wrong one: a score the game has to wait for costs a
 * frame, and a score it simply does not play yet costs nothing at all.
 *
 * A read that declines delivers nothing rather than part of what was asked for, so the
 * caller reads zero bytes and the file position does not move; asking again once the bytes
 * have landed reads the same run over. Declined is not short, and Declined() is how the two
 * are told apart -- a caller that reads zero and was not declined has reached the end of the
 * file, exactly as before.
 *
 * The scope is meant to be entered around one read of a file that is already open and left
 * straight afterwards. Nothing a name is looked up through is inside one, so a file that is
 * missing still reads as missing. Constructing one with defer false suspends any scope
 * around it, which is how a caller that must not decline says so.
 *
 * A source with the bytes at hand never declines, so a local image and a host filesystem
 * behave exactly as they did.
 */
class ISODeferredReadClass
{
	public:
		explicit ISODeferredReadClass(bool defer = true);
		~ISODeferredReadClass(void);

		ISODeferredReadClass(ISODeferredReadClass const &) = delete;
		ISODeferredReadClass & operator = (ISODeferredReadClass const &) = delete;

		/// <summary>Did a read inside this scope decline for want of bytes not yet here?</summary>
		bool Declined(void) const {return(IsDeclined);}

		/// <summary>May a read decline rather than fetch, on this thread, right now?</summary>
		static bool Deferring(void);

		/// <summary>Has a read inside the innermost scope declined?</summary>
		/// <returns>bool; false when there is no scope, which is what every other target
		/// and every read outside one answers.</returns>
		/// <remarks>For the layers between the block source and the caller, which see a read
		/// of nothing and have to know whether to try again or to give up on it.</remarks>
		static bool Declined_Now(void);

		/// <summary>Records that a read has declined, which the innermost scope reports.</summary>
		static void Decline(void);

	private:

		bool IsDeferring;
		bool IsDeclined;
		ISODeferredReadClass * Outer;

		static thread_local ISODeferredReadClass * Innermost;
};


/*
 * Serves an image out of a local file.
 */
class ISOLocalFileSourceClass : public ISOBlockSourceClass
{
	public:
		ISOLocalFileSourceClass(void);
		virtual ~ISOLocalFileSourceClass(void) override;

		ISOLocalFileSourceClass(ISOLocalFileSourceClass const &) = delete;
		ISOLocalFileSourceClass & operator = (ISOLocalFileSourceClass const &) = delete;

		bool Open(char const * filename);
		void Close(void);
		bool Is_Open(void) const {return(File != nullptr);}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override;
		virtual std::uint64_t Total_Size(void) override {return(Length);}

	private:

		std::FILE * File;
		std::uint64_t Length;
};


/*
 * One run of consecutive sectors belonging to a file. A file normally has exactly one, but
 * ECMA-119 splits anything past the four gigabyte extent limit across several records.
 */
struct ISOExtentType {
	std::uint32_t Start;   // First logical block of the run.
	std::uint32_t Length;  // Bytes this run contributes.
};


/*
 * A resolved directory entry. Holding the extents rather than a directory offset keeps the
 * entry usable after the sector it was parsed from has left the cache.
 */
class ISOEntryClass
{
	public:
		ISOEntryClass(void) : IsDirectory(false), Size(0), DateTime(0) {}

		bool Is_Valid(void) const {return(!Extents.empty());}
		void Reset(void) {IsDirectory = false; Size = 0; DateTime = 0; Extents.clear();}

		bool IsDirectory;
		std::uint32_t Size;                   // Total bytes across every extent.
		unsigned int DateTime;                // Packed the way FileClass::Get_Date_Time reports it.
		std::vector<ISOExtentType> Extents;
};


/*
 * A mounted ISO9660 volume.
 *
 * Names are matched against the primary descriptor's plain 8.3 identifiers, which is what
 * the engine asks for anyway. A Joliet supplementary descriptor is detected and reported
 * but never used for lookup, so a disc that carries one still resolves through the same
 * uppercase names as a disc that does not.
 */
class ISOVolumeClass
{
	public:
		ISOVolumeClass(void);
		~ISOVolumeClass(void);

		ISOVolumeClass(ISOVolumeClass const &) = delete;
		ISOVolumeClass & operator = (ISOVolumeClass const &) = delete;

		bool Open(char const * filename);
		bool Attach(std::unique_ptr<ISOBlockSourceClass> source);
		void Close(void);

		bool Is_Open(void) const {return(Source != nullptr);}
		bool Has_Joliet(void) const {return(HasJoliet);}
		char const * Volume_Name(void) const {return(VolumeName.c_str());}
		ISOEntryClass const & Root(void) const {return(RootEntry);}

		bool Find(char const * path, ISOEntryClass & entry) const;
		bool Find_In(ISOEntryClass const & directory, char const * name, ISOEntryClass & entry) const;
		bool Enumerate(ISOEntryClass const & directory, std::vector<std::string> & names) const;

		int Read(ISOEntryClass const & entry, std::uint32_t offset, void * buffer, unsigned int length) const;
		void Hint(ISOEntryClass const & entry, ISOHintType kind, std::uint32_t offset, std::uint32_t length) const;

	private:

		bool Parse_Descriptors(void);
		unsigned char const * Sector(std::uint32_t block) const;
		bool Scan(ISOEntryClass const & directory, char const * wanted, ISOEntryClass * found, std::vector<std::string> * names) const;

		struct CacheEntryType {
			std::uint32_t Block;
			bool IsValid;
			unsigned char Data[ISO_SECTOR_SIZE];
		};

		std::unique_ptr<ISOBlockSourceClass> Source;
		ISOEntryClass RootEntry;
		std::string VolumeName;
		bool HasJoliet;

		/*
		**	The cache is written by lookups on an otherwise const volume.
		*/
		mutable std::vector<CacheEntryType> Cache;
};


bool ISO_Compare_Names(char const * left, char const * right);
bool ISO_Match_Wildcard(char const * pattern, char const * name);
void ISO_Search_Directories(ISOVolumeClass const & volume, std::vector<std::string> & directories);
