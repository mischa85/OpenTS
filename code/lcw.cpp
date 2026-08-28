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
 *                     $Archive:: /G/wwlib/lcw.cpp                                            $*
 *                                                                                             *
 *                      $Author:: Neal_k                                                      $*
 *                                                                                             *
 *                     $Modtime:: 10/04/99 10:25a                                             $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   LCW_Comp -- Performes LCW compression on a block of data.                                 *
 *   LCW_Uncomp -- Decompress an LCW encoded data block.                                       *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include	"always.h"
#include	"lcw.h"

#include <cstdint>

/***************************************************************************
 * LCW_Uncomp -- Decompress an LCW encoded data block.                     *
 *                                                                         *
 * Uncompress data to the following codes in the format b = byte, w = word *
 * n = byte code pulled from compressed data.                              *
 *                                                                         *
 *   Command code, n        |Description                                   *
 * ------------------------------------------------------------------------*
 * n=0xxxyyyy,yyyyyyyy      |short copy back y bytes and run x+3 from dest *
 * n=10xxxxxx,n1,n2,...,nx+1|med length copy the next x+1 bytes from source*
 * n=11xxxxxx,w1            |med copy from dest x+3 bytes from offset w1   *
 * n=11111111,w1,w2         |long copy from dest w1 bytes from offset w2   *
 * n=11111110,w1,b1         |long run of byte b1 for w1 bytes              *
 * n=10000000               |end of data reached                           *
 *                                                                         *
 *                                                                         *
 * INPUT:                                                                  *
 *      void * source ptr                                                  *
 *      void * destination ptr                                             *
 *      unsigned long length of uncompressed data                          *
 *                                                                         *
 *                                                                         *
 * OUTPUT:                                                                 *
 *     unsigned long # of destination bytes written                        *
 *                                                                         *
 * WARNINGS:                                                               *
 *     3rd argument is dummy. It exists to provide cross-platform          *
 *      compatibility. Note therefore that this implementation does not    *
 *      check for corrupt source data by testing the uncompressed length.  *
 *                                                                         *
 * HISTORY:                                                                *
 *    03/20/1995 IML : Created.                                            *
 *=========================================================================*/
int LCW_Uncomp(void const * source, void * dest, unsigned long )
{
	unsigned char * source_ptr, * dest_ptr, * copy_ptr;
	unsigned char op_code, data;
	unsigned count;
	unsigned * word_dest_ptr;
	unsigned word_data;

	/* Copy the source and destination ptrs. */
	source_ptr = (unsigned char*) source;
	dest_ptr   = (unsigned char*) dest;

	for (;;) {

		/* Read in the operation code. */
		op_code = *source_ptr++;

		if (!(op_code & 0x80)) {

			/* Do a short copy from destination. */
			count = (op_code >> 4) + 3;
			copy_ptr = dest_ptr - ((unsigned) *source_ptr++ + (((unsigned) op_code & 0x0f) << 8));

			while (count--) *dest_ptr++ = *copy_ptr++;

		} else {

			if (!(op_code & 0x40)) {

				if (op_code == 0x80) {

					/* Return # of destination bytes written. */
					return((unsigned long) (dest_ptr - (unsigned char*) dest));

				} else {

					/* Do a medium copy from source. */
					count = op_code & 0x3f;

					while (count--) *dest_ptr++ = *source_ptr++;
				}

			} else {

				if (op_code == 0xfe) {

					/* Do a long run. */
					count = *source_ptr + ((unsigned) *(source_ptr + 1) << 8);
					word_data = data = *(source_ptr + 2);
					word_data  = (word_data << 24) + (word_data << 16) + (word_data << 8) + word_data;
					source_ptr += 3;

					copy_ptr = dest_ptr + 4 - ((uintptr_t) dest_ptr & 0x3);
					count -= (copy_ptr - dest_ptr);
					while (dest_ptr < copy_ptr) *dest_ptr++ = data;

					word_dest_ptr = (unsigned*) dest_ptr;

					dest_ptr += (count & 0xfffffffc);

					while (word_dest_ptr < (unsigned*) dest_ptr) {
						*word_dest_ptr		= word_data;
						*(word_dest_ptr + 1) = word_data;
						word_dest_ptr += 2;
					}

					copy_ptr = dest_ptr + (count & 0x3);
					while (dest_ptr < copy_ptr) *dest_ptr++ = data;

				} else {

					if (op_code == 0xff) {

						/* Do a long copy from destination. */
						count = *source_ptr + ((unsigned) *(source_ptr + 1) << 8);
						copy_ptr = (unsigned char*) dest + *(source_ptr + 2) + ((unsigned) *(source_ptr + 3) << 8);
						source_ptr += 4;

						while (count--) *dest_ptr++ = *copy_ptr++;

					} else {

						/* Do a medium copy from destination. */
						count = (op_code & 0x3f) + 3;
						copy_ptr = (unsigned char*) dest + *source_ptr + ((unsigned) *(source_ptr + 1) << 8);
						source_ptr += 2;

						while (count--) *dest_ptr++ = *copy_ptr++;
					}
				}
			}
		}
	}
}


