/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#include "cmp.h"

#include <string.h>

namespace {

/* Zapped-audio command codes, packed into the top two bits of a code byte. */
enum {
	CODE_2BIT = 0,
	CODE_4BIT = 1,
	CODE_RAW = 2,
	CODE_SILENCE = 3
};

signed char const Decode2Bit[4] = {-2, -1, 0, 1};
signed char const Decode4Bit[16] = {-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8};


/// <summary>Adds a signed delta to an unsigned sample, saturating at both ends.</summary>
/// <param name="sample">Previous sample value.</param>
/// <param name="delta">Delta to apply.</param>
/// <returns>The new sample, clamped to 0..255.</returns>
unsigned char Add_Delta(unsigned char sample, signed char delta)
{
	int value = (int)sample + (int)delta;

	if (delta < 0) {
		if (value < 0) {
			value = 0;
		}
	} else {
		if (value > 255) {
			value = 255;
		}
	}

	return (unsigned char)value;
}

} // namespace


/// <summary>Decompresses an LCW encoded data block.</summary>
/// <remarks>
/// A leading zero byte selects the relative stream format, in which the med and long
/// copy commands reference bytes behind the current output position rather than
/// offsets from the start of the destination. Every command is clipped to the
/// remaining destination space, and decoding stops once the buffer is full.
/// </remarks>
/// <param name="source">Compressed data.</param>
/// <param name="dest">Buffer to decompress into.</param>
/// <param name="length">Size of the destination buffer.</param>
/// <returns>Number of bytes written to the destination.</returns>
unsigned long __cdecl VQA_LCW_Uncompress(char const * source, char * dest, unsigned long length)
{
	unsigned char const * src = (unsigned char const *)source;
	unsigned char * dst = (unsigned char *)dest;
	unsigned char * const first = dst;
	unsigned char * const lastbyte = dst + length;

	bool relative = (*src == 0);

	if (relative) {
		src++;
	}

	for (;;) {
		long remaining = (long)(lastbyte - dst);

		if (remaining == 0) {
			break;
		}

		/* The absolute format gives up once a command has overrun the buffer; the
		 * relative format keeps going, and its unsigned clip then never triggers.
		 */
		if (!relative && remaining < 0) {
			break;
		}

		unsigned long maxlen = (unsigned long)remaining;
		unsigned char op = *src++;
		unsigned long count;
		unsigned char const * copy;

		if (!(op & 0x80)) {

			/* Short copy from destination. */
			count = (unsigned long)(op >> 4) + 3;
			unsigned int offset = ((unsigned int)(op & 0x0F) << 8) | *src++;
			copy = dst - offset;

			if (count > maxlen) {
				count = maxlen;
			}

			while (count--) {
				*dst++ = *copy++;
			}

		} else if (!(op & 0x40)) {

			if (op == 0x80) {
				break;
			}

			/* Medium copy from source. */
			count = (unsigned long)(op & 0x3F);

			if (count > maxlen) {
				count = maxlen;
			}

			while (count--) {
				*dst++ = *src++;
			}

		} else if (op == 0xFE) {

			/* Long run of a single byte. */
			count = (unsigned long)src[0] | ((unsigned long)src[1] << 8);

			unsigned char data = src[2];
			src += 3;

			if (count > maxlen) {
				count = maxlen;
			}

			while (count--) {
				*dst++ = data;
			}

		} else {

			/* Medium or long copy from destination. */
			count = (unsigned long)(op & 0x3F) + 3;

			if (op == 0xFF) {
				count = (unsigned long)src[0] | ((unsigned long)src[1] << 8);
				src += 2;
			}

			unsigned int offset = (unsigned int)src[0] | ((unsigned int)src[1] << 8);
			src += 2;

			copy = relative ? (dst - offset) : (first + offset);

			if (count > maxlen) {
				count = maxlen;
			}

			while (count--) {
				*dst++ = *copy++;
			}
		}
	}

	return (unsigned long)(dst - first);
}


/// <summary>Decompresses a zapped audio sample.</summary>
/// <param name="source">Encoded audio data.</param>
/// <param name="dest">Buffer to decompress into.</param>
/// <param name="count">Maximum size of the destination buffer.</param>
/// <returns>Number of source bytes consumed.</returns>
long __cdecl AudioUnzap(void * source, void * dest, long count)
{
	long incount = 0;

	if (source == nullptr || dest == nullptr || count == 0) {
		return incount;
	}

	unsigned char const * src = (unsigned char const *)source;
	unsigned char * dst = (unsigned char *)dest;
	unsigned char previous = 0x80;

	while (count > 0) {
		unsigned char code = *src++;
		incount++;

		unsigned int command = (unsigned int)(code >> 6);
		unsigned int data = (unsigned int)(code & 0x3F);

		if (command == CODE_RAW) {

			if (data & 0x20) {

				/* The low five bits hold a signed delta. */
				signed char delta = (signed char)((unsigned char)(data << 3));
				delta = (signed char)(delta >> 3);

				previous = (unsigned char)(previous + delta);
				*dst++ = previous;
				count--;

			} else {

				/* The low five bits hold a count of raw samples that follow. */
				long run = (long)data + 1;

				memcpy(dst, src, (size_t)run);
				src += run;
				dst += run;
				incount += run;
				count -= run;
				previous = *(dst - 1);
			}

		} else if (command == CODE_4BIT) {

			long run = (long)data + 1;

			while (run--) {
				unsigned char packed = *src++;
				incount++;

				previous = Add_Delta(previous, Decode4Bit[packed & 0x0F]);
				dst[0] = previous;

				previous = Add_Delta(previous, Decode4Bit[packed >> 4]);
				dst[1] = previous;

				dst += 2;
				count -= 2;
			}

		} else if (command == CODE_2BIT) {

			long run = (long)data + 1;

			while (run--) {
				unsigned char packed = *src++;
				incount++;

				for (int i = 0; i < 4; i++) {
					previous = Add_Delta(previous, Decode2Bit[(packed >> (i * 2)) & 0x03]);
					dst[i] = previous;
				}

				dst += 4;
				count -= 4;
			}

		} else {

			/* A run of zero deltas repeats the previous sample. */
			long run = (long)data + 1;

			memset(dst, previous, (size_t)run);
			dst += run;
			count -= run;
		}
	}

	return incount;
}
