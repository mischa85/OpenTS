/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises LCW_Uncompress, the library entry point iff.h declares and the VQA player's
// caption loader calls. Its blocks arrive inside movie files, so the streams here include
// the ones a damaged or hostile file can carry as well as the ones the compressor
// produces. No game data is read: every stream is either built by LCW_Comp or written out
// by hand.
//
// The prototype comes from iff.h rather than from this file, so that the harness also
// establishes that the declaration the engine compiles against matches the definition.
//
// The entry point takes no source length, so every stream below stays readable through
// to its end of data code even where a command is refused. What is under test is that
// the decoder stops inside the destination it was given.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "iff.h"
#include "lcw.h"


static int Failures = 0;
static int Checks = 0;


static void Report_Failure(char const * name, char const * detail)
{
	Failures++;
	printf("FAIL %s: %s\n", name, detail);
}


static void Check(char const * name, bool condition, char const * detail)
{
	Checks++;
	if (!condition) Report_Failure(name, detail);
}


/* The guard bytes past the declared length catch a decoder that writes more than the
 * count it reports, which is what the caller sizes its buffer from.
 */
static void Check_Decompresses_To(char const * name, std::vector<unsigned char> const & packed,
	std::vector<unsigned char> const & input, unsigned long capacity)
{
	size_t const guard = 16;
	std::vector<unsigned char> unpacked(input.size() + guard, 0xab);
	unsigned long const len = LCW_Uncompress((void *)packed.data(), unpacked.data(), capacity);

	char detail[256];

	Checks++;

	if (len != (unsigned long)input.size()) {
		snprintf(detail, sizeof(detail), "recovered %lu bytes, expected %u", len, (unsigned)input.size());
		Report_Failure(name, detail);
		return;
	}

	if (!input.empty() && memcmp(unpacked.data(), input.data(), input.size()) != 0) {
		size_t at = 0;
		while (at < input.size() && unpacked[at] == input[at]) at++;
		snprintf(detail, sizeof(detail), "first mismatch at offset %u (%02x, expected %02x)",
			(unsigned)at, unpacked[at], input[at]);
		Report_Failure(name, detail);
		return;
	}

	for (size_t i = input.size(); i < unpacked.size(); i++) {
		if (unpacked[i] != 0xab) {
			snprintf(detail, sizeof(detail), "wrote %u bytes past the %u it reported",
				(unsigned)(i + 1 - input.size()), (unsigned)input.size());
			Report_Failure(name, detail);
			return;
		}
	}
}


/* A zero capacity is the argument the entry point was originally documented to ignore,
 * so both spellings of a well formed call have to produce the same block.
 */
static void Check_Round_Trip(char const * name, std::vector<unsigned char> const & input)
{
	std::vector<unsigned char> packed((size_t)LCW_Comp_Worst_Case((int)input.size()) + 256, 0xcd);
	unsigned char const nothing = 0;
	int const len = LCW_Comp(input.empty() ? &nothing : input.data(), packed.data(), (int)input.size());

	if (len < 0 || (size_t)len > packed.size()) {
		printf("FATAL: LCW_Comp returned %d for %u bytes of input\n", len, (unsigned)input.size());
		exit(1);
	}

	packed.resize((size_t)len);

	Check_Decompresses_To(name, packed, input, (unsigned long)input.size());
	Check_Decompresses_To(name, packed, input, 0);
}


static void Test_Well_Formed_Blocks(void)
{
	Check_Round_Trip("empty", std::vector<unsigned char>());
	Check_Round_Trip("single byte", {0x42});
	Check_Round_Trip("four literals", {'A', 'B', 'C', 'D'});

	/* Constant data is what reaches the long run command, and the length it is given
	 * decides where that command's dword sized stores land.
	 */
	for (size_t length = 1; length <= 300; length++) {
		char name[64];
		snprintf(name, sizeof(name), "constant run of %u", (unsigned)length);
		Check_Round_Trip(name, std::vector<unsigned char>(length, 0x7f));
	}

	Check_Round_Trip("constant run of 4096", std::vector<unsigned char>(4096, 0x7f));

	for (size_t period : {size_t(2), size_t(7), size_t(255)}) {
		std::vector<unsigned char> data(4096);
		for (size_t i = 0; i < data.size(); i++) data[i] = (unsigned char)(i % period);

		char name[64];
		snprintf(name, sizeof(name), "period %u", (unsigned)period);
		Check_Round_Trip(name, data);
	}
}


/* The long run command lays its bytes down in dword sized pieces, which once carried it
 * past the count it was handed. The count is the number of bytes it must produce.
 */
static void Test_Long_Runs(void)
{
	for (int count = 1; count <= 16; count++) {
		std::vector<unsigned char> stream = {0x81, 'A', 0xfe, (unsigned char)count, 0x00, 0x5a, 0x80};
		std::vector<unsigned char> expected(1, 'A');
		expected.insert(expected.end(), (size_t)count, 0x5a);

		Check_Decompresses_To("long run", stream, expected, (unsigned long)expected.size());
	}
}


/* Commands that cannot be honoured inside the destination given, and back references to
 * bytes the block has not produced. Each ends the block where it stands, and the short
 * count is what tells the caller the stream is damaged.
 */
static void Test_Malformed_Streams(void)
{
	unsigned char const guard = 0xab;

	struct Case
	{
		char const * Name;
		std::vector<unsigned char> Stream;
		unsigned long Capacity;
	};

	std::vector<unsigned char> literal(1, 0xbf);
	literal.resize(1 + 63, 'z');
	literal.push_back(0x80);

	Case const cases[] = {
		{"oversized run", {0x81, 'A', 0xfe, 0x00, 0xf0, 0x55, 0x80}, 32},
		{"oversized long copy", {0x81, 'A', 0xff, 0x00, 0xf0, 0x00, 0x00, 0x80}, 16},
		{"oversized medium copy", {0x81, 'A', 0xfd, 0x00, 0x00, 0x80}, 16},
		{"oversized short copy", {0x81, 'A', 0x70, 0x01, 0x80}, 4},
		{"oversized literal group", literal, 16},
		{"back reference before the block", {0x81, 'A', 0x01, 0x00, 0x80}, 64},
		{"long copy from beyond the output", {0x81, 'A', 0xff, 0x04, 0x00, 0xff, 0xff, 0x80}, 64},
		{"medium copy from beyond the output", {0x81, 'A', 0xc1, 0xff, 0xff, 0x80}, 64},
		{"copy from the write position", {0x81, 'A', 0x00, 0x00, 0x80}, 64}
	};

	for (Case const & test : cases) {
		std::vector<unsigned char> dest(test.Capacity + 64, guard);
		unsigned long const len = LCW_Uncompress((void *)test.Stream.data(), dest.data(), test.Capacity);

		char detail[128];
		snprintf(detail, sizeof(detail), "the command produced %lu of the %lu bytes available",
			len, test.Capacity);
		Check(test.Name, len < test.Capacity, detail);

		bool clean = true;
		for (size_t i = test.Capacity; i < dest.size(); i++) clean = clean && (dest[i] == guard);
		Check(test.Name, clean, "wrote past the capacity given");
	}
}


int main(void)
{
	Test_Well_Formed_Blocks();
	Test_Long_Runs();
	Test_Malformed_Streams();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
