/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Hashes the vectors from FIPS 180-4 and checks the digest byte for byte. The engine's
// state is five 32-bit words; hold it in anything wider and every digest comes out wrong,
// which the mix file index then fails to match. Needs no game data.

#include "sha.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int Failures = 0;

// The published digest, as the forty hexadecimal characters it is usually written in.
void Check(char const * name, std::string const & message, char const * expected)
{
	SHAEngine engine;
	engine.Hash(message.data(), message.size());

	unsigned char digest[20] = {};
	size_t const size = engine.Result(digest);

	char written[41] = {};
	for (size_t index = 0; index < sizeof(digest); index++) {
		std::snprintf(&written[index * 2], 3, "%02x", digest[index]);
	}

	bool const ok = size == sizeof(digest) && std::strcmp(written, expected) == 0;
	std::printf("%-24s %s\n", name, ok ? "ok" : "FAILED");
	if (!ok) {
		std::printf("    expected %s\n    got      %s (%zu bytes)\n", expected, written, size);
		Failures++;
	}
}

}	// namespace


int main(void)
{
	Check("empty", "", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	Check("abc", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d");

	// Two blocks, so the partial buffer is carried across a block boundary.
	Check("two blocks", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"84983e441c3bd26ebaae4aa1f95129e5e54670f1");

	// A million bytes, which is the vector that exercises the length counter.
	Check("a million a", std::string(1000000, 'a'),
		"34aa973cd4c4daa4f61eeb2bdbad27316534016f");

	if (Failures > 0) {
		std::printf("shadigest: %d failures\n", Failures);
		return(1);
	}

	std::printf("shadigest: every digest matches\n");
	return(0);
}
