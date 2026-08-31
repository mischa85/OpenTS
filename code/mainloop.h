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
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Commando/mainloop.h                          $*
 *                                                                                             *
 *                      $Author:: Denzil_l                                                    $*
 *                                                                                             *
 *                     $Modtime:: 10/18/01 6:21p                                              $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "keyboard.h"

bool Main_Loop(void);
void Keyboard_Process(KeyNumType & input);
void Multiplayer_Debug_Print(bool noframecheck);

/*
** The game parks while it is not being looked at. The predicate is the state, and the
** routine beside it is the one pass of work a parked game still owes the message queue;
** how long the game stays parked is the caller's to decide.
*/
bool Is_Suspended(void);
void Service_Suspension(void);

/*
** The frame pacer, split into the wait and the work. Frame_Is_Due answers whether the
** frame that has been played is over yet, and Service_Frame is what the engine does with
** the remainder of one: the maintenance callback, and a redraw when there is a tactical
** view to redraw.
*/
bool Frame_Is_Due(void);
void Service_Frame(void);
