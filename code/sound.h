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

/***************************************************************************
 **   C O N F I D E N T I A L --- W E S T W O O D    S T U D I O S        **
 ***************************************************************************
 *                                                                         *
 *                 Project Name : Westwood Library                         *
 *                                                                         *
 *                    File Name : SOUND.H                                  *
 *                                                                         *
 *                   Programmer : Joe L. Bostic                            *
 *                                                                         *
 *                   Start Date : September 1, 1993                        *
 *                                                                         *
 *                  Last Update : September 1, 1993   [JLB]                *
 *                                                                         *
 *-------------------------------------------------------------------------*
 * Functions:                                                              *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

//#define	HMI_DRIVER	TRUE
#if defined(__EMSCRIPTEN__)
#include "win32compat.h"
#else
#include "dsound.h"
#endif
#include "soscomp.h"

/*
**	Maximum number of sound effects that may run at once.
*/
#define	MAX_SFX		5

/*
**	Size of temp HMI low memory staging buffer.
*/
#define	SECONDARY_BUFFER_SIZE		(1024*32)
