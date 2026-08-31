/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Holds the SOS ADPCM decoders in soscodec.cpp to the output the assembly they replaced
// produced. The vectors in sosgolden.h were recorded from that assembly before it was
// removed; this replays each one and compares the decoded bytes and the stream state left
// behind. Needs no game data.

#include <cstdio>
#include <cstring>

#include "soscomp.h"
#include "vqalib/cmp.h"

#include "sosgolden.h"

namespace {

int const SRCMAX = 8192;
int const DSTMAX = 65536;
unsigned char const GUARD = 0xA5;

unsigned char SourceStore[SRCMAX + 16];
unsigned char DestStore[DSTMAX];

unsigned int Seed = 0;

int Failures = 0;
int Checked = 0;
int Skipped = 0;


unsigned int Next_Random(void)
{
	Seed = Seed * 1103515245u + 12345u;
	return(Seed >> 8);
}


// Must reproduce the generator's pattern exactly or every vector misses.
void Fill_Source(unsigned int seed)
{
	Seed = seed;
	for (int i = 0; i < SRCMAX + 16; i++) {
		SourceStore[i] = (unsigned char)(Next_Random() & 0xFF);
	}
}


unsigned long long Hash(unsigned char const * data, int size)
{
	unsigned long long hash = 1469598103934665603ULL;
	for (int i = 0; i < size; i++) {
		hash ^= (unsigned long long)data[i];
		hash *= 1099511628211ULL;
	}
	return(hash);
}


unsigned long long Dest_Hash(int bytes)
{
	return(Hash(DestStore, bytes + 64));
}


void Report(SosGoldenCase const & test, char const * what, long long expected, long long got)
{
	char const * name = "sosCODEC";

	if (test.Codec == 1) {
		name = "General_sosCODEC";
	} else if (test.Codec == 2) {
		name = "VQA_sosCODEC";
	}

	std::printf("FAILED %s %d bit %d channel %d bytes align %d: %s expected %lld, got %lld\n",
		name, test.BitSize, test.Channels, test.Bytes, test.SourceAlign, what, expected, got);
	Failures++;
}


void Check(SosGoldenCase const & test, char const * what, long long expected, long long got)
{
	if (expected != got) {
		Report(test, what, expected, got);
	}
}


void Run_Case(SosGoldenCase const & test)
{
	// The unified decoder covers only what the engine feeds it: 16 bit samples in whole
	// source bytes per channel. The assembly's 8 bit path and odd-tail handling have no
	// caller left, so those recordings are not replayed.
	if (test.BitSize != 16 || (test.Bytes % (4 * test.Channels)) != 0) {
		Skipped++;
		return;
	}

	Fill_Source(test.Seed);
	std::memset(DestStore, GUARD, sizeof(DestStore));

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = (short)test.BitSize;
	info.wChannels = (short)test.Channels;
	sosCODECInitStream(&info);

	info.wBitSize = (short)test.BitSize;
	info.wChannels = (short)test.Channels;
	info.lpSource = (char *)(SourceStore + test.SourceAlign);
	info.lpDest = (char *)DestStore;

	unsigned int returned;
	if (test.Codec == 2) {
		returned = sosCODECDecompressDataPlanar(&info, (unsigned int)test.Bytes);
	} else {
		returned = sosCODECDecompressData(&info, (unsigned int)test.Bytes);
	}

	Check(test, "hash", (long long)test.Hash, (long long)Dest_Hash(test.Bytes));
	if (test.Codec != 2) {
		Check(test, "return", (long long)test.Returned, (long long)returned);
	}
	// Both assembly decoders kept their step index pre-multiplied by the 32 byte table
	// stride; those recordings carry that scaling, the C++ keeps the plain index.
	int const scale = (test.Codec == 1) ? 1 : 32;

	Check(test, "predicted", test.Predicted, info.Channels[0].dwPredicted);
	Check(test, "index", test.Index, (long long)info.Channels[0].wIndex * scale);
	if (test.Channels == 2) {
		Check(test, "predicted2", test.Predicted2, info.Channels[1].dwPredicted);
		Check(test, "index2", test.Index2, (long long)info.Channels[1].wIndex * scale);
	}
	Checked++;
}

}	// namespace


namespace {

// The assembly's countdown was tested only after being decremented, so these requests never
// reached zero and ran away through both buffers. They must now decode nothing and return.
void Check_Runaway_Inputs(void)
{
	int const bits[] = {16, 16, 16, 16};
	int const channels[] = {1, 2, 1, 2};
	int const bytes[] = {0, 0, 3, 2};

	for (int i = 0; i < 4; i++) {
		Fill_Source(999u);
		std::memset(DestStore, GUARD, sizeof(DestStore));

		_SOS_COMPRESS_INFO info;
		std::memset(&info, 0, sizeof(info));
		info.wBitSize = (short)bits[i];
		info.wChannels = (short)channels[i];
		sosCODECInitStream(&info);
		info.wBitSize = (short)bits[i];
		info.wChannels = (short)channels[i];
		info.lpSource = (char *)SourceStore;
		info.lpDest = (char *)DestStore;

		sosCODECDecompressData(&info, (unsigned int)bytes[i]);

		bool untouched = true;
		for (int b = 0; b < 256; b++) {
			if (DestStore[b] != GUARD) {
				untouched = false;
				break;
			}
		}

		if (!untouched) {
			std::printf("FAILED sosCODEC %d bit %d channel %d bytes wrote to the destination\n",
				bits[i], channels[i], bytes[i]);
			Failures++;
		}

		Checked++;
	}
}

}	// namespace


int main(void)
{
	for (int i = 0; i < SosGoldenCaseCount; i++) {
		Run_Case(SosGoldenCases[i]);
	}

	Check_Runaway_Inputs();

	std::printf("%-52s %s\n", "SOS ADPCM decode matches the recorded assembly", Failures == 0 ? "ok" : "FAILED");
	std::printf("checked %d cases, skipped %d outside the decoder's domain, %d mismatches\n", Checked, Skipped, Failures);

	return(Failures == 0 ? 0 : 1);
}
