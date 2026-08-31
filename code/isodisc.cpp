/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The native host's ISO9660 image backend. The browser build answers the same pair out of
// its HTTP range reader in isohttp.cpp; here an image is an ordinary local file.

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include "iso9660.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>


/// <summary>
/// Names the disc images a session should mount. The OPENTS_DISCS environment variable
/// carries them, colon separated; a session with none mounts nothing and reads its game
/// data from ordinary directories.
/// </summary>
/// <param name="locations">Receives the image paths in mount order.</param>
void ISO_Image_Locations(std::vector<std::string> & locations)
{
	locations.clear();

	char const * configured = std::getenv("OPENTS_DISCS");
	if (configured == NULL || configured[0] == '\0') {
		return;
	}

	std::string const text = configured;
	std::string::size_type start = 0;

	while (start <= text.length()) {
		std::string::size_type end = text.find(':', start);
		if (end == std::string::npos) {
			end = text.length();
		}

		if (end > start) {
			locations.push_back(text.substr(start, end - start));
		}

		if (end == text.length()) {
			break;
		}
		start = end + 1;
	}
}


/// <summary>
/// Opens one disc image as a block source.
/// </summary>
/// <param name="location">Path of the image file.</param>
/// <returns>The open source, or nothing when the file cannot be opened.</returns>
std::unique_ptr<ISOBlockSourceClass> ISO_Open_Location(char const * location)
{
	auto source = std::make_unique<ISOLocalFileSourceClass>();

	if (!source->Open(location)) {
		return(nullptr);
	}

	return(source);
}

#endif
