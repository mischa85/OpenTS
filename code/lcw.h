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
 *                     $Archive:: /G/wwlib/lcw.h                                              $*
 *                                                                                             *
 *                      $Author:: Neal_k                                                      $*
 *                                                                                             *
 *                     $Modtime:: 10/04/99 10:25a                                             $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once


int LCW_Uncomp(void const * source, void * dest, unsigned long length=0);
int LCW_Comp(void const * source, void * dest, int length);


/// <summary>
/// Decompresses an LCW encoded block without reading or writing outside the buffers given.
/// </summary>
/// <param name="source">The compressed data.</param>
/// <param name="srclen">The number of compressed bytes available.</param>
/// <param name="dest">The buffer to decompress into.</param>
/// <param name="destlen">The capacity of that buffer.</param>
/// <returns>Returns with the number of bytes written to the destination.</returns>
/// <remarks>Decoding stops at the end of data code, and equally at anything a well formed
/// stream cannot contain: a command running off either buffer, or a back reference to data
/// the block has not produced yet. A caller that knows how large the block should have been
/// recognises a damaged stream by the short count returned.</remarks>
int LCW_Uncomp_Bounded(void const * source, int srclen, void * dest, int destlen);


/// <summary>
/// Reports the largest output LCW_Comp can produce for a block of the size given.
/// </summary>
/// <param name="datasize">The number of source bytes to be compressed.</param>
/// <returns>Returns with the size, in bytes, that the destination buffer must have.</returns>
/// <remarks>A literal group costs one byte over the bytes it carries, and the cheapest
/// command that can close one off consumes three source bytes while emitting three of its
/// own, so the encoder can spend an extra byte for every four bytes of source. The final
/// byte accounts for the end of data marker.</remarks>
constexpr int LCW_Comp_Worst_Case(int datasize)
{
	int const size = datasize > 0 ? datasize : 0;

	return(size + (size + 3) / 4 + 1);
}
