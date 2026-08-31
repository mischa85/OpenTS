/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

/*
 * What a caller asks of a file. These say what is wanted of the file, not what any one
 * operating system calls it, which is the whole point of asking the file layer rather than
 * a platform API.
 */

enum FileAccessType {
	FILE_ACCESS_READ,
	FILE_ACCESS_WRITE,
	FILE_ACCESS_READ_WRITE
};


enum FileDispositionType {
	FILE_OPEN_EXISTING,			// Fails when the file is not there.
	FILE_OPEN_ALWAYS,			// Opens what is there and creates what is not.
	FILE_CREATE_ALWAYS,			// Creates, discarding whatever was there.
	FILE_CREATE_NEW,			// Creates, failing when something is already there.
	FILE_TRUNCATE_EXISTING		// Empties what is there, failing when it is not.
};


enum FileOriginType {
	FILE_ORIGIN_BEGIN,
	FILE_ORIGIN_CURRENT,
	FILE_ORIGIN_END
};


/*
 * What a hint claims about a run of a file, in the order of how much it claims. A hint is
 * advisory: a backend holding its bytes already does nothing with one, and a backend
 * fetching them over a network reads ahead instead of waiting to be asked. iso9660.hh
 * states the same three claims for the image reader these map onto.
 */
enum FileHintType {
	FILE_HINT_SEQUENTIAL,		// Being read now, front to back, ending where it says.
	FILE_HINT_SOON,				// Probably wanted later, and only worth fetching while idle.
	FILE_HINT_DONE				// Finished with, whatever was claimed about it before.
};
