/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
 * Harness for the VQA player's compression primitives. It covers the LCW block
 * decoder, the SOS 4:1 ADPCM decoder, the zapped-audio decoder, and the UnVQ1 frame
 * decoders, none of which need game assets: every stream here is synthesised.
 *
 * The LCW decoder is checked three ways: against an independent model that walks the
 * same commands, against the engine's own LCW_Uncomp for the streams both formats
 * accept, and against hand-built edge cases. The ADPCM decoder is checked against
 * vectors derived by hand from the original assembly. The UnVQ1 decoders are checked
 * against per-pixel reference implementations.
 */

#include "cmp.h"
#include "lcw.h"
#include "unvq.h"

#include "_vqa.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

/* unvq.cpp reaches for the shared hicolor lookup that the engine owns. */
unsigned short * HicolorTable;

static int Failures = 0;
static int Checks = 0;


static void Check(bool condition, char const * what)
{
	Checks++;

	if (!condition) {
		Failures++;
		std::printf("FAIL: %s\n", what);
	}
}


static void Check_Bytes(std::vector<unsigned char> const & got, std::vector<unsigned char> const & want, char const * what)
{
	Checks++;

	if (got.size() != want.size()) {
		Failures++;
		std::printf("FAIL: %s (size %zu, expected %zu)\n", what, got.size(), want.size());
		return;
	}

	for (size_t i = 0; i < got.size(); i++) {
		if (got[i] != want[i]) {
			Failures++;
			std::printf("FAIL: %s (byte %zu is %02X, expected %02X)\n", what, i, got[i], want[i]);
			return;
		}
	}
}


/*---------------------------------------------------------------------------
 * LCW
 *-------------------------------------------------------------------------*/

/*
 * Builds an LCW stream together with the output it must produce. Every command is
 * emitted with operands the format can actually represent, so the model below stays
 * the definition of correct rather than a copy of the decoder.
 */
struct LCWStream
{
	std::vector<unsigned char> Data;
	std::vector<unsigned char> Expected;

	void Copy_From_Dest(size_t from, size_t count)
	{
		for (size_t i = 0; i < count; i++) {
			Expected.push_back(Expected[from + i]);
		}
	}
};


