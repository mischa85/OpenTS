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

/****************************************************************************
*
*  File              : vqa_uncomp.asm
*  Description       : Audio unzap and LCW decompression for VQA playback.
*
****************************************************************************/

#include "always.h"

#include "vqalib/cmp.h"

/*
 * Two decompressors, each replacing an assembly routine of the same name.
 *
 * AudioUnzap expands delta-encoded 8 bit audio. A code byte selects a run of 2 bit deltas,
 * 4 bit deltas, raw samples or repeats of the previous sample.
 *
 * VQA_LCW_Uncompress expands the LCW (format80) stream VQA carries its palettes, codebooks
 * and pointer data in. It clamps every command against the room left in the destination,
 * which is what separates it from LCW_Uncompress in lcwuncmp.cpp; that one ignores the
 * length it is given, so it is not a substitute here.
 */

namespace {

signed char const _2BitDecode[4] = {-2, -1, 0, 1};
signed char const _4BitDecode[16] = {-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8};

int const CODE_2BIT = 0;
int const CODE_4BIT = 1;
int const CODE_RAW = 2;


/// <summary>
/// Adds a delta to a sample, holding the result inside a byte. The assembly read the carry
/// and sign flags to do this, so a run of deltas piles up against 0 or 255 rather than
/// wrapping around.
/// </summary>
/// <param name="sample">The sample to adjust.</param>
/// <param name="delta">The amount to move it by.</param>
/// <returns>unsigned char; The adjusted sample.</returns>
inline unsigned char Add_Delta(unsigned char sample, signed char delta)
{
	int const sum = (int)sample + (int)delta;

	if (delta < 0) {
		return((sum < 0) ? (unsigned char)0 : (unsigned char)sum);
	}

	return((sum > 255) ? (unsigned char)255 : (unsigned char)sum);
}


/// <summary>
/// Copies bytes one at a time so a source inside the destination reads back what has just
/// been written, which is how a run shorter than its own length repeats itself.
/// </summary>
/// <param name="dest">Where the bytes go.</param>
/// <param name="source">Where they come from.</param>
/// <param name="count">How many to copy.</param>
inline void Copy_Overlapped(unsigned char * dest, unsigned char const * source, long count)
{
	for (long i = 0; i < count; i++) {
		dest[i] = source[i];
	}
}


inline unsigned long Fetch_Word(unsigned char const * & source)
{
	unsigned long const value = (unsigned long)source[0] | ((unsigned long)source[1] << 8);
	source += 2;
	return(value);
}

}	// namespace


/// <summary>
/// Expands a zapped audio sample into a buffer.
///
/// A run keeps writing until it ends, so a stream whose final run reaches past the count can
/// leave more behind than was asked for; the assembly behaved the same way and the callers
/// size their buffers for it.
/// </summary>
/// <param name="source">Encoded audio data.</param>
/// <param name="dest">Buffer to decompress into.</param>
/// <param name="count">Size of the destination buffer.</param>
/// <returns>long; The number of source bytes consumed.</returns>
long __cdecl AudioUnzap(void * source, void * dest, long count)
{
	long incount = 0;

	if (source == NULL || dest == NULL || count == 0) {
		return(incount);
	}

	unsigned char const * src = (unsigned char const *)source;
	unsigned char * dst = (unsigned char *)dest;
	unsigned char previous = 0x80;
	long remaining = count;

	while (remaining > 0) {
		unsigned int const code = *src++;
		incount++;

		unsigned int const op = code >> 6;
		unsigned int const sub = code & 0x3F;

		if (op == CODE_RAW) {

			if ((sub & 0x20) != 0) {

				/*
				 * The low five bits are a signed delta, added without being held
				 * inside the byte the way the packed deltas below are.
				 */
				signed char const delta = (signed char)((sub & 0x1F) << 3) >> 3;
				previous = (unsigned char)(previous + delta);
				*dst++ = previous;
				remaining--;
				continue;
			}

			/*
			 * The low five bits count the raw samples that follow.
			 */
			long const raw = (long)sub + 1;

			for (long i = 0; i < raw; i++) {
				dst[i] = src[i];
			}

			src += raw;
			dst += raw;
			incount += raw;
			remaining -= raw;
			previous = dst[-1];
			continue;
		}

		long const runs = (long)sub + 1;

		if (op == CODE_4BIT) {
			for (long i = 0; i < runs; i++) {
				unsigned int const packed = *src++;
				incount++;

				previous = Add_Delta(previous, _4BitDecode[packed & 0x0F]);
				unsigned char const second = Add_Delta(previous, _4BitDecode[(packed >> 4) & 0x0F]);

				*dst++ = previous;
				*dst++ = second;
				remaining -= 2;
				previous = second;
			}
			continue;
		}

		if (op == CODE_2BIT) {
			for (long i = 0; i < runs; i++) {
				unsigned int const packed = *src++;
				incount++;

				for (int shift = 0; shift < 8; shift += 2) {
					previous = Add_Delta(previous, _2BitDecode[(packed >> shift) & 0x03]);
					*dst++ = previous;
				}

				remaining -= 4;
			}
			continue;
		}

		/*
		 * A run of zero deltas simply repeats the sample already reached.
		 */
		for (long i = 0; i < runs; i++) {
			*dst++ = previous;
		}

		remaining -= runs;
	}

	return(incount);
}


