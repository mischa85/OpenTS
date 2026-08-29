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

					copy_ptr = dest_ptr + 4 - ((unsigned) dest_ptr & 0x3);
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
int LCW_Comp(void const * source, void * dest, int datasize)
{
	unsigned char const * const a1stsrc = (unsigned char const *)source;
	unsigned char * const a1stdest = (unsigned char *)dest;
	unsigned char const * const end_of_data = a1stsrc + (datasize > 0 ? datasize : 0);

	unsigned char const * src = a1stsrc;
	unsigned char * out = a1stdest;

	if (src >= end_of_data) {
		*out++ = 0x80;
		return((int)(out - a1stdest));
	}

	/*
	** The output always opens with a length code covering the first byte, because
	** nothing has been emitted yet for a back reference to point at.
	*/
	unsigned char * lenoff = out;
	bool inlen = true;
	*out++ = 0x81;
	*out++ = *src++;

	unsigned char const * matchoff = a1stsrc;

	while (src < end_of_data) {

		/*
		** The search always restarts from the front of the data, so the encoder
		** finds the longest match in the whole block rather than a nearby one.
		*/
		unsigned char const * search = a1stsrc;
		int count = 1;

		for (;;) {
			unsigned char const value = *src;

			/*
			** A byte matching the one 64 positions later is the cheap test for a run
			** long enough to deserve its own code. A run that reaches the end of the
			** data is measured one byte short, leaving that byte to the literal path.
			*/
			if (src + 64 < end_of_data && value == src[64]) {
				int const remaining = (int)(end_of_data - src);
				int runlen = 1;

				while (runlen < remaining && src[runlen] == value) runlen++;
				if (runlen == remaining) runlen--;

				if (runlen >= 65) {
					src += runlen;

					*out++ = 0xfe;
					*out++ = (unsigned char)(runlen & 0xff);
					*out++ = (unsigned char)((runlen >> 8) & 0xff);
					*out++ = value;
					inlen = false;
					continue;
				}
			}

			/*
			** Find the next earlier occurrence of the current byte and, if it could
			** possibly beat the best match so far, measure how far it agrees.
			*/
			unsigned char const * candidate = search;
			while (candidate < src && *candidate != value) candidate++;
			if (candidate >= src) break;
			search = candidate + 1;

			if (src[count - 1] != candidate[count - 1]) continue;

			int const maxlen = (int)(end_of_data - src);
			int matchlen = 0;

			while (matchlen < maxlen && src[matchlen] == candidate[matchlen]) matchlen++;

			if (matchlen >= count) {
				count = matchlen;
				matchoff = candidate;
			}
		}

		if (count > 2) {
			unsigned int const backoff = (unsigned int)(src - matchoff);

			if (count <= 10 && backoff <= 0xfff) {
				*out++ = (unsigned char)(((count - 3) << 4) + (backoff >> 8));
				*out++ = (unsigned char)(backoff & 0xff);

			} else {
				if (count <= 64) {
					*out++ = (unsigned char)(((count - 3) & 0x3f) | 0xc0);
				} else {
					*out++ = 0xff;
					*out++ = (unsigned char)(count & 0xff);
					*out++ = (unsigned char)((count >> 8) & 0xff);
				}

				unsigned int const absoff = (unsigned int)(matchoff - a1stsrc);
				*out++ = (unsigned char)(absoff & 0xff);
				*out++ = (unsigned char)((absoff >> 8) & 0xff);
			}

			src += count;
			inlen = false;

		} else {

			/*
			** A length code counts up from 0x80 and cannot pass 0xbf, so a full one
			** is closed off and a fresh one started.
			*/
			if (!inlen) {
				lenoff = out;
				*out++ = 0x80;
			}
			if (*lenoff == 0xbf) {
				lenoff = out;
				*out++ = 0x80;
			}

			(*lenoff)++;
			*out++ = *src++;
			inlen = true;
		}
	}

	*out++ = 0x80;

	return((int)(out - a1stdest));
}
