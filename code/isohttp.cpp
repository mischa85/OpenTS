/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The browser half of the file layer's data path. isohttp.h explains what it is for; what
// this file has to solve is that ReadFile is synchronous and the web is not.
//
// The transport is a synchronous request, and it has to be. The alternative -- suspending
// the engine's own stack across a fetch on JavaScript Promise Integration -- would cost
// less and would not stall the page, but a suspend is only legal underneath a promising
// export, and the engine's first file open is not: MapSeedClass's constructor is a static
// object's, and it reaches Fetch_String, Init_Language_Resources and RawFileClass::
// Is_Available before main is entered. A fetch there throws SuspendError and ends the run.
// So the fetch transport can only return once the engine opens no file from a static
// constructor, and until then this is the transport that works everywhere the engine reads
// from.
//
// What that costs: the thread the page draws on is held for the length of a request, and a
// document is only allowed to answer a synchronous request as text, so its bytes arrive
// through a string at two bytes each and are copied out one at a time. A worker is allowed
// the array buffer and takes it, which is the other half of why moving the module onto one
// is worth doing.

#include "isohttp.h"

#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstring>


/*
** The transport. Reports the number of bytes delivered, or a negative value for a request
** that failed or that came back without the range that was asked for.
*/
EM_JS(int, ISO_Http_Transfer, (char const * url, double offset, void * buffer, unsigned int length), {
	try {
		var request = new XMLHttpRequest();
		request.open("GET", UTF8ToString(url), false);
		request.setRequestHeader("Range", "bytes=" + offset + "-" + (offset + length - 1));

		var astext = false;
		try {
			request.responseType = "arraybuffer";
		} catch (error) {
			astext = true;
		}
		if (astext) request.overrideMimeType("text/plain; charset=x-user-defined");

		request.send(null);
		if (request.status !== 206) return -1;

		if (!astext) {
			var bytes = new Uint8Array(request.response);
			var count = Math.min(bytes.length, length);
			HEAPU8.set(bytes.subarray(0, count), buffer);
			return count;
		}

		var text = request.responseText;
		var written = Math.min(text.length, length);
		for (var index = 0; index < written; index++) {
			HEAPU8[buffer + index] = text.charCodeAt(index) & 255;
		}
		return written;
	} catch (error) {
		return -1;
	}
});


EM_JS(double, ISO_Http_Length, (char const * url), {
	try {
		var request = new XMLHttpRequest();
		request.open("GET", UTF8ToString(url), false);
		request.setRequestHeader("Range", "bytes=0-0");
		request.send(null);
		if (request.status !== 206) return -1;

		var range = request.getResponseHeader("Content-Range");
		if (!range) return -1;

		var total = range.split("/")[1];
		if (!total || total === "*") return -1;
		return parseFloat(total);
	} catch (error) {
		return -1;
	}
});


/*
** Where the image is. The page is asked first so that a harness can point the module at
** one of several images without rebuilding it, and the environment second so that the same
** module answers to a shell running it under node.
*/
EM_JS(int, ISO_Image_Location_Query, (char * buffer, int size), {
	var text = "";

	if (typeof Module !== "undefined" && Module["opentsImage"]) {
		text = "" + Module["opentsImage"];
	} else if (typeof process !== "undefined" && process.env && process.env["OPENTS_IMAGE"]) {
		text = "" + process.env["OPENTS_IMAGE"];
	} else if (typeof document !== "undefined") {
		text = "opents-data.iso";
	}

	var count = 0;
	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[buffer + count] = (code > 127) ? 63 : code;
		count++;
	}
	HEAPU8[buffer + count] = 0;
	return count;
});


ISOHttpSourceClass::ISOHttpSourceClass(void) :
	Length(0)
{
}


ISOHttpSourceClass::~ISOHttpSourceClass(void)
{
	Close();
}


/// <summary>Opens an image served from a URL.</summary>
/// <param name="url">Where the image is, absolute or relative to the page.</param>
/// <returns>bool; Did the server answer a ranged request and report the image length?</returns>
bool ISOHttpSourceClass::Open(char const * url)
{
	Close();

	if (url == nullptr || *url == '\0') return(false);

	Url = url;

	double const length = ISO_Http_Length(Url.c_str());
	if (!(length > 0.0)) {
		Url.clear();
		return(false);
	}

	Length = (std::uint64_t)length;
	return(true);
}


