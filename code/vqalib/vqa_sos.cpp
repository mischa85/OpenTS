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
*          Copyright (c) 1994, HMI, INC. All Rights Reserved
*
*----------------------------------------------------------------------------
*
* DESCRIPTION
*     HMI SOS ADPCM decompression, as consumed by the VQA player.
*
* PROGRAMMER
*     Nick Skrepetos
*     Denzil E. Long, Jr. (Fixed bugs, rewrote for watcom)
*     Bill Petro          (Added stereo support)
*     Jonathan Lanier
*
* DATE
*     Febuary 15, 1995
*
****************************************************************************/

#include "cmp.h"

namespace {

/*
 * The decoder state is carried as a byte offset rather than a plain step index:
 * wIndex holds step * 32, and a nybble contributes nybble * 2 on top of it, so a
 * single value addresses both expanded tables. The saved value always has its low
 * five bits clear, which is what lets the nybble be merged in with an OR.
 */
constexpr int STEP_COUNT = 89;
constexpr int NYBBLE_COUNT = 16;

constexpr short StepTable[STEP_COUNT] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
	34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
	724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
	3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

constexpr signed char IndexAdjust[NYBBLE_COUNT] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

struct CodecTables
{
	long Difference[STEP_COUNT * NYBBLE_COUNT];
	unsigned short Index[STEP_COUNT * NYBBLE_COUNT];
};


constexpr CodecTables Build_Codec_Tables()
{
	CodecTables tables{};

	for (int step = 0; step < STEP_COUNT; step++) {
		for (int nybble = 0; nybble < NYBBLE_COUNT; nybble++) {
			int size = StepTable[step];
			int difference = size >> 3;

			if (nybble & 4) {
				difference += size;
			}

			if (nybble & 2) {
				difference += size >> 1;
			}

			if (nybble & 1) {
				difference += size >> 2;
			}

			int next = step + IndexAdjust[nybble];

			if (next < 0) {
				next = 0;
			}

			if (next > (STEP_COUNT - 1)) {
				next = STEP_COUNT - 1;
			}

			tables.Difference[step * NYBBLE_COUNT + nybble] = (nybble & 8) ? -difference : difference;
			tables.Index[step * NYBBLE_COUNT + nybble] = (unsigned short)(next * 32);
		}
	}

	return tables;
}

constexpr CodecTables Tables = Build_Codec_Tables();


/// <summary>Decodes one 4:1 ADPCM channel into 16 bit samples.</summary>
/// <param name="source">Packed nybble stream, low nybble of each byte first.</param>
/// <param name="dest">First sample slot to write.</param>
/// <param name="stride">Sample slots between consecutive writes.</param>
/// <param name="samples">Number of samples to produce.</param>
/// <param name="predicted">Running predictor, updated on return.</param>
/// <param name="index">Running step offset, updated on return.</param>
void Decode_Channel(unsigned char const * source, short * dest, unsigned long stride, unsigned long samples, long & predicted, short & index)
{
	long sample = predicted;
	unsigned int offset = (unsigned int)(unsigned short)index;

	for (unsigned long i = 0; i < samples; i++) {
		unsigned int nybble = (i & 1) ? (unsigned int)(source[i >> 1] >> 4) : (unsigned int)(source[i >> 1] & 0x0F);
		unsigned int entry = (offset | (nybble << 1)) >> 1;

		sample += Tables.Difference[entry];
		offset = Tables.Index[entry];

		if (sample > 32767) {
			sample = 32767;
		} else if (sample < -32768) {
			sample = -32768;
		}

		*dest = (short)sample;
		dest += stride;
	}

	predicted = sample;
	index = (short)offset;
}

} // namespace


/// <summary>Resets an ADPCM stream to its starting predictor and step.</summary>
/// <param name="sosinfo">Compression information structure.</param>
void __cdecl VQA_sosCODECInitStream(_VQA_SOS_COMPRESS_INFO * sosinfo)
{
	sosinfo->wIndex = 0;
	sosinfo->dwPredicted = 0;
	sosinfo->wIndex2 = 0;
	sosinfo->dwPredicted2 = 0;
}


/// <summary>Decompresses a 4:1 ADPCM stream.</summary>
/// <remarks>
/// Only 16 bit mono and stereo are implemented; any other format leaves the
/// destination untouched. A stereo stream stores each channel's nybbles in its own
/// half of the source, the right channel starting dwUnCompSize/8 bytes in.
/// </remarks>
/// <param name="src">Compressed nybble stream.</param>
/// <param name="dst">Buffer to decompress into.</param>
/// <param name="wBitSize">Samples size in bits.</param>
/// <param name="wChannels">Number of channels.</param>
/// <param name="dwUnCompSize">Size of the decompressed data in bytes.</param>
/// <param name="sosinfo">Compression information structure.</param>
void __cdecl VQA_sosCODECDecompressData(void * src, void * dst, unsigned short wBitSize, unsigned short wChannels, unsigned long dwUnCompSize, _VQA_SOS_COMPRESS_INFO * sosinfo)
{
	if (wBitSize != 16) {
		return;
	}

	unsigned char const * source = (unsigned char const *)src;
	short * dest = (short *)dst;

	if (wChannels == 1) {
		Decode_Channel(source, dest, 1, dwUnCompSize >> 1, sosinfo->dwPredicted, sosinfo->wIndex);
	} else if (wChannels == 2) {
		unsigned long samples = dwUnCompSize >> 2;

		Decode_Channel(source, dest, 2, samples, sosinfo->dwPredicted, sosinfo->wIndex);
		Decode_Channel(source + (dwUnCompSize >> 3), dest + 1, 2, samples, sosinfo->dwPredicted2, sosinfo->wIndex2);
	}
}