/***********************************************************************************************
 * LCW_Comp -- Performes LCW compression on a block of data.                                   *
 *                                                                                             *
 *    This routine will compress a block of data using the LCW compression method. LCW has     *
 *    the primary characteristic of very fast uncompression at the expense of very slow        *
 *    compression times.                                                                       *
 *                                                                                             *
 * INPUT:   source   -- Pointer to the source data to compress.                                *
 *                                                                                             *
 *          dest     -- Pointer to the destination location to store the compressed data       *
 *                      to.                                                                    *
 *                                                                                             *
 *          datasize -- The size (in bytes) of the source data to compress.                    *
 *                                                                                             *
 * OUTPUT:  Returns with the number of bytes of output data stored into the destination        *
 *          buffer.                                                                            *
 *                                                                                             *
 * WARNINGS:   Be sure that the destination buffer is big enough. The maximum size required    *
 *             for the destination buffer is (datasize + datasize/128).                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/20/1997 JLB : Created.                                                                 *
 *=============================================================================================*/

/*
 * Compressed blocks reach save games, so this emits the same bytes the assembly it replaced
 * emitted, quirks included. Two of those quirks are worth knowing about before changing
 * anything here.
 *
 * A datasize of 1 does not produce a one byte block. The first source byte is written before
 * the loop is entered and the end is only tested after a byte has been consumed, so a second
 * byte is read past the end of the source and encoded alongside it. The block that comes out
 * expands to two bytes rather than one.
 *
 * The search for a run reads sixty four bytes ahead of the current position without checking
 * that they belong to the source at all, so it can read past the end of the buffer. Only the
 * comparison result is used, and the run length that follows is measured properly.
 */
int LCW_Comp(void const * source, void * dest, int datasize)
{
	unsigned char const * const start = (unsigned char const *)source;
	unsigned char * const first = (unsigned char *)dest;
	unsigned char const * const end_of_data = start + datasize;

	unsigned char const * si = start;
	unsigned char * di = first;

	/*
	 * The first command is always a run of literals, opened here and extended in place as
	 * more of them are emitted.
	 */
	bool inlen = true;
	unsigned char * lenoff = di;

	*di++ = 0x81;
	*di++ = *si++;

	while (true) {
		unsigned char * ndest = di;
		unsigned char const * search = start;
		unsigned char const * matchoff = start;
		int count = 1;

		/*
		 * Find the longest run of earlier data that repeats at the current position. A
		 * single byte repeated far enough is worth a command of its own and is emitted
		 * straight away, without disturbing the search.
		 */
		while (true) {
			unsigned char const value = *si;

			if (value == si[64]) {
				long const left = (long)(end_of_data - si);
				long matched = 0;

				while (matched < left && si[matched] == value) {
					matched++;
				}

				/*
				 * A run that reaches the end of the source is counted one short,
				 * because the scan it replaces stepped past the last byte it read.
				 * With nothing left at all that count goes negative, and the test
				 * below is unsigned, so it reads as enormous and a run is emitted
				 * from a position that has already passed the end. That only arises
				 * on the malformed tail described above the function, and it is kept
				 * because the bytes it produces are the bytes callers have.
				 */
				long const runlength = (matched < left) ? matched : (left - 1);

				if ((unsigned long)runlength >= 65) {
					inlen = false;
					si += runlength;
					di = ndest;

					*di++ = 0xFE;
					*di++ = (unsigned char)(runlength & 0xFF);
					*di++ = (unsigned char)((runlength >> 8) & 0xFF);
					*di++ = value;

					ndest = di;
					continue;
				}
			}

			long const window = (long)(si - search);

			if (window <= 0) {
				break;
			}

			/*
			 * Look for somewhere earlier the current byte appears.
			 */
			unsigned char const * found = NULL;

			for (long i = 0; i < window; i++) {
				unsigned char const candidate = *search++;

				if (candidate == value) {
					found = search;
					break;
				}
			}

			if (found == NULL) {
				break;
			}

			/*
			 * Reject the candidate cheaply before measuring it: if the byte that would
			 * end a run at least as long as the best so far does not agree, it cannot
			 * beat it.
			 */
			if (si[count - 1] != search[count - 2]) {
				continue;
			}

			long const room = (long)(end_of_data - si);
			long length = 0;

			while (length < room && si[length] == (search - 1)[length]) {
				length++;
			}

			if (length < count) {
				continue;
			}

			count = (int)length;
			matchoff = search - 1;
		}

		di = ndest;

		if (count > 2) {
			unsigned long const back = (unsigned long)(si - matchoff);

			if (count <= 10 && back <= 0x0FFF) {

				/*
				 * Short run: three bits of length and twelve of distance, packed into
				 * two bytes.
				 */
				*di++ = (unsigned char)((((unsigned long)(count - 3)) << 4) | ((back >> 8) & 0x0F));
				*di++ = (unsigned char)(back & 0xFF);
			} else {
				if (count <= 64) {
					*di++ = (unsigned char)(0xC0 | (count - 3));
				} else {
					*di++ = 0xFF;
					*di++ = (unsigned char)(count & 0xFF);
					*di++ = (unsigned char)((count >> 8) & 0xFF);
				}

				/*
				 * The longer forms carry the match's position from the start of the
				 * data rather than its distance back from here.
				 */
				unsigned long const offset = (unsigned long)(matchoff - start);

				*di++ = (unsigned char)(offset & 0xFF);
				*di++ = (unsigned char)((offset >> 8) & 0xFF);
			}

			si += count;
			inlen = false;
		} else {

			/*
			 * Nothing worth referencing, so the byte goes out as a literal. A length
			 * command counts up to 0x3F bytes before another has to be opened.
			 */
			if (!inlen || *lenoff == 0xBF) {
				lenoff = di;
				*di++ = 0x80;
			}

			(*lenoff)++;
			*di++ = *si++;
			inlen = true;
		}

		if (si >= end_of_data) {
			break;
		}
	}

	*di++ = 0x80;

	return((int)(di - first));
}
