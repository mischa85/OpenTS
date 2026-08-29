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

#pragma once

class Surface;
class PaletteClass;

#define SIZE_OF_PALETTE 256
extern	"C" unsigned char *InterpolationPalette;
extern	bool	InterpolationPaletteChanged;
void Interpolate_2X_Scale( Surface * source, Surface * dest , char const * palette_file_name);
void Read_Interpolation_Palette (char const *palette_file_name);
void Write_Interpolation_Palette (char const *palette_file_name);
void Increase_Palette_Luminance (PaletteClass & palette , double percentage);
extern "C"{
	extern unsigned char PaletteInterpolationTable[SIZE_OF_PALETTE][SIZE_OF_PALETTE];
	extern unsigned char *InterpolationPalette;
}