void ISOHttpSourceClass::Close(void)
{
	Url.clear();
	Length = 0;
	Cache.clear();
}


bool ISOHttpSourceClass::Transfer(std::uint64_t offset, void * buffer, unsigned int length)
{
	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;

	/*
	**	A server is entitled to answer a range with less than was asked for, so the request
	**	is repeated from where the answer stopped rather than reported short.
	*/
	while (remaining > 0) {
		int const got = ISO_Http_Transfer(Url.c_str(), (double)offset, cursor, remaining);

		if (got <= 0) return(false);

		cursor += (unsigned int)got;
		offset += (unsigned int)got;
		remaining -= (unsigned int)got;
	}

	return(true);
}


ISOHttpSourceClass::BlockType const * ISOHttpSourceClass::Block(std::uint64_t index)
{
	for (std::size_t position = 0; position < Cache.size(); position++) {
		if (Cache[position].Index != index) continue;

		if (position != 0) {
			std::rotate(Cache.begin(), Cache.begin() + (std::ptrdiff_t)position,
				Cache.begin() + (std::ptrdiff_t)position + 1);
		}
		return(&Cache.front());
	}

	std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
	if (at >= Length) return(nullptr);

	std::uint64_t available = Length - at;
	if (available > (std::uint64_t)BLOCK_SIZE) available = (std::uint64_t)BLOCK_SIZE;

	BlockType fetched;
	fetched.Index = index;
	fetched.Data.resize((std::size_t)available);

	if (!Transfer(at, fetched.Data.data(), (unsigned int)available)) return(nullptr);

	if (Cache.size() >= (std::size_t)BLOCK_CACHE) Cache.pop_back();
	Cache.insert(Cache.begin(), std::move(fetched));
	return(&Cache.front());
}


bool ISOHttpSourceClass::Read_At(std::uint64_t offset, void * buffer, unsigned int length)
{
	if (buffer == nullptr) return(false);
	if (length == 0) return(true);
	if (Length == 0 || offset > Length || (std::uint64_t)length > Length - offset) return(false);

	if (length >= (unsigned int)BLOCK_SIZE) return(Transfer(offset, buffer, length));

	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;

	while (remaining > 0) {
		std::uint64_t const index = offset / (std::uint64_t)BLOCK_SIZE;
		std::size_t const within = (std::size_t)(offset - index * (std::uint64_t)BLOCK_SIZE);

		BlockType const * const block = Block(index);
		if (block == nullptr || within >= block->Data.size()) return(false);

		std::size_t chunk = block->Data.size() - within;
		if (chunk > (std::size_t)remaining) chunk = (std::size_t)remaining;

		std::memcpy(cursor, block->Data.data() + within, chunk);
		cursor += chunk;
		offset += chunk;
		remaining -= (unsigned int)chunk;
	}

	return(true);
}


char const * ISO_Image_Location(void)
{
	static char location[512];
	static bool asked = false;

	if (!asked) {
		asked = true;
		location[0] = '\0';
		ISO_Image_Location_Query(location, (int)sizeof(location));
	}

	return(location);
}


std::unique_ptr<ISOBlockSourceClass> ISO_Open_Location(char const * location)
{
	if (location == nullptr || *location == '\0') return(nullptr);

	/*
	**	A scheme separates a URL from a path, and a path is what the node build and the
	**	tests hand over. Nothing else distinguishes the two.
	*/
	if (std::strstr(location, "://") != nullptr) {
		std::unique_ptr<ISOHttpSourceClass> source = std::make_unique<ISOHttpSourceClass>();

		if (!source->Open(location)) return(nullptr);
		return(source);
	}

	std::unique_ptr<ISOLocalFileSourceClass> source = std::make_unique<ISOLocalFileSourceClass>();

	if (!source->Open(location)) {

		/*
		**	A bare name in a page is a URL relative to the document, which is how an image
		**	beside the module is named. There is no local file to fall back on there.
		*/
		std::unique_ptr<ISOHttpSourceClass> remote = std::make_unique<ISOHttpSourceClass>();

		if (!remote->Open(location)) return(nullptr);
		return(remote);
	}

	return(source);
}

#endif
