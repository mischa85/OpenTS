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

/* $Header: /CounterStrike/LCWUNCMP.CPP 1     3/03/97 10:25a Joe_bostic $ */
/***************************************************************************
 **    C O N F I D E N T I A L --- W E S T W O O D   S T U D I O S        **
 ***************************************************************************
 *                                                                         *
 *               Project Name : WESTWOOD LIBRARY (PSX)                     *
 *                                                                         *
 *                 File Name : LCWUNCMP.CPP                                *
 *                                                                         *
 *                Programmer : Ian M. Leslie                               *
 *                                                                         *
 *                Start Date : May 17, 1995                                *
 *                                                                         *
 *               Last Update : May 17, 1995    [IML]                       *
 *                                                                         *
 *-------------------------------------------------------------------------*
 * Functions:                                                              *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include	"lcw.h"

/* LCW_Uncompress is the name iff.h exports for the decoder. The format is the one
 * LCW_Uncomp reads, so the two names reach one implementation rather than two copies
 * of it.
 */

extern "C" {

/***************************************************************************
 * LCW_UNCOMPRESS -- Decompress an LCW encoded data block.                 *
 *                                                                         *
 * Uncompress data to the following codes in the format b = byte, w = word *
 * n = byte code pulled from compressed data.                              *
 *                                                                         *
 * Command code, n        |Description                                     *
 * ------------------------------------------------------------------------*
 * n=0xxxyyyy,yyyyyyyy    |short copy back y bytes and run x+3   from dest *
 * n=10xxxxxx,n1,n2,...,nx+1|med length copy the next x+1 bytes from source*
 * n=11xxxxxx,w1          |med copy from dest x+3 bytes from offset w1     *
 * n=11111111,w1,w2       |long copy from dest w1 bytes from offset w2     *
 * n=11111110,w1,b1       |long run of byte b1 for w1 bytes                *
 * n=10000000             |end of data reached                             *
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
 *     The 3rd argument is the capacity of the destination buffer, with    *
 *      the meaning LCW_Uncomp gives it.                                   *
 *                                                                         *
 * HISTORY:                                                                *
 *    03/20/1995 IML : Created.                                            *
 *=========================================================================*/
unsigned long __cdecl LCW_Uncompress(void * source, void * dest, unsigned long length)
{
	return((unsigned long)LCW_Uncomp(source, dest, length));
}

}