/// <summary>
/// Expands an LCW compressed stream, stopping at the end marker or when the destination is
/// full. Every command is clamped against the room left, so a stream that claims more than
/// the caller allowed is truncated rather than allowed to run past the buffer.
///
/// A leading zero byte selects the relative form, where a long copy reaches back from the
/// current position instead of forward from the start of the destination.
/// </summary>
/// <param name="source">Compressed data.</param>
/// <param name="dest">Buffer to decompress into.</param>
/// <param name="length">Size of the destination buffer.</param>
/// <returns>unsigned long; The number of bytes written.</returns>
unsigned long __cdecl VQA_LCW_Uncompress(char const * source, char * dest, unsigned long length)
{
	unsigned char const * src = (unsigned char const *)source;
	unsigned char * const start = (unsigned char *)dest;
	unsigned char * dst = start;
	unsigned char * const end = start + length;

	bool const relative = (*src == 0);

	if (relative) {
		src++;
	}

	while (true) {

		/*
		 * The assembly left the relative form without this guard, so a destination it had
		 * already overshot kept it looping; stopping here costs nothing on a stream that
		 * lands squarely on the end.
		 */
		long const maxlen = (long)(end - dst);

		if (maxlen <= 0) {
			break;
		}

		unsigned int const code = *src++;

		if ((code & 0x80) == 0) {

			/*
			 * Short run: three bits of length and twelve bits reaching back into what
			 * has already been written.
			 */
			long count = (long)(code >> 4) + 3;
			unsigned long const offset = ((unsigned long)(code & 0x0F) << 8) | (unsigned long)*src++;

			if (count > maxlen) {
				count = maxlen;
			}

			Copy_Overlapped(dst, dst - offset, count);
			dst += count;
			continue;
		}

		if ((code & 0x40) == 0) {

			if (code == 0x80) {
				break;
			}

			/*
			 * Medium length: the bytes that follow are copied out as they stand.
			 */
			long count = (long)(code & 0x3F);

			if (count > maxlen) {
				count = maxlen;
			}

			Copy_Overlapped(dst, src, count);
			src += count;
			dst += count;
			continue;
		}

		long count = (long)(code & 0x3F) + 3;

		if (code == 0xFE) {

			/*
			 * Long run: one byte repeated a counted number of times.
			 */
			count = (long)Fetch_Word(src);

			unsigned char const value = *src++;

			if (count > maxlen) {
				count = maxlen;
			}

			for (long i = 0; i < count; i++) {
				dst[i] = value;
			}

			dst += count;
			continue;
		}

		if (code == 0xFF) {
			count = (long)Fetch_Word(src);
		}

		unsigned long const offset = Fetch_Word(src);
		unsigned char const * from = relative ? (dst - offset) : (start + offset);

		if (count > maxlen) {
			count = maxlen;
		}

		Copy_Overlapped(dst, from, count);
		dst += count;
	}

	return((unsigned long)(dst - start));
}
