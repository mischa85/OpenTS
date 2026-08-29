/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the hicolor lighting arithmetic without touching any game data. Every check
// compares the routine against a model written from the bit masks and shifts of the pixel
// layouts rather than from the routine, so that a mistake in one is not repeated in the
// other. The model reproduces the saturation as a wrapped byte or'd with a fill of ones,
// which is how a gun driven past white has to behave: without it the brightest pixels of
// a lit sprite wrap to black.
//
// Two of the sweeps cover their whole input domain. The packing sweep pairs each of the
// 16777216 gun triples a palette color can hold with each layout, and the brightening
// sweep pairs each of the 65536 display pixels with each of the 256 ramp values. The
// scaling sweep covers every gun against every tint factor the lighting can produce: a
// tint is capped at twice normal and normal is a 16.16 unit, so the factor reaches four
// units at the brightest intensity level and no further.

#include "lightops.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>


static int Failures = 0;
static int Checks = 0;


/*
 * The masks and shifts each pixel layout packs a translation table entry with, and the
 * thirteen constants it brightens a display pixel with.
 */
struct AdjustModel
{
	unsigned int RedMask;
	unsigned int GreenMask;
	unsigned int BlueMask;
	unsigned int RedShift;
	unsigned int GreenShift;
	unsigned int BlueShift;
};

struct BrightenModel
{
	unsigned int Constant[13];
};

struct FormatCase
{
	char const * Name;
	HicolorFormat Format;
	AdjustModel Adjust;
	BrightenModel Brighten;

	/*
	 * How closely the tint factor sweep steps through its range. The display is always
	 * 565, so that layout takes every factor and the others take a coarser sweep that
	 * still crosses every gun's saturation boundary many times over.
	 */
	int FactorStep;
};

static FormatCase const _formats[] = {
	{"555", HICOLOR_FORMAT_555,
		{0xF8, 0xF8, 0xF8, 7, 2, 3},
		{{7, 2, 0xF8, 0xF8, 8, 8, 3, 3, 10, 5, 3, 8, 3}}, 5},
	{"556", HICOLOR_FORMAT_556,
		{0xF8, 0xF8, 0xFC, 8, 3, 2},
		{{8, 3, 0xF8, 0xF8, 8, 8, 3, 3, 11, 6, 2, 8, 2}}, 5},
	{"565", HICOLOR_FORMAT_565,
		{0xF8, 0xFC, 0xF8, 8, 3, 3},
		{{8, 3, 0xF8, 0xFC, 8, 8, 3, 2, 11, 5, 3, 8, 3}}, 1},
	{"655", HICOLOR_FORMAT_655,
		{0xFC, 0xF8, 0xF8, 8, 2, 3},
		{{8, 2, 0xFC, 0xF8, 8, 8, 2, 3, 10, 5, 3, 8, 3}}, 5},
};

static int const FORMAT_COUNT = (int)(sizeof(_formats) / sizeof(_formats[0]));

static int const TABLE_BYTES = (int)(PaletteClass::COLOR_COUNT * sizeof(unsigned short));


/*
 * A single fault repeats across millions of inputs, so reporting stops well before the
 * log fills up.
 */
static void Report_Failure(char const * name, char const * detail)
{
	Failures++;
	printf("FAIL %s: %s\n", name, detail);

	if (Failures > 20) {
		printf("FATAL: too many failures\n");
		exit(1);
	}
}


/*
 * A tint factor is 16.16 fixed point, so the scaled gun is the top half of the product.
 * Anything that does not fit in a byte saturates at white.
 */
static unsigned int Model_Scale_Gun(unsigned int gun, unsigned int factor)
{
	unsigned int value = (gun * factor) >> 16;
	unsigned int fill = (value <= 255u) ? 0x00u : 0xFFu;

	return((value & 0xFFu) | fill);
}


/*
 * A brightened gun is the gun plus a fraction of itself, and saturates the same way.
 */
static unsigned int Model_Brighten_Gun(unsigned int scaled, unsigned int gun)
{
	unsigned int sum = (scaled & 0xFFu) + (gun & 0xFFu);
	unsigned int fill = (sum > 0xFFu) ? 0xFFu : 0x00u;

	return((sum & 0xFFu) | fill);
}


static unsigned short Model_Adjust_Pixel(AdjustModel const & model, unsigned int red, unsigned int green, unsigned int blue)
{
	return((unsigned short)(((red & model.RedMask) << model.RedShift)
		| ((green & model.GreenMask) << model.GreenShift)
		| ((blue & model.BlueMask) >> model.BlueShift)));
}


