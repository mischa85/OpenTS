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


/*
** The maximum output the encoder can produce is one length code per 63 literal bytes,
** plus the opening code and the end marker. The margin here is deliberately larger so
** that an encoder fault overruns the check below rather than the buffer.
*/
static std::vector<unsigned char> Compress(std::vector<unsigned char> const & input)
{
	std::vector<unsigned char> packed(input.size() + input.size() / 8 + 256, 0xcd);
	unsigned char const nothing = 0;
	int const len = LCW_Comp(input.empty() ? &nothing : input.data(), packed.data(), (int)input.size());

	if (len < 0 || (size_t)len > packed.size()) {
		printf("FATAL: LCW_Comp returned %d for %u bytes of input\n", len, (unsigned)input.size());
		exit(1);
	}

	packed.resize((size_t)len);
	return(packed);
}


static void Check_Round_Trip(char const * name, std::vector<unsigned char> const & input)
{
	Checks++;

	std::vector<unsigned char> packed = Compress(input);

	/*
	** The slack at the end absorbs LCW_Uncomp's long-run code, which fills whole
	** aligned words and can reach a few bytes past the last byte it accounts for.
	*/
	std::vector<unsigned char> unpacked(input.size() + 64, 0xab);
	int const len = LCW_Uncomp(packed.data(), unpacked.data(), (unsigned long)input.size());

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
** The routine's own documentation promises the output fits in datasize + datasize/128,
** and the pipe and straw classes size their buffers from that promise. The encoder
** spends one extra byte per 63 literals, so this check records the real worst case
** rather than the documented one.
*/
static void Test_Expansion_Bound(void)
{
	Checks++;

	size_t worst_over = 0;
	size_t worst_length = 0;

	for (size_t length = 1; length <= 2000; length += 37) {
		std::vector<unsigned char> data = Random_Data(0x31337u + (unsigned int)length, length);
		size_t const packed = Compress(data).size();

		if (packed > length && packed - length > worst_over) {
			worst_over = packed - length;
			worst_length = length;
		}
	}

	printf("note: worst measured expansion is %u bytes over %u bytes of input\n",
		(unsigned)worst_over, (unsigned)worst_length);

	if (worst_length != 0 && worst_over > worst_length / 63 + 3) {
		char detail[128];
		snprintf(detail, sizeof(detail), "expansion of %u bytes exceeds one length code per 63 literals",
			(unsigned)worst_over);
		Report_Failure("expansion bound", detail);
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

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
