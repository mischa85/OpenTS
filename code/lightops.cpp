/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "lightops.h"

#include <algorithm>


/*
 * A tint factor is 16.16 fixed point and may exceed one, so a gun it drives past white
 * saturates there. Wrapping instead would turn the brightest pixels of a lit sprite black.
 */
static int Scale_Gun(int gun, int factor)
{
	unsigned int value = ((unsigned int)gun * (unsigned int)factor) >> 16;

	return((int)std::min(value, 255u));
}


/*
 * A spot light adds a fraction of the gun back onto itself, so this too saturates at white.
 */
static int Brighten_Gun(int gun, int multiplier)
{
	return(std::min(gun + ((gun * multiplier) >> 8), 255));
}


/*
 * The low bits a 16 bit pixel cannot carry are lost rather than replicated, so a fully lit
 * gun reads back as 248 or 252 and never quite reaches 255.
 */
static int Expand_Gun(unsigned int pixel, unsigned int bits, unsigned int shift)
{
	return((int)(((pixel >> shift) & ((1u << bits) - 1u)) << (8 - bits)));
}


static unsigned short Build_Pixel(HicolorFormat format, int red, int green, int blue)
{
	return((unsigned short)(((red >> (8 - format.RedBits)) << format.RedShift)
		| ((green >> (8 - format.GreenBits)) << format.GreenShift)
		| ((blue >> (8 - format.BlueBits)) << format.BlueShift)));
}


/// <summary>
/// Builds one intensity level of a color translation table.
/// Every palette color is scaled by the tint the caller asks for and written out as a
/// display pixel, so that art drawn through the table picks the lighting up.
/// </summary>
/// <param name="translator">Receives one display pixel per palette color.</param>
/// <param name="red">Red tint as a 16.16 fixed point factor. Green and blue likewise.</param>
/// <param name="intensity">The factor used in place of the tint on the colors the mask
/// excludes, which are shaded rather than tinted.</param>
/// <param name="tint_mask">One flag per palette color, true where the tint applies.</param>
/// <remarks>Palette color zero is transparent, so it translates to zero rather than being
/// scaled.</remarks>
void Adjust_Color(HicolorFormat format, PaletteClass const & palette, unsigned short * translator, int red, int green, int blue, int intensity, bool const * tint_mask)
{
	translator[0] = 0;

	for (int index = 1; index < PaletteClass::COLOR_COUNT; index++) {
		RGBClass const & color = palette[index];

		int red_factor = intensity;
		int green_factor = intensity;
		int blue_factor = intensity;

		if (tint_mask[index]) {
			red_factor = red;
			green_factor = green;
			blue_factor = blue;
		}

		translator[index] = Build_Pixel(format,
			Scale_Gun(color.Get_Red(), red_factor),
			Scale_Gun(color.Get_Green(), green_factor),
			Scale_Gun(color.Get_Blue(), blue_factor));
	}
}


/// <summary>
/// Brightens a block of display pixels through a ramp.
/// Each ramp byte says what fraction of a pixel's own color to add back onto it, so a
/// spot light reads as a glow over whatever was rendered underneath.
/// </summary>
/// <param name="mul_buffer">The brightness ramp, one byte per pixel. Zero leaves the pixel
/// alone.</param>
/// <param name="color_buffer">The display pixels, brightened in place.</param>
/// <param name="mul_pitch">Bytes from one ramp row to the next.</param>
/// <param name="color_pitch">Bytes from one pixel row to the next.</param>
void Brighten_Color(HicolorFormat format, unsigned char const * mul_buffer, unsigned short * color_buffer, int mul_pitch, int color_pitch, int width, int height)
{
	for (int y = 0; y < height; y++) {
		unsigned char const * mulptr = mul_buffer;
		unsigned short * colorptr = color_buffer;

		for (int x = 0; x < width; x++) {
			int multiplier = *mulptr++;

			if (multiplier != 0) {
				unsigned int pixel = *colorptr;

				*colorptr = Build_Pixel(format,
					Brighten_Gun(Expand_Gun(pixel, format.RedBits, format.RedShift), multiplier),
					Brighten_Gun(Expand_Gun(pixel, format.GreenBits, format.GreenShift), multiplier),
					Brighten_Gun(Expand_Gun(pixel, format.BlueBits, format.BlueShift), multiplier));
			}

			colorptr++;
		}

		mul_buffer += mul_pitch;
		color_buffer = (unsigned short *)((unsigned char *)color_buffer + color_pitch);
	}
}