static void Model_Adjust_Color(FormatCase const & format, PaletteClass const & palette, unsigned short * translator, unsigned int red, unsigned int green, unsigned int blue, unsigned int intensity, bool const * tint_mask)
{
	translator[0] = 0;

	for (int index = 1; index < PaletteClass::COLOR_COUNT; index++) {
		unsigned int red_factor = tint_mask[index] ? red : intensity;
		unsigned int green_factor = tint_mask[index] ? green : intensity;
		unsigned int blue_factor = tint_mask[index] ? blue : intensity;

		translator[index] = Model_Adjust_Pixel(format.Adjust,
			Model_Scale_Gun((unsigned int)palette[index].Get_Red(), red_factor),
			Model_Scale_Gun((unsigned int)palette[index].Get_Green(), green_factor),
			Model_Scale_Gun((unsigned int)palette[index].Get_Blue(), blue_factor));
	}
}


static unsigned short Model_Brighten_Pixel(BrightenModel const & model, unsigned int color, unsigned int multiplier)
{
	unsigned int const * constant = model.Constant;

	unsigned int red = (color >> constant[0]) & constant[2];
	unsigned int green = (color >> constant[1]) & constant[3];
	unsigned int blue = (color << constant[10]) & 0xFFu;

	red = Model_Brighten_Gun((red * multiplier) >> constant[4], red);
	green = Model_Brighten_Gun((green * multiplier) >> constant[5], green);
	blue = Model_Brighten_Gun((blue * multiplier) >> constant[11], blue);

	return((unsigned short)(((red >> constant[6]) << constant[8])
		| ((green >> constant[7]) << constant[9])
		| (blue >> constant[12])));
}


static void Model_Brighten_Color(FormatCase const & format, unsigned char const * mul_buffer, unsigned short * color_buffer, int mul_pitch, int color_pitch, int width, int height)
{
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			unsigned int multiplier = mul_buffer[y * mul_pitch + x];

			if (multiplier != 0) {
				unsigned short * pixel = (unsigned short *)((unsigned char *)color_buffer + y * color_pitch) + x;
				*pixel = Model_Brighten_Pixel(format.Brighten, *pixel, multiplier);
			}
		}
	}
}


/*
 * The sweeps run to hundreds of millions of entries, so they compare a whole table at a
 * time and only pick the difference apart once one is found.
 */
static int First_Difference(unsigned short const * got, unsigned short const * expected)
{
	if (memcmp(got, expected, TABLE_BYTES) == 0) {
		return(-1);
	}

	for (int index = 0; index < PaletteClass::COLOR_COUNT; index++) {
		if (got[index] != expected[index]) {
			return(index);
		}
	}

	return(-1);
}


/*
 * Palette color zero is transparent, so its translation table entry is zero rather than a
 * scaled color.
 */
static void Test_Transparent_Entry(void)
{
	Checks++;

	PaletteClass palette;
	bool mask[256];

	for (int i = 0; i < 256; i++) {
		palette[i].Set_Red(255);
		palette[i].Set_Green(255);
		palette[i].Set_Blue(255);
		mask[i] = true;
	}

	for (int f = 0; f < FORMAT_COUNT; f++) {
		unsigned short translator[256];

		for (int i = 0; i < 256; i++) {
			translator[i] = 0xDEAD;
		}

		Adjust_Color(_formats[f].Format, palette, translator, 0x10000, 0x10000, 0x10000, 0x10000, mask);

		if (translator[0] != 0) {
			char detail[128];
			snprintf(detail, sizeof(detail), "%s translated color zero to %04x", _formats[f].Name, translator[0]);
			Report_Failure("transparent entry", detail);
		}
	}
}


/*
 * A unit factor leaves every gun alone, so with red carried by the palette index this
 * sweep reaches every gun triple in 65536 calls per layout.
 */
