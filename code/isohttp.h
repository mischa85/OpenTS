/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Where a browser build gets its game data. A page has no filesystem to run out of, so
// the disc image is left on a web server and read with range requests. iso9660.h asks a
// block source for nothing but a synchronous read at an absolute offset, which is exactly
// what one ranged GET answers, so the parser is the same one a local image goes through.

#pragma once

#include "iso9660.h"

#if defined(__EMSCRIPTEN__)

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>


/*
 * What the browser's database is holding for one image, kept in memory while the run lasts
 * and written back beside the blocks so the next run knows what is there without reading
 * it. The whole of it is ordinary arithmetic over a signature and a list of block numbers,
 * which is what lets the part that decides whether a stored block may be believed be tested
 * without a server, a document or a database.
 *
 * Keeping the record beside the blocks in one transaction is what makes the two agree: a
 * transaction that does not complete leaves neither the blocks nor the record it wrote.
 */
class ISOBlockIndexClass
{
	public:
		ISOBlockIndexClass(void);

		/// <summary>Builds the key that says which image a stored block belongs to.</summary>
		/// <param name="url">The image's absolute URL.</param>
		/// <param name="length">The image's length in bytes.</param>
		/// <param name="validator">What the server calls this version of it -- an entity tag
		/// or a modification date -- or an empty string when it names none.</param>
		/// <returns>The key, or an empty string when the image cannot be identified.</returns>
		static std::string Signature(char const * url, std::uint64_t length, char const * validator);

		void Reset(std::string const & signature);

		/// <summary>Takes on a stored record, if it was written for this image.</summary>
		/// <param name="record">What the database held, or an empty string for a fresh one.</param>
		/// <param name="signature">The key the current image answers to.</param>
		/// <returns>bool; May the blocks the record lists be served? A false leaves the index
		/// empty and means the stored blocks belong to something else and must go.</returns>
		bool Adopt(char const * record, std::string const & signature);

		std::string Encode(void) const;

		bool Holds(std::uint64_t index) const {return(Held.count(index) != 0);}

		/// <summary>Records a block as stored, evicting whatever no longer fits.</summary>
		/// <param name="index">The block's number.</param>
		/// <param name="size">The bytes it holds.</param>
		/// <param name="evicted">Filled in with the blocks that must be deleted to make room.</param>
		void Note(std::uint64_t index, std::uint64_t size, std::vector<std::uint64_t> & evicted);

		std::uint64_t Bytes(void) const {return(Total);}
		std::size_t Count(void) const {return(Order.size());}
		std::string const & Key(void) const {return(Sig);}

		/*
		** How much of an image may be kept. The working set of a mission is a few tens of
		** megabytes, so this holds one comfortably while staying far enough under an origin's
		** quota that the store is not the reason a saved game will not fit.
		*/
		static constexpr std::uint64_t STORE_LIMIT = 64ull * 1024ull * 1024ull;

		// A key long enough for any URL a page will resolve, and a record long enough for the
		// key and for the block list a full store holds.
		static constexpr std::size_t SIGNATURE_MAX = 512;
		static constexpr std::size_t RECORD_MAX = 65536;

	private:

		struct EntryType {
			std::uint64_t Index;
			std::uint64_t Size;
		};

		std::string Sig;
		std::uint64_t Total;
		std::vector<EntryType> Order;
		std::unordered_set<std::uint64_t> Held;
};


/*
 * Serves an image out of a URL.
 *
 * Reads arrive small and clustered -- a directory sector here, a mixfile header there --
 * and one request apiece would spend the whole run in round trips, so a read shorter than
 * a block is served from a block-sized window that is kept in a small least-recently-used
 * set. Whole blocks are asked for together, so an extent still costs one request rather
 * than one per window, and the two partial blocks at its ends come through the window that
 * covers them -- which is also what leaves every block either wholly fetched or not fetched
 * at all, and so fit to be stored.
 *
 * What has been fetched is kept in the browser's database, so a second run reads the same
 * blocks back instead of the network. The memory-resident set stays the small window set
 * above; the stored set is bounded separately and read a block at a time.
 *
 * A server that ignores the range and answers with the entire image is rejected rather
 * than accommodated: every read would then cost the whole file.
 */
class ISOHttpSourceClass : public ISOBlockSourceClass
{
	public:
		ISOHttpSourceClass(void);
		virtual ~ISOHttpSourceClass(void) override;

		ISOHttpSourceClass(ISOHttpSourceClass const &) = delete;
		ISOHttpSourceClass & operator = (ISOHttpSourceClass const &) = delete;

		bool Open(char const * url);
		void Close(void);
		bool Is_Open(void) const {return(Length != 0);}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override;
		virtual std::uint64_t Total_Size(void) override {return(Length);}

		enum {
			BLOCK_SIZE = 65536,		// Bytes fetched for a read too short to be worth its own request.
			BLOCK_CACHE = 32		// Windows kept, which bounds the read ahead at two megabytes.
		};

	private:

		struct BlockType {
			std::uint64_t Index;
			std::vector<unsigned char> Data;
		};

		// Whether the run has reached the point where the database may be waited on at all.
		enum StoreStateType {
			STORE_UNTRIED,
			STORE_READY,
			STORE_OFF
		};

		// Blocks staged before the batch is written. It bounds what an unwritten batch costs
		// in memory, and bounds what a run that stops loses to a batch it never wrote.
		enum {
			STORE_BATCH = 16
		};

		// How long a partly filled batch waits for the loading to resume before it is
		// written anyway, in milliseconds.
		static constexpr double STORE_IDLE = 250.0;

		bool Transfer(std::uint64_t offset, void * buffer, unsigned int length);
		bool Fetch_Run(std::uint64_t offset, void * buffer, unsigned int length);
		BlockType const * Block(std::uint64_t index);

		bool Store_Ready(void);
		bool Store_Serve(std::uint64_t offset, void * buffer, unsigned int length);
		void Store_Keep(std::uint64_t offset, void const * buffer, unsigned int length);
		void Store_Write(void);
		void Store_Discard(void);

		std::string Url;
		std::uint64_t Length;
		std::vector<BlockType> Cache;

		std::string Signature;
		std::string Removals;
		ISOBlockIndexClass Index;
		StoreStateType StoreState;
		unsigned int Staged;
		double StagedAt;

};


/// <summary>Reports where the page or the host says the disc image is.</summary>
/// <returns>A URL, a local path, or an empty string when no image was named.</returns>
/// <remarks>A page names one by setting Module.opentsImage before the module loads; under
/// node the OPENTS_IMAGE environment variable does the same. A page that names none still
/// gets the default location, since a browser build has nowhere else to read from.</remarks>
char const * ISO_Image_Location(void);

/// <summary>Builds the block source that serves a location.</summary>
/// <param name="location">A URL, or a path on whatever filesystem the module has.</param>
/// <returns>An open source, or nothing when the location could not be read.</returns>
std::unique_ptr<ISOBlockSourceClass> ISO_Open_Location(char const * location);

#endif
