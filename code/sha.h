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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Library/SHA.H                                     $*
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 7/22/97 11:37a                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once


#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>


/*
**	This implements the Secure Hash Algorithm. It is a cryptographically
**	secure hash with no known weaknesses. It generates a 160 bit hash
**	result given an arbitrary length data source.
*/
class SHAEngine
{
	public:
		SHAEngine(void) : IsCached(false), Length(0), PartialCount(0) {
			Acc.Long[0] = SA;
			Acc.Long[1] = SB;
			Acc.Long[2] = SC;
			Acc.Long[3] = SD;
			Acc.Long[4] = SE;
		};

		void Init(void) {
			new ((void*)this) SHAEngine;
		};

		// Fetch result as if source data were to stop now.
		size_t Result(void * result) const;

		void Hash(void const * data, size_t length);

		static size_t Digest_Size(void) {return(sizeof(SHADigest));}

	private:

		typedef union {
			uint32_t Long[5];
			unsigned char Char[20];
		} SHADigest;

		/*
		**	This holds the calculated final result. It is cached
		**	here to avoid the overhead of recalculating it over
		**	multiple sequential requests.
		*/
		mutable bool IsCached;
		mutable SHADigest FinalResult;

		// The initial seeds for the block accumulators.
		static constexpr uint32_t SA = 0x67452301;
		static constexpr uint32_t SB = 0xefcdab89;
		static constexpr uint32_t SC = 0x98badcfe;
		static constexpr uint32_t SD = 0x10325476;
		static constexpr uint32_t SE = 0xc3d2e1f0;

		// The round constants the standard fixes, and the widths it fixes them at: a
		// sixty-four byte block is sixteen words in, expanded into an eighty word schedule.
		static constexpr uint32_t K1 = 0x5a827999;	// t=0..19		2^(1/2)/4
		static constexpr uint32_t K2 = 0x6ed9eba1;	// t=20..39		3^(1/2)/4
		static constexpr uint32_t K3 = 0x8f1bbcdc;	// t=40..59		5^(1/2)/4
		static constexpr uint32_t K4 = 0xca62c1d6;	// t=60..79		10^(1/2)/4

		static constexpr size_t SRC_BLOCK_SIZE = 64;
		static constexpr size_t SRC_BLOCK_WORDS = 16;
		static constexpr size_t PROC_BLOCK_WORDS = 80;


		uint32_t Get_Constant(size_t index) const {
			if (index < 20) return(K1);
			if (index < 40) return(K2);
			if (index < 60) return(K3);
			return(K4);
		};

		// Used for 0..19
		uint32_t Function1(uint32_t X, uint32_t Y, uint32_t Z) const {
			return(Z ^ ( X & ( Y ^ Z ) ) );
		};

		// Used for 20..39
		uint32_t Function2(uint32_t X, uint32_t Y, uint32_t Z) const {
			return( X ^ Y ^ Z );
		};

		// Used for 40..59
		uint32_t Function3(uint32_t X, uint32_t Y, uint32_t Z) const {
			return( (X & Y) | (Z & (X | Y) ) );
		};

		// Used for 60..79
		uint32_t Function4(uint32_t X, uint32_t Y, uint32_t Z) const {
			return( X ^ Y ^ Z );
		};

		uint32_t Do_Function(size_t index, uint32_t X, uint32_t Y, uint32_t Z) const {
			if (index < 20) return(Function1(X, Y, Z));
			if (index < 40) return(Function2(X, Y, Z));
			if (index < 60) return(Function3(X, Y, Z));
			return(Function4(X, Y, Z));
		};

		// Process a full source data block.
		void Process_Block(void const * source, SHADigest & acc) const;

		// Processes a partially filled source accumulator buffer.
		void Process_Partial(void const * & data, size_t & length);

		/*
		**	This is the running accumulator values. These values
		**	are updated by a block processing step that occurs
		**	every 512 bits of source data.
		*/
		SHADigest Acc;

		/*
		**	This is the running length of the source data
		**	processed so far. This total is used to modify the
		**	resulting hash value as if it were appended to the end
		**	of the source data.
		*/
		// Bytes hashed so far, and bytes waiting in the staging buffer.
		size_t Length;

		/*
		**	This holds any partial source block. Partial source blocks are
		**	a consequence of submitting less than block sized data chunks
		**	to the SHA Engine.
		*/
		size_t PartialCount;
		char Partial[SRC_BLOCK_SIZE];
};


#define	SHA_SOURCE1		"abc"
#define	SHA_DIGEST1a	"\xA9\x99\x3E\x36\x47\x06\x81\x6A\xBA\x3E\x25\x71\x78\x50\xC2\x6C\x9C\xD0\xD8\x9D"
#define	SHA_DIGEST1b	"\x01\x64\xB8\xA9\x14\xCD\x2A\x5E\x74\xC4\xF7\xFF\x08\x2C\x4D\x97\xF1\xED\xF8\x80"


#define	SHA_SOURCE2		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
#define	SHA_DIGEST2a	"\x84\x98\x3E\x44\x1C\x3B\xD2\x6E\xBA\xAE\x4A\xA1\xF9\x51\x29\xE5\xE5\x46\x70\xF1"
#define	SHA_DIGEST2b	"\xD2\x51\x6E\xE1\xAC\xFA\x5B\xAF\x33\xDF\xC1\xC4\x71\xE4\x38\x44\x9E\xF1\x34\xC8"

#define	SHA_SOURCE3		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define	SHA_DIGEST3a	"\x34\xAA\x97\x3C\xD4\xC4\xDA\xA4\xF6\x1E\xEB\x2B\xDB\xAD\x27\x31\x65\x34\x01\x6F"
#define	SHA_DIGEST3b	"\x32\x32\xAF\xFA\x48\x62\x8A\x26\x65\x3B\x5A\xAA\x44\x54\x1F\xD9\x0D\x69\x06\x03"