static void Test_Adjust_Color_Packing(void)
{
	Checks++;

	PaletteClass palette;
	bool mask[256];

	for (int i = 0; i < 256; i++) {
		palette[i].Set_Red((unsigned char)i);
		mask[i] = true;
	}

	for (int f = 0; f < FORMAT_COUNT; f++) {
		unsigned short translator[256];
		unsigned short expected[256];

		for (int green = 0; green < 256; green++) {
			for (int i = 0; i < 256; i++) {
				palette[i].Set_Green((unsigned char)green);
			}

			for (int blue = 0; blue < 256; blue++) {
				for (int i = 0; i < 256; i++) {
					palette[i].Set_Blue((unsigned char)blue);
				}

				Adjust_Color(_formats[f].Format, palette, translator, 0x10000, 0x10000, 0x10000, 0x10000, mask);
				Model_Adjust_Color(_formats[f], palette, expected, 0x10000, 0x10000, 0x10000, 0x10000, mask);

				int bad = First_Difference(translator, expected);

				if (bad >= 0) {
					char detail[192];
					snprintf(detail, sizeof(detail), "%s rgb(%d,%d,%d) gave %04x, expected %04x",
						_formats[f].Name, bad, green, blue, translator[bad], expected[bad]);
					Report_Failure("adjust packing", detail);
				}
			}
		}
	}
}


/*
 * Every gun value against the tint factors the lighting can ask for, which is the range
 * the saturation boundary of every gun falls in.
 */
static void Test_Adjust_Color_Scaling(void)
{
	Checks++;

	PaletteClass palette;
	bool mask[256];

	/*
	 * The multipliers are odd, so each gun runs over all 256 values while the three guns
	 * of a color stay out of step with each other.
	 */
	for (int i = 0; i < 256; i++) {
		palette[i].Set_Red((unsigned char)i);
		palette[i].Set_Green((unsigned char)((i * 7) & 0xFF));
		palette[i].Set_Blue((unsigned char)((i * 13) & 0xFF));
		mask[i] = true;
	}

	for (int f = 0; f < FORMAT_COUNT; f++) {
		unsigned short translator[256];
		unsigned short expected[256];

		for (int factor = 0; factor <= 4 * 0x10000; factor += _formats[f].FactorStep) {
			Adjust_Color(_formats[f].Format, palette, translator, factor, factor, factor, 0, mask);
			Model_Adjust_Color(_formats[f], palette, expected, (unsigned int)factor, (unsigned int)factor, (unsigned int)factor, 0, mask);

			int bad = First_Difference(translator, expected);

			if (bad >= 0) {
				char detail[192];
				snprintf(detail, sizeof(detail), "%s factor %d color %d gave %04x, expected %04x",
					_formats[f].Name, factor, bad, translator[bad], expected[bad]);
				Report_Failure("adjust scaling", detail);
			}
		}
	}
}


/*
 * The mask decides which colors take the tint and which take the plain intensity, so the
 * three tints and the intensity are given values far enough apart that a channel or a
 * branch taken by mistake cannot land on the right answer.
 */
static void Test_Adjust_Color_Tint_Mask(void)
{
	Checks++;

	PaletteClass palette;
	bool mask[256];

	for (int i = 0; i < 256; i++) {
		palette[i].Set_Red((unsigned char)i);
		palette[i].Set_Green((unsigned char)(255 - i));
		palette[i].Set_Blue((unsigned char)((i * 5) & 0xFF));
	}

	static int const _factors[] = {0, 1, 0x100, 0x4000, 0xC000, 0x10000, 0x18000, 0x1FFFF, 0x20000, 0x40000};
	int const factor_count = (int)(sizeof(_factors) / sizeof(_factors[0]));

	for (int f = 0; f < FORMAT_COUNT; f++) {
		unsigned short translator[256];
		unsigned short expected[256];

		for (int pattern = 0; pattern < 5; pattern++) {
			for (int i = 0; i < 256; i++) {
				switch (pattern) {
					case 0: mask[i] = true; break;
					case 1: mask[i] = false; break;
					case 2: mask[i] = (i & 1) != 0; break;
					case 3: mask[i] = (i & 1) == 0; break;
					default: mask[i] = (i % 7) < 3; break;
				}
			}

			for (int r = 0; r < factor_count; r++) {
				for (int g = 0; g < factor_count; g++) {
					for (int b = 0; b < factor_count; b++) {
						for (int n = 0; n < factor_count; n += 3) {
							Adjust_Color(_formats[f].Format, palette, translator, _factors[r], _factors[g], _factors[b], _factors[n], mask);
							Model_Adjust_Color(_formats[f], palette, expected, (unsigned int)_factors[r], (unsigned int)_factors[g], (unsigned int)_factors[b], (unsigned int)_factors[n], mask);

							int bad = First_Difference(translator, expected);

							if (bad >= 0) {
								char detail[256];
								snprintf(detail, sizeof(detail), "%s pattern %d factors %d/%d/%d/%d color %d gave %04x, expected %04x",
									_formats[f].Name, pattern, _factors[r], _factors[g], _factors[b], _factors[n], bad, translator[bad], expected[bad]);
								Report_Failure("adjust tint mask", detail);
							}
						}
					}
				}
			}
		}
	}
}


