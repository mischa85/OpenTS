/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

enum {
	/*
	 * ECMA-119 fixes the logical sector at 2048 bytes and puts the first volume descriptor
	 * at sector 16. A descriptor set is terminated rather than counted, so the scan needs a
	 * ceiling of its own against a truncated or corrupt image.
	 */
	ISO_SECTOR_SIZE = 2048,
	ISO_FIRST_DESCRIPTOR = 16,
	ISO_MAX_DESCRIPTORS = 64,

	/*
	 * Shortest legal directory record: 33 bytes of fixed fields plus at least one identifier
	 * byte, and never crossing a sector boundary.
	 */
	ISO_MIN_RECORD_SIZE = 34,

	/*
	 * Directory traversal revisits the same handful of sectors, and on a network-backed
	 * block source every miss costs a round trip.
	 */
	ISO_SECTOR_CACHE_COUNT = 16
};


enum ISODescriptorType {
	ISO_DESCRIPTOR_BOOT = 0,
	ISO_DESCRIPTOR_PRIMARY = 1,
	ISO_DESCRIPTOR_SUPPLEMENTARY = 2,
	ISO_DESCRIPTOR_PARTITION = 3,
	ISO_DESCRIPTOR_TERMINATOR = 255
};


enum ISORecordFlag {
	ISO_RECORD_HIDDEN = 0x01,
	ISO_RECORD_DIRECTORY = 0x02,
	ISO_RECORD_ASSOCIATED = 0x04,
	ISO_RECORD_EXTENDED_FORMAT = 0x08,
	ISO_RECORD_EXTENDED_PERMISSIONS = 0x10,
	ISO_RECORD_MULTI_EXTENT = 0x80
};


/*
 * What a hint says about a run of an image, in the order of how much it claims.
 *
 * A hint is advisory and free. It exists because the file layer knows three things the block
 * source cannot work out for itself: that a run of bytes is one file and will be read from
 * front to back, that a file the engine has not opened yet is likely to be wanted, and that
 * a run it said all of that about has been given up.
 *
 * The last matters because the reading is what pays for the guessing. A player who skips a
 * cutscene has said they do not want the rest of it, and a block source told so at that
 * moment stops fetching it; one left to find out for itself goes on spending the player's
 * connection on a film nobody is watching.
 */
enum ISOHintType {
	ISO_HINT_SEQUENTIAL,	// Being read now, front to back, and ending where it says.
	ISO_HINT_SOON,			// Probably wanted later, and only worth fetching while nothing else is.
	ISO_HINT_DONE			// Finished with, whatever was said about it before.
};
