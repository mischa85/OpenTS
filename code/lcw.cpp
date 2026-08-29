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

#include	<climits>
#include	<cstring>

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
 *     The 3rd argument is the capacity of the destination buffer. Zero    *
 *      leaves the decoder unbounded, as it always was, so corrupt source  *
 *      data can then drive it past the end of the buffer.                 *
 *                                                                         *
 * HISTORY:                                                                *
 *    03/20/1995 IML : Created.                                            *
 *=========================================================================*/
int LCW_Uncomp(void const * source, void * dest, unsigned long length)
{
	unsigned long const capacity = (length > 0 && length < (unsigned long)INT_MAX) ? length : (unsigned long)INT_MAX;

	return(LCW_Uncomp_Bounded(source, INT_MAX, dest, (int)capacity));
}


int LCW_Uncomp_Bounded(void const * source, int srclen, void * dest, int destlen)
{
	unsigned char const * const source_start = (unsigned char const *)source;
	unsigned char * const dest_start = (unsigned char *)dest;

	int const in_limit = srclen > 0 ? srclen : 0;
	int const out_limit = destlen > 0 ? destlen : 0;

	int in = 0;
	int out = 0;

	for (;;) {

		/* Read in the operation code. */
		if (in >= in_limit) break;
		unsigned char const op_code = source_start[in++];

		if (!(op_code & 0x80)) {

			/* Do a short copy from destination. */
			if (in >= in_limit) break;
			int const count = (op_code >> 4) + 3;
			int const from = out - ((int)source_start[in++] + (((int)op_code & 0x0f) << 8));

			if (from < 0 || from >= out || count > out_limit - out) break;

			for (int i = 0; i < count; i++) dest_start[out + i] = dest_start[from + i];
			out += count;

		} else {

			if (!(op_code & 0x40)) {

				if (op_code == 0x80) {

					/* Return # of destination bytes written. */
					return(out);

				} else {

					/* Do a medium copy from source. */
					int const count = op_code & 0x3f;

					if (count > in_limit - in || count > out_limit - out) break;

					for (int i = 0; i < count; i++) dest_start[out + i] = source_start[in + i];
					in += count;
					out += count;
				}

			} else {

				if (op_code == 0xfe) {

					/* Do a long run. */
					if (in_limit - in < 3) break;
					int const count = (int)source_start[in] + ((int)source_start[in + 1] << 8);
					unsigned char const data = source_start[in + 2];
					in += 3;

					if (count > out_limit - out) break;

					memset(dest_start + out, data, (size_t)count);
					out += count;

				} else {

					if (op_code == 0xff) {

						/* Do a long copy from destination. */
						if (in_limit - in < 4) break;
						int const count = (int)source_start[in] + ((int)source_start[in + 1] << 8);
						int const from = (int)source_start[in + 2] + ((int)source_start[in + 3] << 8);
						in += 4;

						if (from >= out || count > out_limit - out) break;

						for (int i = 0; i < count; i++) dest_start[out + i] = dest_start[from + i];
						out += count;

					} else {

						/* Do a medium copy from destination. */
						if (in_limit - in < 2) break;
						int const count = (op_code & 0x3f) + 3;
						int const from = (int)source_start[in] + ((int)source_start[in + 1] << 8);
						in += 2;

						if (from >= out || count > out_limit - out) break;

						for (int i = 0; i < count; i++) dest_start[out + i] = dest_start[from + i];
						out += count;
					}
				}
			}
		}
	}

	/*
	** Everything that leaves the loop rather than reading the end of data code is a
	** damaged stream. The count returned falls short of what the container promised,
	** which is how the callers tell the two apart.
	*/
	return(out);
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
 * WARNINGS:   Be sure that the destination buffer is big enough. The maximum size             *
 *             required is what LCW_Comp_Worst_Case reports, which is one byte                 *
 *             over the source for every four bytes of it, plus the end marker.                *
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