static void Build_Random_Stream(LCWStream & stream, std::mt19937 & rng, bool relative, bool lcw_uncomp_safe)
{
	std::uniform_int_distribution<int> byte(0, 255);
	std::uniform_int_distribution<int> op(0, 4);

	if (relative) {
		stream.Data.push_back(0);
	}

	/* Nothing has been produced yet, so the stream has to open with source bytes. */
	{
		int count = 1 + (byte(rng) % 63);
		stream.Data.push_back((unsigned char)(0x80 | count));

		for (int i = 0; i < count; i++) {
			unsigned char value = (unsigned char)byte(rng);
			stream.Data.push_back(value);
			stream.Expected.push_back(value);
		}
	}

	int commands = 40;

	while (commands--) {
		size_t produced = stream.Expected.size();

		switch (op(rng)) {

		case 0: {
			/* Short copy from destination. */
			size_t offset = 1 + (size_t)(byte(rng) % (produced < 4095 ? produced : 4095));
			int count = 3 + (byte(rng) % 8);

			stream.Data.push_back((unsigned char)((((count - 3) << 4) & 0x70) | ((offset >> 8) & 0x0F)));
			stream.Data.push_back((unsigned char)(offset & 0xFF));
			stream.Copy_From_Dest(produced - offset, (size_t)count);
		} break;

		case 1: {
			/* Medium copy from source. */
			int count = 1 + (byte(rng) % 63);
			stream.Data.push_back((unsigned char)(0x80 | count));

			for (int i = 0; i < count; i++) {
				unsigned char value = (unsigned char)byte(rng);
				stream.Data.push_back(value);
				stream.Expected.push_back(value);
			}
		} break;

		case 2: {
			/* Long run of one byte. LCW_Uncomp mishandles runs shorter than its
			 * dword alignment pad, so keep those out of the shared streams.
			 */
			int count = (lcw_uncomp_safe ? 8 : 1) + (byte(rng) % 200);
			unsigned char value = (unsigned char)byte(rng);

			stream.Data.push_back(0xFE);
			stream.Data.push_back((unsigned char)(count & 0xFF));
			stream.Data.push_back((unsigned char)((count >> 8) & 0xFF));
			stream.Data.push_back(value);

			for (int i = 0; i < count; i++) {
				stream.Expected.push_back(value);
			}
		} break;

		case 3: {
			/* Long copy from destination. */
			size_t offset = (size_t)(byte(rng) % (produced < 60000 ? produced : 60000));
			int count = 1 + (byte(rng) % 200);

			stream.Data.push_back(0xFF);
			stream.Data.push_back((unsigned char)(count & 0xFF));
			stream.Data.push_back((unsigned char)((count >> 8) & 0xFF));

			size_t encoded = relative ? (produced - offset) : offset;
			stream.Data.push_back((unsigned char)(encoded & 0xFF));
			stream.Data.push_back((unsigned char)((encoded >> 8) & 0xFF));
			stream.Copy_From_Dest(offset, (size_t)count);
		} break;

		default: {
			/* Medium copy from destination. */
			size_t offset = (size_t)(byte(rng) % (produced < 60000 ? produced : 60000));
			int count = 3 + (byte(rng) % 61);

			stream.Data.push_back((unsigned char)(0xC0 | (count - 3)));

			size_t encoded = relative ? (produced - offset) : offset;
			stream.Data.push_back((unsigned char)(encoded & 0xFF));
			stream.Data.push_back((unsigned char)((encoded >> 8) & 0xFF));
			stream.Copy_From_Dest(offset, (size_t)count);
		} break;
		}
	}

	stream.Data.push_back(0x80);
}


static std::vector<unsigned char> Run_Vqa_Lcw(LCWStream const & stream, size_t limit)
{
	/* The slack absorbs LCW_Uncomp's dword writes when the same buffer size is
	 * handed to both decoders.
	 */
	std::vector<unsigned char> dest(limit + 16, 0xCD);

	unsigned long written = VQA_LCW_Uncompress((char const *)stream.Data.data(), (char *)dest.data(), (unsigned long)limit);

	Check(written == limit, "VQA_LCW_Uncompress reports the bytes it wrote");

	bool clean = true;
	for (size_t i = limit; i < dest.size(); i++) {
		clean = clean && (dest[i] == 0xCD);
	}

	Check(clean, "VQA_LCW_Uncompress stays inside the destination");

	dest.resize(limit);
	return dest;
}


