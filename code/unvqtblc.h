/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

bool Hicolor_Init_Table(int cmode);
void Hicolor_Clear_Table(void);
void Hicolor_Translate(void *buffer, int size);

void __cdecl UnVQ2_4x4_Table(unsigned char *codebook, unsigned char *pointers, unsigned char *buffer, unsigned int blocksperrow, unsigned int numrows, unsigned int bufwidth);
void __cdecl UnVQ2_4x2_Table(unsigned char *codebook, unsigned char *pointers, unsigned char *buffer, unsigned int blocksperrow, unsigned int numrows, unsigned int bufwidth);

void __cdecl UnVQ1_4x4_Table(unsigned char *codebook, unsigned char *pointers, unsigned char *buffer, unsigned int blocksperrow, unsigned int numrows, unsigned int bufwidth);
void __cdecl UnVQ1_4x2_Table(unsigned char *codebook, unsigned char *pointers, unsigned char *buffer, unsigned int blocksperrow, unsigned int numrows, unsigned int bufwidth);
