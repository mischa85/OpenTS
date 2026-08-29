/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The lighting arithmetic that runs over every lit pixel. It is deliberately free of the
// surface and display classes so that it can be exercised on its own.

#pragma once

#include "palette.h"


/*
 * The bit layout a 16 bit display packs its color guns into. A shift is the position of
 * the field's least significant bit and the bit count is how much of the eight bit gun
 * survives, so the layouts the engine knows differ only in these six numbers.
 */
struct HicolorFormat
{
	unsigned char RedBits;
	unsigned char RedShift;
	unsigned char GreenBits;
	unsigned char GreenShift;
	unsigned char BlueBits;
	unsigned char BlueShift;
};

constexpr HicolorFormat HICOLOR_FORMAT_555 = {5, 10, 5, 5, 5, 0};
constexpr HicolorFormat HICOLOR_FORMAT_556 = {5, 11, 5, 6, 6, 0};
constexpr HicolorFormat HICOLOR_FORMAT_565 = {5, 11, 6, 5, 5, 0};
constexpr HicolorFormat HICOLOR_FORMAT_655 = {6, 10, 5, 5, 5, 0};

void Adjust_Color(HicolorFormat format, PaletteClass const & palette, unsigned short * translator, int red, int green, int blue, int intensity, bool const * tint_mask);
void Brighten_Color(HicolorFormat format, unsigned char const * mul_buffer, unsigned short * color_buffer, int mul_pitch, int color_pitch, int width, int height);
