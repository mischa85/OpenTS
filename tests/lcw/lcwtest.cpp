/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the LCW codec without touching any game data. LCW_Comp is checked both
// against LCW_Uncomp, which must recover the input exactly, and against encodings
// derived by hand from the compressor's own command selection rules, so that a change
// to which command a length or offset picks is caught rather than silently accepted.
//
// The container caps a block at 64K: run counts and match offsets are stored in 16-bit
// fields, so nothing here compresses more than 65535 bytes at a time.

#include "lcw.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


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


/*
 * LCW_Comp_Worst_Case is what the pipe and straw classes size their buffers from, so the
 * slack here is deliberately larger than it. An encoder that outgrows the promise overruns
 * the check below rather than the buffer.
 */
static std::vector<unsigned char> Compress(std::vector<unsigned char> const & input)
{
	std::vector<unsigned char> packed((size_t)LCW_Comp_Worst_Case((int)input.size()) + 256, 0xcd);
	unsigned char const nothing = 0;
	int const len = LCW_Comp(input.empty() ? &nothing : input.data(), packed.data(), (int)input.size());

	if (len < 0 || (size_t)len > packed.size()) {
		printf("FATAL: LCW_Comp returned %d for %u bytes of input\n", len, (unsigned)input.size());
		exit(1);
	}

	packed.resize((size_t)len);
	return(packed);
}


