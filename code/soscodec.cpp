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
*---------------------------------------------------------------------------
*
* DESCRIPTION
*     HMI SOS ADPCM decompression.
*
* PROGRAMMER
*     Nick Skrepetos
*     Denzil E. Long, Jr. (Fixed bugs, rewrote for watcom)
*     Bill Petro          (Added stereo support)
*     Jonathan Lanier
*
****************************************************************************/

#include "soscomp.h"

#include <cstring>

namespace {

/*
** ADPCM step table and per-token index adjustment. Every difference and index the two
** decoders below use is derived from these two tables.
*/

constexpr int SOS_STEP_COUNT = 89;
constexpr int SOS_TOKEN_COUNT = 16;

constexpr short SOSStepTable[SOS_STEP_COUNT] = {
	7,     8,     9,     10,    11,    12,    13,    14,
	16,    17,    19,    21,    23,    25,    28,    31,
	34,    37,    41,    45,    50,    55,    60,    66,
	73,    80,    88,    97,    107,   118,   130,   143,
	157,   173,   190,   209,   230,   253,   279,   307,
	337,   371,   408,   449,   494,   544,   598,   658,
	724,   796,   876,   963,   1060,  1166,  1282,  1411,
	1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
	3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
	7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

constexpr short SOSIndexAdjust[SOS_TOKEN_COUNT] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

constexpr int SOS_SAMPLE_MAX = 32767;
constexpr int SOS_SAMPLE_MIN = -32768;


int Step_Difference(int step, int token)
{
	int difference = step >> 3;

	if ((token & 4) != 0) {
		difference += step;
	}
	if ((token & 2) != 0) {
		difference += step >> 1;
	}
	if ((token & 1) != 0) {
		difference += step >> 2;
	}
	if ((token & 8) != 0) {
		difference = -difference;
	}

	return difference;
}


int Next_Index(int index, int token)
{
	int next = index + SOSIndexAdjust[token];

	if (next < 0) {
		next = 0;
	}
	if (next > SOS_STEP_COUNT - 1) {
		next = SOS_STEP_COUNT - 1;
	}

	return next;
}


int Clamp_Sample(int sample)
{
	if (sample > SOS_SAMPLE_MAX) {
		return SOS_SAMPLE_MAX;
	}
	if (sample < SOS_SAMPLE_MIN) {
		return SOS_SAMPLE_MIN;
	}

	return sample;
}


// The destination is a raw byte pointer that carries no alignment guarantee.
void Store_Sample_16(char * dest, int sample)
{
	short value = (short)sample;
	std::memcpy(dest, &value, sizeof(value));
}


/*
** Working copy of one channel of the general decoder's stream state. The fields mirror
** the _SOS_COMPRESS_INFO members the decoder reads and leaves behind.
*/
struct GeneralChannel {
	int Predicted;
	int Difference;
	int Index;
	int Step;
	unsigned short CodeBuf;
	unsigned short Code;
	unsigned long SampleIndex;
};


void General_Decode_Sample(GeneralChannel & channel, unsigned char const * & source, int source_stride,
	char * & dest, int dest_stride, bool sixteen_bit)
{
	/*
	** An even sample index starts a new token byte and takes its low nybble; the odd
	** sample that follows takes the high nybble of the byte already held.
	*/
	if ((channel.SampleIndex & 1) != 0) {
		channel.Code = (unsigned short)((channel.CodeBuf >> 4) & 0x000F);
	} else {
		channel.CodeBuf = (unsigned short)*source;
		source += source_stride;
		channel.Code = (unsigned short)(channel.CodeBuf & 0x000F);
	}

	int token = (int)channel.Code;

	// The step is carried in the stream state rather than re-derived from the index.
	channel.Difference = Step_Difference(channel.Step, token);
	channel.Predicted = Clamp_Sample(channel.Predicted + channel.Difference);

	if (sixteen_bit) {
		Store_Sample_16(dest, channel.Predicted);
	} else {
		*dest = (char)(((channel.Predicted >> 8) & 0xFF) ^ 0x80);
	}
	dest += dest_stride;

	channel.Index = Next_Index(channel.Index, token);
	channel.Step = SOSStepTable[channel.Index];
	channel.SampleIndex++;
}


void General_Load_Channel(GeneralChannel & channel, long predicted, long difference, short index, short step,
	short code_buf, short code)
{
	channel.Predicted = (int)predicted;
	channel.Difference = (int)difference;
	channel.Index = (int)index;
	channel.Step = (int)step;
	channel.CodeBuf = (unsigned short)code_buf;
	channel.Code = (unsigned short)code;
	channel.SampleIndex = 0;
}

}  // namespace


extern "C" {


void __cdecl sosCODECInitStream(_SOS_COMPRESS_INFO * info)
{
	info->wIndex = 0;
	info->dwPredicted = 0;
	info->wIndex2 = 0;
	info->dwPredicted2 = 0;
}


unsigned long __cdecl sosCODECDecompressData(_SOS_COMPRESS_INFO * info, unsigned long bytes)
{
	// Only 4:1 16-bit mono is implemented here; every other format decodes nothing.
	if (info->wBitSize != 16 || info->wChannels != 1) {
		return 0;
	}

	unsigned char const * source = (unsigned char const *)info->lpSource;
	char * dest = info->lpDest;

	int predicted = (int)info->dwPredicted;

	/*
	** This decoder keeps the step index in wIndex scaled by SOS_TOKEN_COUNT * 2, the
	** stride of the flattened difference table it indexes with. The scaling is part of
	** the stream state the caller carries between chunks.
	*/
	int index = (int)((unsigned short)info->wIndex >> 5);

	unsigned long samples = bytes >> 1;

	for (unsigned long i = 0; i < samples; i++) {
		unsigned int byte = source[i >> 1];
		int token = ((i & 1) != 0) ? (int)(byte >> 4) : (int)(byte & 0x0F);

		predicted = Clamp_Sample(predicted + Step_Difference(SOSStepTable[index], token));
		index = Next_Index(index, token);

		Store_Sample_16(dest, predicted);
		dest += 2;
	}

	info->dwPredicted = predicted;
	info->wIndex = (short)(index * SOS_TOKEN_COUNT * 2);

	return bytes;
}


void __cdecl General_sosCODECInitStream(_SOS_COMPRESS_INFO * info)
{
	info->wIndex = 0;
	info->wStep = 7;
	info->dwPredicted = 0;
	info->dwSampleIndex = 0;
	info->wIndex2 = 0;
	info->wStep2 = 7;
	info->dwPredicted2 = 0;
	info->dwSampleIndex2 = 0;
}


unsigned long __cdecl General_sosCODECDecompressData(_SOS_COMPRESS_INFO * info, unsigned long bytes)
{
	bool sixteen_bit = (info->wBitSize == 16);
	bool stereo = (info->wChannels == 2);

	info->dwSampleIndex = 0;
	info->dwSampleIndex2 = 0;

	unsigned long count = bytes;
	if (sixteen_bit) {
		count >>= 1;
	}

	GeneralChannel left;
	General_Load_Channel(left, info->dwPredicted, info->dwDifference, info->wIndex, info->wStep,
		info->wCodeBuf, info->wCode);

	unsigned char const * source = (unsigned char const *)info->lpSource;
	char * dest = info->lpDest;

	if (!stereo) {
		int dest_stride = sixteen_bit ? 2 : 1;

		for (unsigned long i = 0; i < count; i++) {
			General_Decode_Sample(left, source, 1, dest, dest_stride, sixteen_bit);
		}
	} else {
		/*
		** Stereo streams interleave whole token bytes, so each channel walks the source
		** two bytes at a time and the two channels are decoded one after the other.
		*/
		int dest_stride = sixteen_bit ? 4 : 2;
		unsigned long pairs = count / 2;

		for (unsigned long i = 0; i < pairs; i++) {
			General_Decode_Sample(left, source, 2, dest, dest_stride, sixteen_bit);
		}

		GeneralChannel right;
		General_Load_Channel(right, info->dwPredicted2, info->dwDifference2, info->wIndex2, info->wStep2,
			info->wCodeBuf2, info->wCode2);

		unsigned char const * right_source = (unsigned char const *)info->lpSource + 1;
		char * right_dest = info->lpDest + (sixteen_bit ? 2 : 1);

		for (unsigned long i = 0; i < pairs; i++) {
			General_Decode_Sample(right, right_source, 2, right_dest, dest_stride, sixteen_bit);
		}

		info->dwPredicted2 = right.Predicted;
		info->dwDifference2 = right.Difference;
		info->wIndex2 = (short)right.Index;
		info->wStep2 = (short)right.Step;
		info->wCodeBuf2 = (short)right.CodeBuf;
		info->wCode2 = (short)right.Code;
		info->dwSampleIndex2 = right.SampleIndex;
	}

	info->dwPredicted = left.Predicted;
	info->dwDifference = left.Difference;
	info->wIndex = (short)left.Index;
	info->wStep = (short)left.Step;
	info->wCodeBuf = (short)left.CodeBuf;
	info->wCode = (short)left.Code;
	info->dwSampleIndex = left.SampleIndex;

	return bytes;
}


}
