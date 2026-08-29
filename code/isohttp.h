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
#include <vector>


/*
 * Serves an image out of a URL.
 *
 * Reads arrive small and clustered -- a directory sector here, a mixfile header there --
 * and one request apiece would spend the whole run in round trips, so a read shorter than
 * a block is served from a block-sized window that is kept in a small least-recently-used
 * set. A read at least that long is already the shape a range request wants and goes
 * straight out, which is what keeps a whole extent to a single request.
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

		bool Transfer(std::uint64_t offset, void * buffer, unsigned int length);
		BlockType const * Block(std::uint64_t index);

		std::string Url;
		std::uint64_t Length;
		std::vector<BlockType> Cache;
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
