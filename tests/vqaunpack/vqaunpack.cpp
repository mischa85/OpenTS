/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Holds AudioUnzap and VQA_LCW_Uncompress in vqa_uncomp.cpp to the output the assembly they
// replaced produced. The vectors in vqagolden.h were recorded from that assembly before it
// was removed. Needs no game data.

#include <cstdio>
#include <cstring>

#include "vqalib/cmp.h"

#include "vqagolden.h"

namespace {

int const SRCMAX = 262144;
int const DSTMAX = 1048576;
unsigned char const GUARD = 0xA5;

unsigned char SourceStore[SRCMAX];
unsigned char DestStore[DSTMAX];

unsigned int Seed = 0;

int Failures = 0;
int Checked = 0;


unsigned int Next_Random(void)
{
	Seed = Seed * 1103515245u + 12345u;
	return(Seed >> 8);
}


// Must reproduce the generator's pattern exactly or every vector misses.
void Fill_Source(unsigned int seed)
{
	Seed = seed;
	for (int i = 0; i < SRCMAX; i++) {
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


void Check(bool condition, char const * what, long long expected, long long got)
{
	if (!condition) {
		std::printf("FAILED %s: expected %lld, got %lld\n", what, expected, got);
		Failures++;
	}
}


/*
 * Must build byte for byte what the generator built, or the recorded output describes a
 * different stream than the one replayed here.
 */
int Build_Stream(int which, unsigned char * out, bool relative)
{
	int at = 0;

	if (relative) {
		out[at++] = 0x00;
	}

	out[at++] = 0x80 | 0x10;
	for (int i = 0; i < 16; i++) {
		out[at++] = (unsigned char)(0x40 + i);
	}

	switch (which) {
		case 0:
			out[at++] = (unsigned char)(((5 - 3) << 4) | 0x00);
			out[at++] = 8;
			break;

		case 1:
			out[at++] = (unsigned char)(((10 - 3) << 4) | 0x00);
			out[at++] = 3;
			break;

		case 2:
			out[at++] = 0xFE;
			out[at++] = (unsigned char)(300 & 0xFF);
			out[at++] = (unsigned char)(300 >> 8);
			out[at++] = 0x5A;
			break;

		case 3:
			out[at++] = 0xFF;
			out[at++] = (unsigned char)(200 & 0xFF);
			out[at++] = (unsigned char)(200 >> 8);
			if (relative) {
				out[at++] = 12;
				out[at++] = 0;
			} else {
				out[at++] = 2;
				out[at++] = 0;
			}
			break;

		case 4:
			out[at++] = 0xC0 | 20;
			if (relative) {
				out[at++] = 10;
				out[at++] = 0;
			} else {
				out[at++] = 4;
				out[at++] = 0;
			}
			break;

		case 5:
			out[at++] = 0x80 | 0x20;
			for (int i = 0; i < 32; i++) {
				out[at++] = (unsigned char)(0x90 + i);
			}
			break;

		default:
			break;
	}

	out[at++] = 0x80;
	return(at);
}

}	// namespace


int main(void)
{
	for (int i = 0; i < UnzapGoldenCaseCount; i++) {
		UnzapGoldenCase const & test = UnzapGoldenCases[i];

		Fill_Source(test.Seed);
		std::memset(DestStore, GUARD, sizeof(DestStore));

		long const returned = AudioUnzap(SourceStore, DestStore, test.Bytes);
		unsigned long long const hash = Hash(DestStore, test.Bytes + 256);

		char label[128];
		std::snprintf(label, sizeof(label), "AudioUnzap %d bytes hash", test.Bytes);
		Check(hash == test.Hash, label, (long long)test.Hash, (long long)hash);

		std::snprintf(label, sizeof(label), "AudioUnzap %d bytes consumed", test.Bytes);
		Check(returned == test.Returned, label, test.Returned, returned);

		Checked++;
	}

	for (int i = 0; i < LcwGoldenCaseCount; i++) {
		LcwGoldenCase const & test = LcwGoldenCases[i];

		std::memset(SourceStore, 0, sizeof(SourceStore));
		std::memset(DestStore, GUARD, sizeof(DestStore));

		Build_Stream(test.Which, SourceStore, test.Relative != 0);

		unsigned long const returned = VQA_LCW_Uncompress((char const *)SourceStore, (char *)DestStore, test.Length);
		unsigned long long const hash = Hash(DestStore, (int)test.Length + 256);

		char label[128];
		std::snprintf(label, sizeof(label), "LCW stream %d rel %d len %lu hash", test.Which, test.Relative, test.Length);
		Check(hash == test.Hash, label, (long long)test.Hash, (long long)hash);

		std::snprintf(label, sizeof(label), "LCW stream %d rel %d len %lu written", test.Which, test.Relative, test.Length);
		Check(returned == test.Returned, label, (long long)test.Returned, (long long)returned);

		Checked++;
	}

	std::printf("%-52s %s\n", "VQA unzap and LCW match the recorded assembly", Failures == 0 ? "ok" : "FAILED");
	std::printf("checked %d cases, %d mismatches\n", Checked, Failures);

	return(Failures == 0 ? 0 : 1);
}