/*
 * Every display pixel paired with every brightness ramp value, for every pixel layout.
 * That is the whole input domain of a single brightened pixel.
 */
static void Test_Brighten_Color_Exhaustive(void)
{
	Checks++;

	std::vector<unsigned char> ramp(65536);
	std::vector<unsigned short> identity(65536);
	std::vector<unsigned short> pixels(65536);
	std::vector<unsigned short> expected(65536);

	for (int color = 0; color < 65536; color++) {
		identity[color] = (unsigned short)color;
	}

	for (int f = 0; f < FORMAT_COUNT; f++) {
		for (int multiplier = 0; multiplier < 256; multiplier++) {
			memset(ramp.data(), multiplier, ramp.size());
			memcpy(pixels.data(), identity.data(), identity.size() * sizeof(unsigned short));
			memcpy(expected.data(), identity.data(), identity.size() * sizeof(unsigned short));

			Brighten_Color(_formats[f].Format, ramp.data(), pixels.data(), 65536, 65536 * 2, 65536, 1);
			Model_Brighten_Color(_formats[f], ramp.data(), expected.data(), 65536, 65536 * 2, 65536, 1);

			if (memcmp(pixels.data(), expected.data(), pixels.size() * sizeof(unsigned short)) != 0) {
				for (int color = 0; color < 65536; color++) {
					if (pixels[color] != expected[color]) {
						char detail[192];
						snprintf(detail, sizeof(detail), "%s pixel %04x times %d gave %04x, expected %04x",
							_formats[f].Name, color, multiplier, pixels[color], expected[color]);
						Report_Failure("brighten exhaustive", detail);
						break;
					}
				}
			}
		}
	}
}


/*
 * The two buffers are walked at different pitches and the routine writes a rectangle
 * inside a larger one, so this checks that it stays within it and leaves a pixel whose
 * ramp value is zero untouched.
 */
static void Test_Brighten_Color_Geometry(void)
{
	Checks++;

	int const ramp_pitch = 256;
	int const pixel_pitch = 300 * 2;
	int const rows = 40;

	std::vector<unsigned char> ramp((size_t)ramp_pitch * rows);
	std::vector<unsigned short> pixels((size_t)(pixel_pitch / 2) * rows);
	std::vector<unsigned short> expected(pixels.size());

	unsigned int seed = 0x1234567u;

	for (size_t i = 0; i < ramp.size(); i++) {
		seed = seed * 1103515245u + 12345u;
		ramp[i] = (unsigned char)((seed >> 16) & 0xFF);

		/*
		 * A ramp that is zero only where nothing would change would not prove much, so
		 * roughly a quarter of the samples are forced to zero.
		 */
		if (((seed >> 24) & 3u) == 0) {
			ramp[i] = 0;
		}
	}

	for (size_t i = 0; i < pixels.size(); i++) {
		seed = seed * 1103515245u + 12345u;
		pixels[i] = (unsigned short)(seed >> 13);
		expected[i] = pixels[i];
	}

	for (int f = 0; f < FORMAT_COUNT; f++) {
		for (int width = 0; width <= 37; width++) {
			for (int height = 0; height <= 7; height++) {
				std::vector<unsigned short> work = pixels;
				std::vector<unsigned short> reference = expected;

				Brighten_Color(_formats[f].Format, ramp.data(), work.data(), ramp_pitch, pixel_pitch, width, height);
				Model_Brighten_Color(_formats[f], ramp.data(), reference.data(), ramp_pitch, pixel_pitch, width, height);

				if (work != reference) {
					char detail[128];
					snprintf(detail, sizeof(detail), "%s %dx%d rectangle differs", _formats[f].Name, width, height);
					Report_Failure("brighten geometry", detail);
				}
			}
		}
	}
}


int main(void)
{
	Test_Transparent_Entry();
	Test_Adjust_Color_Tint_Mask();
	Test_Adjust_Color_Scaling();
	Test_Adjust_Color_Packing();
	Test_Brighten_Color_Geometry();
	Test_Brighten_Color_Exhaustive();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