static void Test_Lcw()
{
	std::mt19937 rng(20260829u);

	for (int trial = 0; trial < 64; trial++) {
		LCWStream stream;
		Build_Random_Stream(stream, rng, false, true);

		std::vector<unsigned char> got = Run_Vqa_Lcw(stream, stream.Expected.size());
		Check_Bytes(got, stream.Expected, "absolute stream matches the model");

		std::vector<unsigned char> reference(stream.Expected.size() + 16, 0);
		int written = LCW_Uncomp(stream.Data.data(), reference.data(), 0);
		reference.resize(stream.Expected.size());

		Check((size_t)written == stream.Expected.size(), "LCW_Uncomp reports the same length");
		Check_Bytes(reference, stream.Expected, "LCW_Uncomp agrees with the model");
		Check_Bytes(got, reference, "VQA_LCW_Uncompress agrees with LCW_Uncomp");
	}

	for (int trial = 0; trial < 64; trial++) {
		LCWStream stream;
		Build_Random_Stream(stream, rng, false, false);

		std::vector<unsigned char> got = Run_Vqa_Lcw(stream, stream.Expected.size());
		Check_Bytes(got, stream.Expected, "short long-runs match the model");
	}

	for (int trial = 0; trial < 64; trial++) {
		LCWStream stream;
		Build_Random_Stream(stream, rng, true, false);

		std::vector<unsigned char> got = Run_Vqa_Lcw(stream, stream.Expected.size());
		Check_Bytes(got, stream.Expected, "relative stream matches the model");
	}

	/* Truncation: every command is clipped to the destination that is left. */
	for (int trial = 0; trial < 32; trial++) {
		LCWStream stream;
		Build_Random_Stream(stream, rng, false, false);

		size_t limit = stream.Expected.size() / (2 + (size_t)(trial % 4));
		std::vector<unsigned char> want(stream.Expected.begin(), stream.Expected.begin() + (ptrdiff_t)limit);
		std::vector<unsigned char> got = Run_Vqa_Lcw(stream, limit);

		Check_Bytes(got, want, "clipped stream stops at the destination end");
	}

	/* An empty destination consumes nothing and produces nothing. */
	{
		unsigned char source[] = {0x81, 0x41, 0x80};
		unsigned char dest[4] = {0xCD, 0xCD, 0xCD, 0xCD};

		Check(VQA_LCW_Uncompress((char const *)source, (char *)dest, 0) == 0, "zero length writes nothing");
		Check(dest[0] == 0xCD, "zero length leaves the destination alone");
	}

	/* An immediate end code. */
	{
		unsigned char source[] = {0x80};
		unsigned char dest[4] = {0xCD, 0xCD, 0xCD, 0xCD};

		Check(VQA_LCW_Uncompress((char const *)source, (char *)dest, 4) == 0, "an immediate end code writes nothing");
		Check(dest[0] == 0xCD, "an immediate end code leaves the destination alone");
	}

	/* A single source byte. */
	{
		unsigned char source[] = {0x81, 0x5A, 0x80};
		unsigned char dest[4] = {0xCD, 0xCD, 0xCD, 0xCD};

		Check(VQA_LCW_Uncompress((char const *)source, (char *)dest, 4) == 1, "a one byte copy writes one byte");
		Check(dest[0] == 0x5A && dest[1] == 0xCD, "a one byte copy writes the right byte");
	}

	/* A back reference one byte behind runs that byte forward. */
	{
		unsigned char source[] = {0x81, 0x11, 0x70, 0x01, 0x80};
		unsigned char dest[16];
		std::memset(dest, 0xCD, sizeof(dest));

		unsigned long written = VQA_LCW_Uncompress((char const *)source, (char *)dest, sizeof(dest));

		Check(written == 11, "an overlapping short copy runs to full length");

		bool ok = true;
		for (int i = 0; i < 11; i++) {
			ok = ok && (dest[i] == 0x11);
		}

		Check(ok, "an overlapping short copy replicates the source byte");
		Check(dest[11] == 0xCD, "an overlapping short copy stops on time");
	}

	/* A long run larger than the destination is clipped, not wrapped. */
	{
		unsigned char source[] = {0x81, 0x01, 0xFE, 0xFF, 0xFF, 0x77, 0x80};
		unsigned char dest[8];
		std::memset(dest, 0xCD, sizeof(dest));

		unsigned long written = VQA_LCW_Uncompress((char const *)source, (char *)dest, sizeof(dest));

		Check(written == sizeof(dest), "an oversized run fills the destination exactly");
		Check(dest[0] == 0x01 && dest[1] == 0x77 && dest[7] == 0x77, "an oversized run writes the run byte");
	}
}


/*---------------------------------------------------------------------------
 * SOS ADPCM
 *-------------------------------------------------------------------------*/

static void Check_Samples(short const * got, short const * want, int count, char const * what)
{
	Checks++;

	for (int i = 0; i < count; i++) {
		if (got[i] != want[i]) {
			Failures++;
			std::printf("FAIL: %s (sample %d is %d, expected %d)\n", what, i, got[i], want[i]);
			return;
		}
	}
}


