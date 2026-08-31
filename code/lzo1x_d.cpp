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
 *                     $Archive:: /Commando/Library/LZO1X_D.CPP                               $*
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

/* $Header: /Commando/Library/LZO1X_D.CPP 1     7/22/97 12:00p Greg_h $ */
/* lzo1x_d.c -- standalone LZO1X decompressor

   This file is part of the LZO real-time data compression library.

   Copyright (C) 1996 Markus Franz Xaver Johannes Oberhumer

   The LZO library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   The LZO library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the LZO library; see the file COPYING.LIB.
   If not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA.

   Markus F.X.J. Oberhumer
   markus.oberhumer@jk.uni-linz.ac.at
 */


#include "always.h"

#include "lzo1x.h"

#if !defined(LZO1X) && !defined(LZO1Y)
#  define LZO1X
#endif

#if 1
#  define TEST_IP				1
#else
#  define TEST_IP				(ip < ip_end)
#endif


/***********************************************************************
// decompress a block of data.
************************************************************************/

/// <summary>
/// Expands a block of LZO1X compressed data.
/// This is the low level decompression routine that the LZO straw, the LZO pipe, and the
/// compressed stream reader all funnel through. The compressed block carries its own end
/// marker, so the length of the expanded data is discovered as the block is unpacked rather
/// than being told to this routine.
/// </summary>
/// <param name="in">Pointer to the compressed data to expand.</param>
/// <param name="in_len">Length of the compressed data, in bytes.</param>
/// <param name="out">Buffer that the expanded data is written into. On entry, out_len
/// carries its capacity in bytes.</param>
/// <param name="out_len">Set to the number of bytes that were expanded into the buffer.</param>
/// <returns>Returns with LZO_E_OK if the block expanded cleanly, otherwise one of the
/// LZO_E_ error codes.</returns>
/// <remarks>A malformed block comes back as an error rather than undefined behavior, so a
/// corrupt save file can be refused instead of crashing the game.</remarks>
int lzo1x_decompress     ( const lzo_byte * in, lzo_uint  in_len,
                                 lzo_byte * out, lzo_uint * out_len,
                                 lzo_voidp )
{
	lzo_byte *op;
	const lzo_byte *ip;
	lzo_uint t;
	const lzo_byte *m_pos;
	const lzo_byte * const ip_end = in + in_len;
	lzo_byte * const op_end = out + *out_len;

#define NEED_IP(x)	if ((lzo_uint)(ip_end - ip) < (lzo_uint)(x)) goto input_overrun
#define NEED_OP(x)	if ((lzo_uint)(op_end - op) < (lzo_uint)(x)) goto output_overrun
#define TEST_LB()	if (m_pos < out || m_pos >= op) goto lookbehind_overrun

	*out_len = 0;

	op = out;
	ip = in;

	NEED_IP(1);
	if (*ip > 17) {
		t = *ip++ - 17;
		NEED_OP(t); NEED_IP(t + 1);
		goto first_literal_run;
	}

	for (;;) {
//	while (TEST_IP) {
		NEED_IP(1);
		t = *ip++;
		if (t >= 16)
			goto match;
		/* a literal run */
		if (t == 0) {
			t = 15;
			NEED_IP(1);
			while (*ip == 0) {
				t += 255, ip++;
				NEED_IP(1);
			}
			t += *ip++;
		}
		/* copy literals */
		NEED_OP(t + 3); NEED_IP(t + 4);
		*op++ = *ip++; *op++ = *ip++; *op++ = *ip++;
first_literal_run:
		do *op++ = *ip++; while (--t > 0);


		t = *ip++;

		if (t >= 16) {
			goto match;
		}
#if defined(LZO1X)
		m_pos = op - 1 - 0x800;
#elif defined(LZO1Y)
		m_pos = op - 1 - 0x400;
#endif
		m_pos -= t >> 2;
		NEED_IP(1);
		m_pos -= *ip++ << 2;
		TEST_LB(); NEED_OP(3);
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		*op++ = *m_pos;
//		*op++ = *m_pos++;
		goto match_done;


		/* handle matches */
		for (;;) {
//		while (TEST_IP) {
			if (t < 16) {						/* a M1 match */
				m_pos = op - 1;
				m_pos -= t >> 2;
				NEED_IP(1);
				m_pos -= *ip++ << 2;
				TEST_LB(); NEED_OP(2);
				*op++ = *m_pos++;
				*op++ = *m_pos;
//				*op++ = *m_pos++;
			} else {
match:
				if (t >= 64) {				/* a M2 match */
					m_pos = op - 1;
#if defined(LZO1X)
					m_pos -= (t >> 2) & 7;
					NEED_IP(1);
					m_pos -= *ip++ << 3;
					t = (t >> 5) - 1;
#elif defined(LZO1Y)
					m_pos -= (t >> 2) & 3;
					NEED_IP(1);
					m_pos -= *ip++ << 2;
					t = (t >> 4) - 3;
#endif
					TEST_LB();
				} else {
					if (t >= 32) {			/* a M3 match */
						t &= 31;
						if (t == 0) {
							t = 31;
							NEED_IP(1);
							while (*ip == 0) {
								t += 255, ip++;
								NEED_IP(1);
							}
							t += *ip++;
						}
						m_pos = op - 1;
						NEED_IP(2);
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
						TEST_LB();
					} else {						/* a M4 match */
						m_pos = op;
						m_pos -= (t & 8) << 11;
						t &= 7;
						if (t == 0) {
							t = 7;
							NEED_IP(1);
							while (*ip == 0) {
								t += 255, ip++;
								NEED_IP(1);
							}
							t += *ip++;
						}
						NEED_IP(2);
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
						if (m_pos == op) {
							goto eof_found;
						}
						m_pos -= 0x4000;
						TEST_LB();
					}
				}
				NEED_OP(t + 2);
				*op++ = *m_pos++; *op++ = *m_pos++;
				do *op++ = *m_pos++; while (--t > 0);
			}

match_done:
			t = ip[-2] & 3;
			if (t == 0)
				break;
			/* copy literals */
			NEED_OP(t); NEED_IP(t + 1);
			do *op++ = *ip++; while (--t > 0);
			t = *ip++;
		}
	}

	/* ip == ip_end and no EOF code was found */

	//Unreachable - ST 9/5/96 5:07PM
	//*out_len = op - out;
	//return (ip == ip_end ? LZO_E_EOF_NOT_FOUND : LZO_E_ERROR);

eof_found:
	*out_len = op - out;
	if (t != 1) {
		return(LZO_E_ERROR);
	}
	return (ip == ip_end ? LZO_E_OK : LZO_E_ERROR);

input_overrun:
	*out_len = op - out;
	return(LZO_E_INPUT_OVERRUN);

output_overrun:
	*out_len = op - out;
	return(LZO_E_OUTPUT_OVERRUN);

lookbehind_overrun:
	*out_len = op - out;
	return(LZO_E_LOOKBEHIND_OVERRUN);

#undef NEED_IP
#undef NEED_OP
#undef TEST_LB
}


/*
vi:ts=4
*/
