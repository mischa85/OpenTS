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
 *                     $Archive:: /Commando/Library/SHAPIPE.CPP                               $*
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 7/22/97 11:37a                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   SHAPipe::Result -- Fetches the current SHA value.                                         *
 *   SHAPipe::Put -- Pass data through the pipe, but use it to build a SHA digest.             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "shapipe.h"


/***********************************************************************************************
 * SHAPipe::Put -- Pass data through the pipe, but use it to build a SHA digest.               *
 *                                                                                             *
 *    This pipe segment will not modify the data, but it will examine the data and use it when *
 *    building a SHA digest.                                                                   *
 *                                                                                             *
 * INPUT:   source   -- Pointer to the data to flow through the pipe.                          *
 *                                                                                             *
 *          length   -- The number of bytes to submit.                                         *
 *                                                                                             *
 * OUTPUT:  Returns with the actual number of bytes output at the distant final end of the     *
 *          pipe chain.                                                                        *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/03/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
int SHAPipe::Put(void const * source, int slen)
{
	// The pipe interface counts bytes in a signed type. Nothing is hashed from a count
	// that is not positive, and the pipe still passes it along.
	if (slen > 0) {
		SHA.Hash(source, (size_t)slen);
	}
	return(BASECLASS::Put(source, slen));
}


/***********************************************************************************************
 * SHAPipe::Result -- Fetches the current SHA value.                                           *
 *                                                                                             *
 *    This routine will return the SHA digest for the data that has passed through this        *
 *    link in the pipe chain. It is a non-destructive read.                                    *
 *                                                                                             *
 * INPUT:   result   -- Pointer to the buffer to hold the SHA digest. This buffer must be      *
 *                      20 bytes long.                                                         *
 *                                                                                             *
 * OUTPUT:  Returns with the number of bytes copied into the buffer. This will be 20.          *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/03/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
size_t SHAPipe::Result(void * result) const
{
	return(SHA.Result(result));
}