static void Test_Adpcm()
{
	/*
	 * Hand-derived from the assembly. Starting from a cleared stream the step size
	 * is 7, so nybble 2 adds 7>>1, nybble 1 adds 7>>2, nybble 0 adds 7>>3 and
	 * nybble 15 subtracts 7>>3 + 7 + 7>>1 + 7>>2. Only nybble 15 moves the step
	 * index, by +8.
	 */
	{
		unsigned char source[2] = {0x12, 0xF0};
		short dest[4] = {0, 0, 0, 0};
		short want[4] = {3, 4, 4, -7};
		_VQA_SOS_COMPRESS_INFO info;

		VQA_sosCODECInitStream(&info);
		Check(info.dwPredicted == 0 && info.wIndex == 0 && info.dwPredicted2 == 0 && info.wIndex2 == 0, "init clears both channels");

		VQA_sosCODECDecompressData(source, dest, 16, 1, 8, &info);

		Check_Samples(dest, want, 4, "mono decode matches the hand derived vector");
		Check(info.dwPredicted == -7, "mono decode leaves the last sample as the predictor");
		Check(info.wIndex == 8 * 32, "mono decode leaves the step index scaled by 32");
	}

	/*
	 * Saturation, from the top of the step table. Step 32767 gives a magnitude of
	 * 4095 + 32767 + 16383 + 8191 = 61436 for nybbles 7 and 15.
	 */
	{
		unsigned char source[2] = {0xF7, 0x0F};
		short dest[4] = {0, 0, 0, 0};
		short want[4] = {32767, -28669, -32768, -28673};
		_VQA_SOS_COMPRESS_INFO info;

		VQA_sosCODECInitStream(&info);
		info.dwPredicted = 32000;
		info.wIndex = 88 * 32;

		VQA_sosCODECDecompressData(source, dest, 16, 1, 8, &info);

		Check_Samples(dest, want, 4, "mono decode saturates at both ends");
		Check(info.wIndex == 87 * 32, "the step index walks back down");
	}

	/* Spot values lifted from the original expanded difference table. */
	{
		unsigned char source[1] = {0x00};
		short dest[2] = {0, 0};
		_VQA_SOS_COMPRESS_INFO info;

		/* Step index 45 (step 544), nybble 0 adds 544>>3 = 68. */
		VQA_sosCODECInitStream(&info);
		info.wIndex = 45 * 32;
		VQA_sosCODECDecompressData(source, dest, 16, 1, 2, &info);
		Check(dest[0] == 68, "step 45 nybble 0 adds 68");

		/* Step index 88 (step 32767), nybble 0 adds 4095. */
		VQA_sosCODECInitStream(&info);
		info.wIndex = 88 * 32;
		VQA_sosCODECDecompressData(source, dest, 16, 1, 2, &info);
		Check(dest[0] == 4095, "step 88 nybble 0 adds 4095");
	}

	/* Anything but 16 bit is refused outright. */
	{
		unsigned char source[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
		short dest[8];
		_VQA_SOS_COMPRESS_INFO info;

		std::memset(dest, 0x7B, sizeof(dest));
		VQA_sosCODECInitStream(&info);
		VQA_sosCODECDecompressData(source, dest, 8, 1, 16, &info);

		bool untouched = true;
		for (int i = 0; i < 8; i++) {
			untouched = untouched && (dest[i] == (short)0x7B7B);
		}

		Check(untouched, "an 8 bit stream is left alone");
		Check(info.dwPredicted == 0 && info.wIndex == 0, "an 8 bit stream leaves the state alone");
	}

	/*
	 * A stereo stream is two independent mono streams, the right channel starting
	 * an eighth of the uncompressed size into the source.
	 */
	{
		unsigned char source[8] = {0x3C, 0x91, 0x5F, 0x02, 0xE7, 0x48, 0xB1, 0x6D};
		short interleaved[16];
		short left[8];
		short right[8];
		_VQA_SOS_COMPRESS_INFO stereo;
		_VQA_SOS_COMPRESS_INFO mono;

		VQA_sosCODECInitStream(&stereo);
		VQA_sosCODECDecompressData(source, interleaved, 16, 2, 32, &stereo);

		VQA_sosCODECInitStream(&mono);
		VQA_sosCODECDecompressData(source, left, 16, 1, 16, &mono);
		long left_predicted = mono.dwPredicted;
		short left_index = mono.wIndex;

		VQA_sosCODECInitStream(&mono);
		VQA_sosCODECDecompressData(source + 4, right, 16, 1, 16, &mono);

		bool ok = true;
		for (int i = 0; i < 8; i++) {
			ok = ok && (interleaved[i * 2] == left[i]) && (interleaved[i * 2 + 1] == right[i]);
		}

		Check(ok, "a stereo stream interleaves two independent channels");
		Check(stereo.dwPredicted == left_predicted && stereo.wIndex == left_index, "the left channel state lands in the first slot");
		Check(stereo.dwPredicted2 == mono.dwPredicted && stereo.wIndex2 == mono.wIndex, "the right channel state lands in the second slot");
	}
}


/*---------------------------------------------------------------------------
 * Zapped audio
 *-------------------------------------------------------------------------*/

static void Test_Audio_Unzap()
{
	/* A silence run repeats the running sample, which starts at 0x80. */
	{
		unsigned char source[] = {0xC5};
		unsigned char dest[8];
		std::memset(dest, 0xCD, sizeof(dest));

		Check(AudioUnzap(source, dest, 6) == 1, "a silence run reads one byte");

		bool ok = true;
		for (int i = 0; i < 6; i++) {
			ok = ok && (dest[i] == 0x80);
		}

		Check(ok, "a silence run repeats the previous sample");
		Check(dest[6] == 0xCD, "a silence run writes exactly its length");
	}

	/* A raw run copies the bytes that follow and adopts the last one. */
	{
		unsigned char source[] = {0x83, 0x10, 0x20, 0x30, 0x40, 0xC0};
		unsigned char dest[8];
		std::memset(dest, 0xCD, sizeof(dest));

		Check(AudioUnzap(source, dest, 5) == 6, "a raw run counts its payload");
		Check(dest[0] == 0x10 && dest[3] == 0x40, "a raw run copies its payload");
		Check(dest[4] == 0x40, "a raw run leaves the last byte as the previous sample");
	}

	/* A five bit delta, sign extended. */
	{
		unsigned char source[] = {0xBF};
		unsigned char dest[4];
		std::memset(dest, 0xCD, sizeof(dest));

		Check(AudioUnzap(source, dest, 1) == 1, "a delta code reads one byte");
		Check(dest[0] == 0x7F, "a delta code of -1 steps down from 0x80");
	}

	/* Two bit deltas run from the low bits of each packed byte. */
	{
		unsigned char source[] = {0x00, 0xE4};
		unsigned char dest[8];
		std::memset(dest, 0xCD, sizeof(dest));

		Check(AudioUnzap(source, dest, 4) == 2, "a two bit block reads its packed byte");
		Check(dest[0] == 0x7E && dest[1] == 0x7D && dest[2] == 0x7D && dest[3] == 0x7E, "two bit deltas run low bits first");
	}

	/* Four bit deltas run low nybble first. */
	{
		unsigned char source[] = {0x40, 0x08};
		unsigned char dest[8];
		std::memset(dest, 0xCD, sizeof(dest));

		Check(AudioUnzap(source, dest, 2) == 2, "a four bit block reads its packed byte");
		Check(dest[0] == 0x80 && dest[1] == 0x77, "four bit deltas run low nybble first");
	}

	/* Deltas saturate rather than wrap. */
	{
		unsigned char source[] = {0x83, 0x02, 0x02, 0x02, 0x02, 0x40, 0x00};
		unsigned char dest[8];
		std::memset(dest, 0xCD, sizeof(dest));

		AudioUnzap(source, dest, 6);

		Check(dest[4] == 0x00, "a four bit delta saturates at zero");
	}

	/* A refused call reports nothing consumed. */
	{
		unsigned char dest[4];

		Check(AudioUnzap(nullptr, dest, 4) == 0, "a null source is refused");
		Check(AudioUnzap(dest, nullptr, 4) == 0, "a null destination is refused");
		Check(AudioUnzap(dest, dest, 0) == 0, "a zero length is refused");
	}
}


/*---------------------------------------------------------------------------
 * UnVQ1 frame decoders
 *-------------------------------------------------------------------------*/

static uint32_t Block_Pointer(unsigned char const * pointers, uint32_t block, uint32_t entries)
{
	return ((uint32_t)pointers[block + entries] << 8) | pointers[block];
}


static void Ref_UnVQ_4x2(unsigned char const * codebook, unsigned char const * pointers, unsigned char * buffer, uint32_t blocksperrow, uint32_t numrows, uint32_t bufwidth)
{
	uint32_t entries = numrows * blocksperrow;

	for (uint32_t r = 0; r < numrows; r++) {
		for (uint32_t c = 0; c < blocksperrow; c++) {
			uint32_t index = Block_Pointer(pointers, r * blocksperrow + c, entries);
			unsigned char * base = buffer + r * bufwidth * 2 + c * 4;

			for (uint32_t y = 0; y < 2; y++) {
				for (uint32_t x = 0; x < 4; x++) {
					base[y * bufwidth + x] = ((index >> 8) == 0xFFu) ? (unsigned char)index : codebook[index * 8 + y * 4 + x];
				}
			}
		}
	}
}


static void Ref_UnVQ_4x4(unsigned char const * codebook, unsigned char const * pointers, unsigned char * buffer, uint32_t blocksperrow, uint32_t numrows, uint32_t bufwidth)
{
	uint32_t entries = numrows * blocksperrow;

	for (uint32_t r = 0; r < numrows; r++) {
		for (uint32_t c = 0; c < blocksperrow; c++) {
			uint32_t index = Block_Pointer(pointers, r * blocksperrow + c, entries);
			unsigned char * base = buffer + r * bufwidth * 4 + c * 4;

			for (uint32_t y = 0; y < 4; y++) {
				for (uint32_t x = 0; x < 4; x++) {
					base[y * bufwidth + x] = ((index >> 8) == 0xFFu) ? (unsigned char)index : codebook[index * 16 + y * 4 + x];
				}
			}
		}
	}
}


static void Ref_UnVQ_4x4_Half(unsigned char const * codebook, unsigned char const * pointers, unsigned char * buffer, uint32_t blocksperrow, uint32_t numrows, uint32_t bufwidth)
{
	uint32_t entries = numrows * blocksperrow;

	for (uint32_t r = 0; r < numrows; r++) {
		for (uint32_t c = 0; c < blocksperrow; c++) {
			uint32_t index = Block_Pointer(pointers, r * blocksperrow + c, entries);
			unsigned char * base = buffer + r * bufwidth * 2 + c * 2;

			for (uint32_t y = 0; y < 2; y++) {
				for (uint32_t x = 0; x < 2; x++) {
					base[y * bufwidth + x] = ((index >> 8) == 0xFFu) ? (unsigned char)index : codebook[index * 16 + y * 8 + x * 2];
				}
			}
		}
	}
}


/*
 * The color mode 1 decoders share a 16 bit pixel layout. `lines` names the codeword
 * rows they draw and where each lands inside the four line band.
 */
static void Ref_UnVQ_C1(unsigned char const * codebook, unsigned char const * pointers, unsigned char * buffer, uint32_t blocksperrow, uint32_t numrows, uint32_t bufwidth, uint32_t const * lines, uint32_t linecount, bool table)
{
	uint32_t entries = numrows * blocksperrow;
	uint32_t pitch = bufwidth * 2;

	for (uint32_t r = 0; r < numrows; r++) {
		for (uint32_t c = 0; c < blocksperrow; c++) {
			uint32_t index = Block_Pointer(pointers, r * blocksperrow + c, entries);
			unsigned char * base = buffer + r * pitch * 4 + c * 8;

			for (uint32_t l = 0; l < linecount; l++) {
				uint32_t y = lines[l];

				for (uint32_t x = 0; x < 4; x++) {
					unsigned char * out = base + y * pitch + x * 2;

					if (index & 0x8000u) {
						uint16_t pixel = table ? HicolorTable[index & 0x7FFFu] : (uint16_t)(index & 0x7FFFu);
						std::memcpy(out, &pixel, 2);
					} else {
						std::memcpy(out, codebook + index * 32 + y * 8 + x * 2, 2);
					}
				}
			}
		}
	}
}


struct Frame
{
	std::vector<unsigned char> Codebook;
	std::vector<unsigned char> Pointers;
	uint32_t BlocksPerRow;
	uint32_t NumRows;
	uint32_t BufWidth;
};


static Frame Make_Frame(std::mt19937 & rng, uint32_t blocksperrow, uint32_t numrows, uint32_t bufwidth, uint32_t codeword_size, int onecolor_mode)
{
	std::uniform_int_distribution<int> byte(0, 255);
	Frame frame;

	frame.BlocksPerRow = blocksperrow;
	frame.NumRows = numrows;
	frame.BufWidth = bufwidth;

	/*
	 * Indices are kept under 0x400 so that both planes of the pointer stream carry
	 * data while every codeword still lands inside the codebook.
	 */
	uint32_t const codewords = 0x400;

	frame.Codebook.resize(codewords * codeword_size);
	for (size_t i = 0; i < frame.Codebook.size(); i++) {
		frame.Codebook[i] = (unsigned char)byte(rng);
	}

	uint32_t entries = blocksperrow * numrows;
	frame.Pointers.resize(entries * 2u);

	for (uint32_t i = 0; i < entries; i++) {
		bool solid = (byte(rng) % 3) == 0;

		frame.Pointers[i] = (unsigned char)byte(rng);

		if (onecolor_mode == 0) {
			frame.Pointers[i + entries] = solid ? 0xFF : (unsigned char)(byte(rng) % 4);
		} else {
			frame.Pointers[i + entries] = solid ? (unsigned char)(0x80 | (byte(rng) % 0x80)) : (unsigned char)(byte(rng) % 4);
		}
	}

	return frame;
}


static void Test_Unvq()
{
	std::mt19937 rng(19950208u);

	std::vector<unsigned short> hicolor(0x8000u);
	for (size_t i = 0; i < hicolor.size(); i++) {
		hicolor[i] = (unsigned short)((i * 2654435761u) >> 13);
	}
	HicolorTable = hicolor.data();

	struct Geometry
	{
		uint32_t BlocksPerRow;
		uint32_t NumRows;
		uint32_t BufWidth;
	};

	Geometry const geometries[] = {
		{1, 1, 4},
		{2, 3, 8},
		{80, 50, 320},
		{7, 5, 40}
	};

	for (Geometry const & g : geometries) {

		/* Color mode 0, 4x2 blocks. */
		{
			Frame frame = Make_Frame(rng, g.BlocksPerRow, g.NumRows, g.BufWidth, 8, 0);
			size_t size = (size_t)g.BufWidth * g.NumRows * 2 + 64;

			std::vector<unsigned char> got(size, 0xA5);
			std::vector<unsigned char> want(size, 0xA5);

			ASM_UnVQ_4x2(frame.Codebook.data(), frame.Pointers.data(), got.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);
			Ref_UnVQ_4x2(frame.Codebook.data(), frame.Pointers.data(), want.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);

			Check_Bytes(got, want, "ASM_UnVQ_4x2 matches the reference");
		}

		/* Color mode 0, 4x4 blocks. */
		{
			Frame frame = Make_Frame(rng, g.BlocksPerRow, g.NumRows, g.BufWidth, 16, 0);
			size_t size = (size_t)g.BufWidth * g.NumRows * 4 + 64;

			std::vector<unsigned char> got(size, 0xA5);
			std::vector<unsigned char> want(size, 0xA5);

			ASM_UnVQ_4x4(frame.Codebook.data(), frame.Pointers.data(), got.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);
			Ref_UnVQ_4x4(frame.Codebook.data(), frame.Pointers.data(), want.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);

			Check_Bytes(got, want, "ASM_UnVQ_4x4 matches the reference");
		}

		/* Color mode 0, 4x4 blocks at half resolution. */
		{
			Frame frame = Make_Frame(rng, g.BlocksPerRow, g.NumRows, g.BufWidth, 16, 0);
			size_t size = (size_t)g.BufWidth * g.NumRows * 2 + 64;

			std::vector<unsigned char> got(size, 0xA5);
			std::vector<unsigned char> want(size, 0xA5);

			ASM_UnVQ_4x4_HALF(frame.Codebook.data(), frame.Pointers.data(), got.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);
			Ref_UnVQ_4x4_Half(frame.Codebook.data(), frame.Pointers.data(), want.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);

			Check_Bytes(got, want, "ASM_UnVQ_4x4_HALF matches the reference");
		}

		/* Color mode 1. */
		{
			Frame frame = Make_Frame(rng, g.BlocksPerRow, g.NumRows, g.BufWidth, 32, 1);
			size_t size = (size_t)g.BufWidth * 2 * g.NumRows * 4 + 64;

			uint32_t const alllines[] = {0, 1, 2, 3};
			uint32_t const altlines[] = {0, 2};

			{
				std::vector<unsigned char> got(size, 0xA5);
				std::vector<unsigned char> want(size, 0xA5);

				ASM_UnVQ1_C1_4x4(frame.Codebook.data(), frame.Pointers.data(), got.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);
				Ref_UnVQ_C1(frame.Codebook.data(), frame.Pointers.data(), want.data(), g.BlocksPerRow, g.NumRows, g.BufWidth, alllines, 4, false);

				Check_Bytes(got, want, "ASM_UnVQ1_C1_4x4 matches the reference");
			}

			{
				std::vector<unsigned char> got(size, 0xA5);
				std::vector<unsigned char> want(size, 0xA5);

				ASM_UnVQ1_C1_TABLE(frame.Codebook.data(), frame.Pointers.data(), got.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);
				Ref_UnVQ_C1(frame.Codebook.data(), frame.Pointers.data(), want.data(), g.BlocksPerRow, g.NumRows, g.BufWidth, alllines, 4, true);

				Check_Bytes(got, want, "ASM_UnVQ1_C1_TABLE matches the reference");
			}

			{
				std::vector<unsigned char> got(size, 0xA5);
				std::vector<unsigned char> want(size, 0xA5);

				ASM_UnVQ1_C1_TABLE_ALT(frame.Codebook.data(), frame.Pointers.data(), got.data(), g.BlocksPerRow, g.NumRows, g.BufWidth);
				Ref_UnVQ_C1(frame.Codebook.data(), frame.Pointers.data(), want.data(), g.BlocksPerRow, g.NumRows, g.BufWidth, altlines, 2, true);

				Check_Bytes(got, want, "ASM_UnVQ1_C1_TABLE_ALT matches the reference");
			}
		}
	}

	HicolorTable = nullptr;
}


int main()
{
	Test_Lcw();
	Test_Adpcm();
	Test_Audio_Unzap();
	Test_Unvq();

	std::printf("%d checks, %d failures\n", Checks, Failures);

	return (Failures == 0) ? 0 : 1;
}