/*
** The decompressor must recover the input exactly and must leave the guard bytes past it
** alone, whether or not it was told how large the destination is. A decoder that writes
** more than the length it reports has no way to promise its caller a buffer size.
*/
static void Check_Decompresses_To(char const * name, std::vector<unsigned char> const & packed,
	std::vector<unsigned char> const & input, unsigned long capacity)
{
	size_t const guard = 16;
	std::vector<unsigned char> unpacked(input.size() + guard, 0xab);
	int const len = LCW_Uncomp(packed.data(), unpacked.data(), capacity);

	char detail[256];

	if (len != (int)input.size()) {
		snprintf(detail, sizeof(detail), "recovered %d bytes, expected %u", len, (unsigned)input.size());
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


static void Check_Round_Trip(char const * name, std::vector<unsigned char> const & input)
{
	Checks++;

	std::vector<unsigned char> packed = Compress(input);

	Check_Decompresses_To(name, packed, input, (unsigned long)input.size());
	Check_Decompresses_To(name, packed, input, 0);
}


static void Check_Encoding(char const * name, std::vector<unsigned char> const & input, std::vector<unsigned char> const & expected)
{
	Checks++;

	std::vector<unsigned char> packed = Compress(input);

	if (packed != expected) {
		std::string got;
		std::string want;
		char byte[8];

		for (size_t i = 0; i < packed.size(); i++) {
			snprintf(byte, sizeof(byte), "%02x ", packed[i]);
			got += byte;
		}
		for (size_t i = 0; i < expected.size(); i++) {
			snprintf(byte, sizeof(byte), "%02x ", expected[i]);
			want += byte;
		}

		char detail[1024];
		snprintf(detail, sizeof(detail), "got [%s], expected [%s]", got.c_str(), want.c_str());
		Report_Failure(name, detail);
	}
}


/*
** A small xorshift generator keeps the corpora identical on every platform, which a
** library generator would not.
*/
static unsigned int Random_State = 0;


static unsigned char Next_Random(void)
{
	Random_State ^= Random_State << 13;
	Random_State ^= Random_State >> 17;
	Random_State ^= Random_State << 5;
	return((unsigned char)(Random_State >> 16));
}


static std::vector<unsigned char> Random_Data(unsigned int seed, size_t length)
{
	Random_State = seed;

	std::vector<unsigned char> data(length);
	for (size_t i = 0; i < length; i++) data[i] = Next_Random();
	return(data);
}


static std::vector<unsigned char> Repeated(unsigned char value, size_t length)
{
	return(std::vector<unsigned char>(length, value));
}


static void Test_Degenerate_Inputs(void)
{
	Check_Round_Trip("empty", std::vector<unsigned char>());
	Check_Encoding("empty encoding", std::vector<unsigned char>(), {0x80});

	Check_Round_Trip("single byte", {0x42});
	Check_Encoding("single byte encoding", {0x42}, {0x81, 0x42, 0x80});

	Check_Round_Trip("two equal bytes", {0x42, 0x42});
	Check_Round_Trip("two different bytes", {0x42, 0x43});
	Check_Encoding("four literals", {'A', 'B', 'C', 'D'}, {0x84, 'A', 'B', 'C', 'D', 0x80});
}


/*
** The command the encoder picks depends on the run length and on how far back the
** match is, so each boundary named in the block header gets its own vector.
*/
static void Test_Command_Boundaries(void)
{
	/*
	** Five equal bytes become the opening literal plus a short run of four at an
	** offset of one.
	*/
	Check_Encoding("short run at offset one", Repeated('A', 5), {0x81, 'A', 0x10, 0x01, 0x80});

	/*
	** A run of eleven is one longer than a short run can carry, so it moves to the
	** medium command and its absolute offset.
	*/
	Check_Encoding("medium run boundary", Repeated('A', 12), {0x81, 'A', 0xc8, 0x00, 0x00, 0x80});

	/*
	** Sixty-eight equal bytes fill the medium command exactly; sixty-nine spill into
	** the long command.
	*/
	Check_Encoding("medium run maximum", Repeated('A', 65), {0x81, 'A', 0xfd, 0x00, 0x00, 0x80});
	Check_Encoding("long run boundary", Repeated('A', 66), {0x81, 'A', 0xff, 0x41, 0x00, 0x00, 0x00, 0x80});

	/*
	** Past the sixty-four byte lookahead the encoder takes the dedicated run command,
	** which measures the run one byte short when it reaches the end of the data.
	*/
	Check_Encoding("dedicated run command", Repeated(0x55, 70), {0x81, 0x55, 0xfe, 0x44, 0x00, 0x55, 0x81, 0x55, 0x80});

	for (size_t length = 1; length <= 300; length++) {
		char name[64];
		snprintf(name, sizeof(name), "constant run of %u", (unsigned)length);
		Check_Round_Trip(name, Repeated(0x7f, length));
	}

	for (size_t length : {size_t(3), size_t(10), size_t(11), size_t(64), size_t(65), size_t(66),
			size_t(255), size_t(256), size_t(257), size_t(4095), size_t(4096), size_t(4097)}) {
		char name[64];
		snprintf(name, sizeof(name), "run boundary %u", (unsigned)length);
		Check_Round_Trip(name, Repeated(0xa5, length));
	}
}


/*
** A back reference at offset one is the run case; the far end of the short command is
** an offset of 4095, past which the encoder must fall back to the medium command.
*/
static void Test_Offset_Boundaries(void)
{
	for (size_t distance : {size_t(1), size_t(2), size_t(3), size_t(4094), size_t(4095), size_t(4096), size_t(4097)}) {
		std::vector<unsigned char> data = Random_Data(0x1234u + (unsigned int)distance, distance);

		/*
		** Repeating the leading bytes at exactly the chosen distance forces a match
		** whose offset sits on the boundary under test.
		*/
		data.push_back(0xff);
		for (int i = 0; i < 8; i++) data.push_back(data[(size_t)i]);

		char name[64];
		snprintf(name, sizeof(name), "back reference at distance %u", (unsigned)distance);
		Check_Round_Trip(name, data);
	}
}


static void Test_Compressible_Data(void)
{
	for (size_t period : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(64), size_t(255)}) {
		std::vector<unsigned char> data(4096);
		for (size_t i = 0; i < data.size(); i++) data[i] = (unsigned char)(i % period);

		char name[64];
		snprintf(name, sizeof(name), "period %u", (unsigned)period);
		Check_Round_Trip(name, data);
	}

	/*
	** Runs longer than the length code can hold force the encoder to close one length
	** code and open the next.
	*/
	std::vector<unsigned char> literals(2000);
	for (size_t i = 0; i < literals.size(); i++) literals[i] = (unsigned char)(i * 7 + (i / 251));
	Check_Round_Trip("long literal chain", literals);

	std::vector<unsigned char> mixed;
	Random_State = 0xfeedbeefu;
	while (mixed.size() < 6000) {
		unsigned char control = Next_Random();
		if ((control & 3) == 0) {
			size_t run = (size_t)Next_Random() + 1;
			unsigned char value = Next_Random();
			for (size_t i = 0; i < run; i++) mixed.push_back(value);
		} else {
			size_t chunk = (size_t)(Next_Random() & 31) + 1;
			for (size_t i = 0; i < chunk; i++) mixed.push_back(Next_Random());
		}
	}
	Check_Round_Trip("runs mixed with noise", mixed);

	Check_Round_Trip("65535 equal bytes", Repeated(0x11, 65535));
	Check_Round_Trip("65535 equal bytes with a tail", [] {
		std::vector<unsigned char> data = Repeated(0x22, 65530);
		for (int i = 0; i < 5; i++) data.push_back((unsigned char)(0x30 + i));
		return(data);
	}());
}


static void Test_Incompressible_Data(void)
{
	for (size_t length : {size_t(3), size_t(17), size_t(64), size_t(65), size_t(256), size_t(1000), size_t(4096)}) {
		char name[64];
		snprintf(name, sizeof(name), "random %u", (unsigned)length);
		Check_Round_Trip(name, Random_Data(0xa5a5a5a5u ^ (unsigned int)length, length));
	}
}


/*
 * Builds the corpus that costs the encoder the most per byte. A three byte match whose only
 * earlier occurrence is more than 4095 bytes back takes the command that spends three output
 * bytes on three source bytes, and it closes off the literal group before it, so the byte
 * that follows has to open a fresh one. Alternating the two spends five output bytes for
 * every four of source.
 *
 * The leading region places each three byte sequence once, far enough ahead of every later
 * use of it that no nearer match exists. The byte following each sequence differs there from
 * the byte following it in every body copy, so no match ever runs past three bytes, and the
 * bytes between the matches never repeat a three byte sequence of their own.
 */
static std::vector<unsigned char> Worst_Case_Data(int copies)
{
	int const cells = 1030;

	Random_State = 0x0badc0deu;

	std::vector<std::vector<unsigned char>> triple((size_t)cells);
	std::vector<unsigned char> separator((size_t)cells);
	std::vector<std::vector<unsigned char>> between((size_t)copies, std::vector<unsigned char>((size_t)cells));

	for (int i = 0; i < cells; i++) {
		triple[(size_t)i] = {Next_Random(), Next_Random(), Next_Random()};
		separator[(size_t)i] = Next_Random();

		for (int c = 0; c < copies; c++) {
			unsigned char value;
			bool distinct;

			do {
				value = Next_Random();
				distinct = value != separator[(size_t)i];
				for (int p = 0; p < c; p++) {
					if (between[(size_t)p][(size_t)i] == value) distinct = false;
				}
			} while (!distinct);

			between[(size_t)c][(size_t)i] = value;
		}
	}

	std::vector<unsigned char> data;

	for (int i = 0; i < cells; i++) {
		data.insert(data.end(), triple[(size_t)i].begin(), triple[(size_t)i].end());
		data.push_back(separator[(size_t)i]);
	}

	for (int c = 0; c < copies; c++) {
		for (int i = 0; i < cells; i++) {
			data.insert(data.end(), triple[(size_t)i].begin(), triple[(size_t)i].end());
			data.push_back(between[(size_t)c][(size_t)i]);
		}
	}

	return(data);
}


static void Check_Within_Bound(char const * name, std::vector<unsigned char> const & data)
{
	Checks++;

	size_t const packed = Compress(data).size();
	size_t const bound = (size_t)LCW_Comp_Worst_Case((int)data.size());

	if (packed > bound) {
		char detail[160];
		snprintf(detail, sizeof(detail), "%u bytes of input packed to %u, over the promised %u",
			(unsigned)data.size(), (unsigned)packed, (unsigned)bound);
		Report_Failure(name, detail);
	}
}


/*
 * LCW_Comp_Worst_Case is the promise the pipe and straw classes size their buffers from, so
 * nothing the encoder can be handed may pack larger than it reports. The corpus that
 * approaches the promise is checked as well, because a margin derived from literal groups
 * alone looks generous against ordinary data and is not enough for that corpus.
 */
static void Test_Expansion_Bound(void)
{
	for (size_t length = 1; length <= 2000; length += 37) {
		char name[64];
		snprintf(name, sizeof(name), "expansion bound at %u", (unsigned)length);
		Check_Within_Bound(name, Random_Data(0x31337u + (unsigned int)length, length));
	}

	Check_Within_Bound("expansion bound for a full block", Random_Data(0xc0ffeeu, 8192));
	Check_Within_Bound("expansion bound for constant data", Repeated(0x5a, 8192));

	std::vector<unsigned char> const worst = Worst_Case_Data(2);
	Check_Within_Bound("expansion bound for the worst case corpus", worst);
	Check_Round_Trip("worst case corpus", worst);

	Checks++;

	size_t const packed = Compress(worst).size();
	size_t const over = packed - worst.size();

	printf("note: worst case corpus expands %u bytes of input by %u, against a promise of %u\n",
		(unsigned)worst.size(), (unsigned)over,
		(unsigned)((size_t)LCW_Comp_Worst_Case((int)worst.size()) - worst.size()));

	/*
	 * One byte per 63 literals is what an all literal encoding costs, and it was once taken
	 * for the whole story. The corpus above must stay well clear of it, or this test no
	 * longer covers the case the margin exists for.
	 */
	if (over <= worst.size() / 63 + 1) {
		char detail[160];
		snprintf(detail, sizeof(detail), "expansion of %u bytes no longer exceeds one length code per 63 literals",
			(unsigned)over);
		Report_Failure("worst case corpus", detail);
	}
}


/*
 * Compressed blocks reach the engine from downloaded maps and saved games, so every count
 * in them is hostile until proven otherwise. A command that would run off either buffer,
 * or that reaches back for data the block has not produced, ends the block where it stands.
 */
static void Test_Malformed_Streams(void)
{
	unsigned char const guard = 0xab;

	/*
	** A run whose length exceeds the destination. Nothing may be written past the
	** capacity given, and the short count is what tells the caller the block is bad.
	*/
	{
		unsigned char const stream[] = {0x81, 'A', 0xfe, 0x00, 0xf0, 0x55, 0x80};
		std::vector<unsigned char> dest(64, guard);
		int const len = LCW_Uncomp_Bounded(stream, (int)sizeof(stream), dest.data(), 32);

		Check("oversized run stops short", len < 32, "the run was allowed to fill the destination");

		bool clean = true;
		for (size_t i = 32; i < dest.size(); i++) clean = clean && (dest[i] == guard);
		Check("oversized run stays inside the destination", clean, "wrote past the capacity given");
	}

	/*
	** The same for the two copy commands and for a literal group.
	*/
	{
		unsigned char const longcopy[] = {0x81, 'A', 0xff, 0x00, 0xf0, 0x00, 0x00, 0x80};
		unsigned char const medium[] = {0x81, 'A', 0xfd, 0x00, 0x00, 0x80};

		std::vector<unsigned char> literal(1, 0xbf);
		literal.resize(1 + 63, 'z');
		literal.push_back(0x80);

		std::vector<unsigned char> dest(64, guard);

		Check("oversized long copy stops short", LCW_Uncomp_Bounded(longcopy, (int)sizeof(longcopy), dest.data(), 16) < 16,
			"a copy larger than the destination was allowed");
		Check("oversized medium copy stops short", LCW_Uncomp_Bounded(medium, (int)sizeof(medium), dest.data(), 16) < 16,
			"a copy larger than the destination was allowed");
		Check("oversized literal group stops short", LCW_Uncomp_Bounded(literal.data(), (int)literal.size(), dest.data(), 16) < 16,
			"a literal group larger than the destination was allowed");

		bool clean = true;
		for (size_t i = 16; i < dest.size(); i++) clean = clean && (dest[i] == guard);
		Check("oversized copies stay inside the destination", clean, "wrote past the capacity given");
	}

	/*
	** A block that never reaches its end of data code. The decoder must stop at the last
	** byte it was given rather than reading whatever follows the block in memory.
	*/
	{
		std::vector<unsigned char> stream;
		for (int i = 0; i < 32; i++) {
			stream.push_back(0x81);
			stream.push_back('A');
		}

		std::vector<unsigned char> dest(256, guard);
		int const len = LCW_Uncomp_Bounded(stream.data(), (int)stream.size(), dest.data(), (int)dest.size());

		Check("truncated block stops at the last byte given", len == 32, "the decoder read past the block");
	}

	/*
	** A command cut in half by the end of the block.
	*/
	{
		unsigned char const shortcopy[] = {0x81, 'A', 0x00};
		unsigned char const run[] = {0x81, 'A', 0xfe, 0x04};
		unsigned char const longcopy[] = {0x81, 'A', 0xff, 0x04, 0x00};
		unsigned char const medium[] = {0x81, 'A', 0xc0};
		std::vector<unsigned char> dest(64, guard);

		Check("truncated short copy stops", LCW_Uncomp_Bounded(shortcopy, (int)sizeof(shortcopy), dest.data(), (int)dest.size()) == 1, "");
		Check("truncated run stops", LCW_Uncomp_Bounded(run, (int)sizeof(run), dest.data(), (int)dest.size()) == 1, "");
		Check("truncated long copy stops", LCW_Uncomp_Bounded(longcopy, (int)sizeof(longcopy), dest.data(), (int)dest.size()) == 1, "");
		Check("truncated medium copy stops", LCW_Uncomp_Bounded(medium, (int)sizeof(medium), dest.data(), (int)dest.size()) == 1, "");
	}

	/*
	** Back references that point outside what the block has produced so far.
	*/
	{
		unsigned char const behind[] = {0x81, 'A', 0x01, 0x00, 0x80};
		unsigned char const ahead[] = {0x81, 'A', 0xff, 0x04, 0x00, 0xff, 0xff, 0x80};
		unsigned char const medium[] = {0x81, 'A', 0xc1, 0xff, 0xff, 0x80};
		unsigned char const itself[] = {0x81, 'A', 0x00, 0x00, 0x80};
		std::vector<unsigned char> dest(64, guard);

		Check("back reference before the block stops", LCW_Uncomp_Bounded(behind, (int)sizeof(behind), dest.data(), (int)dest.size()) == 1, "");
		Check("long copy from beyond the output stops", LCW_Uncomp_Bounded(ahead, (int)sizeof(ahead), dest.data(), (int)dest.size()) == 1, "");
		Check("medium copy from beyond the output stops", LCW_Uncomp_Bounded(medium, (int)sizeof(medium), dest.data(), (int)dest.size()) == 1, "");
		Check("copy from the write position stops", LCW_Uncomp_Bounded(itself, (int)sizeof(itself), dest.data(), (int)dest.size()) == 1, "");
	}

	/*
	** Runs shorter than the four byte alignment pad the long run code used to lay down
	** first. The count it was handed is the number of bytes it must produce.
	*/
	for (int count = 1; count <= 16; count++) {
		std::vector<unsigned char> stream = {0x81, 'A', 0xfe, (unsigned char)count, 0x00, 0x5a, 0x80};
		std::vector<unsigned char> expected(1, 'A');
		expected.insert(expected.end(), (size_t)count, 0x5a);

		std::vector<unsigned char> dest(expected.size() + 16, guard);
		int const len = LCW_Uncomp((void const *)stream.data(), dest.data(), (unsigned long)expected.size());

		char name[64];
		snprintf(name, sizeof(name), "long run of %d", count);

		Checks++;

		if (len != (int)expected.size()) {
			Report_Failure(name, "the run reported the wrong length");
			continue;
		}

		bool clean = true;
		for (size_t i = expected.size(); i < dest.size(); i++) clean = clean && (dest[i] == guard);
		if (!clean) {
			Report_Failure(name, "the run wrote past the bytes it declared");
			continue;
		}

		dest.resize(expected.size());
		if (dest != expected) Report_Failure(name, "the run did not produce the bytes it declared");
	}
}


int main(void)
{
	Test_Degenerate_Inputs();
	Test_Command_Boundaries();
	Test_Offset_Boundaries();
	Test_Compressible_Data();
	Test_Incompressible_Data();
	Test_Expansion_Bound();
	Test_Malformed_Streams();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
