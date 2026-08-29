/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the twelve low level voxel drawers against synthesized layers, so no game
// asset is involved. Every vehicle in the game renders through these routines, and until
// the assembly rasterizers were retired none of them had ever executed, so this harness
// pins the behavior the drawers were checked against the assembly for.
//
// Each drawer is measured two ways. Small hand-built layers assert exact pixels, which is
// what distinguishes a drawer that covers a pair of buffer bytes per voxel from one that
// covers a single byte, and what catches an address computed with the wrong precedence.
// Larger layers are then reduced to a digest of the whole draw and depth buffers, which
// pins the run length walk, the span tables, the delta table and the strides together.
//
// The digests were captured from the drawers as they stand. They record current behavior,
// so a deliberate change to a drawer means recomputing them from the printed values.

#include "voxdraw.hh"

#include <cstdio>
#include <cstring>
#include <vector>


/*
** The drawers reach these through voxdraw.hh. The engine defines them in voxdrsys.cpp and
** voxlib.cpp, both of which are Win32 bound, so the harness supplies its own.
*/

/*
 * Both buffers carry a run of guard bytes past the end of the buffer proper. A drawer that
 * covers a pair of bytes reaches one byte beyond the one a voxel projects onto, so a voxel
 * on the buffer's last byte is the one position where the pair leaves the buffer. Only the
 * first VOXEL_BITMAP_SIZE bytes are the buffer, and everything that clears, digests or
 * reads it keeps to them.
 */
static size_t const VOXEL_GUARD_BYTES = 16;

extern "C" {
unsigned char VoxelPaletteTranslateTable[MAX_PALETTE_LOOKUP_ENTRIES][VOXEL_PALETTE_SIZE];
unsigned char VoxelDrawBuffer[VOXEL_BITMAP_SIZE + VOXEL_GUARD_BYTES];
unsigned char VoxelDrawZBuffer[VOXEL_BITMAP_SIZE + VOXEL_GUARD_BYTES];
short VoxelPixelDeltaTable[VOXEL_BITMAP_WIDTH][2];
unsigned char VoxelNormalTranslateTable[VOXEL_PALETTE_SIZE];
}


static int Failures = 0;
static int Checks = 0;


static void Report_Failure(char const * name, char const * detail)
{
	Failures++;
	printf("FAIL %s: %s\n", name, detail);
}


static void Check_Equal(char const * name, int actual, int expected)
{
	Checks++;

	if (actual != expected) {
		char detail[128];
		snprintf(detail, sizeof(detail), "expected %d, got %d", expected, actual);
		Report_Failure(name, detail);
	}
}


/*
** One run of solid voxels, preceded by a count of the empty voxels skipped over to reach
** it. A column is a list of these, and their skips and lengths must add up to ZSize.
*/
struct VoxelSpan
{
	int Skip;
	std::vector<unsigned char> Colors;
	std::vector<unsigned char> Normals;
};


/*
** A synthesized layer: the run length encoded voxel data plus the two span tables the
** drawers index by column. A column with no voxels at all is marked with UINT_MAX in both
** tables, which is the value the drawers test for.
*/
struct VoxelLayer
{
	std::vector<unsigned char> Data;
	std::vector<unsigned int> StartTable;
	std::vector<unsigned int> EndTable;
	unsigned char XSize;
	unsigned char YSize;
	unsigned char ZSize;
};


static void Init_Layer(VoxelLayer & layer, int x_size, int y_size, int z_size)
{
	layer.XSize = (unsigned char)x_size;
	layer.YSize = (unsigned char)y_size;
	layer.ZSize = (unsigned char)z_size;
	layer.Data.clear();
	layer.StartTable.assign((size_t)(x_size * y_size), 0xFFFFFFFFu);
	layer.EndTable.assign((size_t)(x_size * y_size), 0xFFFFFFFFu);
}


/// <summary>
/// Encodes one column of a synthesized layer.
/// The forward drawers walk the bytes from the first, the reversed drawers from the last,
/// so both span tables are filled in. A voxel occupies two bytes for the drawers that read
/// normals and one byte for the drawers used by a layer whose NormalType is zero.
/// </summary>
/// <param name="layer">The layer being built.</param>
/// <param name="index">The column's index into the span tables.</param>
/// <param name="spans">The runs making up the column, whose lengths must total ZSize.</param>
/// <param name="with_normals">Whether each voxel carries a normal byte.</param>
static void Add_Column(VoxelLayer & layer, int index, std::vector<VoxelSpan> const & spans, bool with_normals)
{
	int total = 0;

	for (VoxelSpan const & span : spans) {
		total += span.Skip + (int)span.Colors.size();
	}

	if (total != layer.ZSize) {
		Report_Failure("layer setup", "column does not add up to ZSize");
		return;
	}

	unsigned int start = (unsigned int)layer.Data.size();

	for (VoxelSpan const & span : spans) {
		layer.Data.push_back((unsigned char)span.Skip);
		layer.Data.push_back((unsigned char)span.Colors.size());

		for (size_t i = 0; i < span.Colors.size(); i++) {
			layer.Data.push_back(span.Colors[i]);
			if (with_normals) {
				layer.Data.push_back(span.Normals[i]);
			}
		}

		layer.Data.push_back((unsigned char)span.Colors.size());
	}

	layer.StartTable[(size_t)index] = start;
	layer.EndTable[(size_t)index] = (unsigned int)layer.Data.size() - 1;
}


/// <summary>
/// Points a drawer argument block at a built layer.
/// </summary>
/// <param name="state">The argument block to fill in.</param>
/// <param name="layer">The layer the drawer is to walk.</param>
/// <param name="reversed">Whether the walk runs backwards through the span tables.</param>
static void Bind_Layer(VoxelFuncArgumentStruct & state, VoxelLayer & layer, bool reversed)
{
	state.StartOffset = (unsigned char *)layer.StartTable.data();
	state.EndOffset = (unsigned char *)layer.EndTable.data();
	state.DataOffset = layer.Data.data();

	state.XSize = layer.XSize;
	state.YSize = layer.YSize;
	state.ZSize = layer.ZSize;

	if (reversed) {
		state.StartIndex = layer.XSize * layer.YSize - 1;
		state.StrideX = -1;
		state.StrideY = -layer.XSize;
	} else {
		state.StartIndex = 0;
		state.StrideX = 1;
		state.StrideY = layer.XSize;
	}
}


static void Set_Vector(Vector3i16 & vector, int i, int j, int k)
{
	vector.I = (unsigned short)i;
	vector.J = (unsigned short)j;
	vector.K = (unsigned short)k;
}


static void Clear_Buffers(void)
{
	memset(VoxelDrawBuffer, 0, sizeof(VoxelDrawBuffer));
	memset(VoxelDrawZBuffer, 0, sizeof(VoxelDrawZBuffer));
	memset(VoxelPixelDeltaTable, 0, sizeof(VoxelPixelDeltaTable));
}


static unsigned int Pixel_At(int column, int row)
{
	return VoxelDrawBuffer[(size_t)(row * VOXEL_BITMAP_WIDTH + column)];
}


static unsigned int Depth_At(int column, int row)
{
	return VoxelDrawZBuffer[(size_t)(row * VOXEL_BITMAP_WIDTH + column)];
}


static int Guard_Written(unsigned char const * buffer)
{
	int written = 0;

	for (size_t i = 0; i < VOXEL_GUARD_BYTES; i++) {
		if (buffer[VOXEL_BITMAP_SIZE + i] != 0) written++;
	}

	return written;
}


/*
** Sets up the lighting lookups the shading drawers read. Every entry is distinct enough
** that a drawer reading the wrong normal byte, or skipping the remap, changes the result.
*/
static void Init_Lighting_Tables(void)
{
	for (int i = 0; i < VOXEL_PALETTE_SIZE; i++) {
		VoxelNormalTranslateTable[i] = (unsigned char)(i % MAX_PALETTE_LOOKUP_ENTRIES);
	}

	for (int t = 0; t < MAX_PALETTE_LOOKUP_ENTRIES; t++) {
		for (int c = 0; c < VOXEL_PALETTE_SIZE; c++) {
			VoxelPaletteTranslateTable[t][c] = (unsigned char)((c + t * 7 + 1) & 0xFF);
		}
	}
}


/*
** Builds a layer holding a single voxel, in a column one voxel deep. Nothing steps in any
** direction, so the voxel lands exactly at the projection origin and the drawer's address
** arithmetic is all that decides where it goes.
*/
static void Build_Single_Voxel(VoxelLayer & layer, VoxelFuncArgumentStruct & state, bool with_normals, bool reversed, int column, int row, int depth, unsigned char color, unsigned char normal)
{
	Init_Layer(layer, 1, 1, 1);

	VoxelSpan span;
	span.Skip = 0;
	span.Colors.push_back(color);
	span.Normals.push_back(normal);

	std::vector<VoxelSpan> spans;
	spans.push_back(span);

	Add_Column(layer, 0, spans, with_normals);
	Bind_Layer(state, layer, reversed);

	Set_Vector(state.TransformMatrix[0], column << 8, row << 8, depth << 8);
	Set_Vector(state.TransformMatrix[1], 0, 0, 0);
	Set_Vector(state.TransformMatrix[2], 0, 0, 0);
	Set_Vector(state.TransformMatrix[3], 0, 0, 0);
}


/// <summary>
/// Checks that a drawer that shades nothing covers the buffer bytes it is supposed to.
/// The unshaded drawers cover a pair of bytes per voxel and the drawers for a layer
/// without normals cover a single byte. Both are checked at an even and at an odd screen
/// column, because an address computed as one term plus one collapses a pair onto the
/// neighboring pair at even columns and leaves odd columns looking correct.
/// </summary>
static void Test_Pixel_Coverage(void)
{
	static struct {
		char const * Name;
		VoxelFuncPtr Draw;
		bool WithNormals;
		bool Reversed;
		bool Shaded;
		int Width;
	} const cases[] = {
		{ "Draw_Voxel_Regular_Normals", &Draw_Voxel_Regular_Normals, true, false, false, 2 },
		{ "Draw_Voxel_Reverse_Normals", &Draw_Voxel_Reverse_Normals, true, true, false, 2 },
		{ "Draw_Voxel_Regular_Normals_Lighting", &Draw_Voxel_Regular_Normals_Lighting, true, false, true, 2 },
		{ "Draw_Voxel_Reverse_Normals_Lighting", &Draw_Voxel_Reverse_Normals_Lighting, true, true, true, 2 },
		{ "Draw_Voxel_Regular", &Draw_Voxel_Regular, false, false, false, 1 },
		{ "Draw_Voxel_Reverse", &Draw_Voxel_Reverse, false, true, false, 1 },
	};

	static int const columns[] = { 40, 41 };

	for (auto const & item : cases) {
		for (int column : columns) {
			int const row = 30;
			unsigned char const color = 0x5A;
			unsigned char const normal = 0x11;

			Clear_Buffers();

			VoxelLayer layer;
			VoxelFuncArgumentStruct state;
			Build_Single_Voxel(layer, state, item.WithNormals, item.Reversed, column, row, 0, color, normal);
			item.Draw(&state);

			unsigned char expected = color;
			if (item.Shaded) {
				expected = VoxelPaletteTranslateTable[VoxelNormalTranslateTable[normal]][color];
			}

			char label[160];

			snprintf(label, sizeof(label), "%s col %d first byte", item.Name, column);
			Check_Equal(label, (int)Pixel_At(column, row), expected);

			snprintf(label, sizeof(label), "%s col %d second byte", item.Name, column);
			Check_Equal(label, (int)Pixel_At(column + 1, row), item.Width == 2 ? expected : 0);

			snprintf(label, sizeof(label), "%s col %d byte past the run", item.Name, column);
			Check_Equal(label, (int)Pixel_At(column + item.Width, row), 0);

			snprintf(label, sizeof(label), "%s col %d byte before the run", item.Name, column);
			Check_Equal(label, (int)Pixel_At(column - 1, row), 0);
		}
	}
}


/// <summary>
/// Checks the depth buffered drawers against a primed depth buffer.
/// A voxel behind what is already there leaves both buffers untouched, and a voxel in
/// front of it stamps its depth across the same pair of bytes it colors.
/// </summary>
static void Test_Depth_Buffer(void)
{
	static struct {
		char const * Name;
		VoxelFuncPtr Draw;
		bool WithNormals;
		bool Reversed;
		bool Shaded;
	} const cases[] = {
		{ "Draw_Voxel_Regular_Normals_ZBuffer", &Draw_Voxel_Regular_Normals_ZBuffer, true, false, false },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer", &Draw_Voxel_Reverse_Normals_ZBuffer, true, true, false },
		{ "Draw_Voxel_Regular_Normals_ZBuffer_Lighting", &Draw_Voxel_Regular_Normals_ZBuffer_Lighting, true, false, true },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer_Lighting", &Draw_Voxel_Reverse_Normals_ZBuffer_Lighting, true, true, true },
		{ "Draw_Voxel_Regular_ZBuffer", &Draw_Voxel_Regular_ZBuffer, false, false, false },
		{ "Draw_Voxel_Reverse_ZBuffer", &Draw_Voxel_Reverse_ZBuffer, false, true, false },
	};

	for (auto const & item : cases) {
		int const column = 60;
		int const row = 44;
		int const depth = 12;
		unsigned char const color = 0x2B;
		unsigned char const normal = 0x23;

		unsigned char expected = color;
		if (item.Shaded) {
			expected = VoxelPaletteTranslateTable[VoxelNormalTranslateTable[normal]][color];
		}

		char label[160];

		/*
		** In front of what is already recorded, so the voxel wins.
		*/
		Clear_Buffers();
		VoxelDrawZBuffer[row * VOXEL_BITMAP_WIDTH + column] = (unsigned char)(depth - 1);
		VoxelDrawZBuffer[row * VOXEL_BITMAP_WIDTH + column + 1] = (unsigned char)(depth - 1);

		VoxelLayer layer;
		VoxelFuncArgumentStruct state;
		Build_Single_Voxel(layer, state, item.WithNormals, item.Reversed, column, row, depth, color, normal);
		item.Draw(&state);

		snprintf(label, sizeof(label), "%s nearer first byte", item.Name);
		Check_Equal(label, (int)Pixel_At(column, row), expected);

		snprintf(label, sizeof(label), "%s nearer second byte", item.Name);
		Check_Equal(label, (int)Pixel_At(column + 1, row), expected);

		snprintf(label, sizeof(label), "%s nearer first depth", item.Name);
		Check_Equal(label, (int)Depth_At(column, row), depth);

		snprintf(label, sizeof(label), "%s nearer second depth", item.Name);
		Check_Equal(label, (int)Depth_At(column + 1, row), depth);

		snprintf(label, sizeof(label), "%s nearer byte past the run", item.Name);
		Check_Equal(label, (int)Pixel_At(column + 2, row), 0);

		/*
		** Behind what is already recorded, so nothing is written at all.
		*/
		Clear_Buffers();
		VoxelDrawZBuffer[row * VOXEL_BITMAP_WIDTH + column] = (unsigned char)(depth + 1);
		VoxelDrawZBuffer[row * VOXEL_BITMAP_WIDTH + column + 1] = (unsigned char)(depth + 1);

		Build_Single_Voxel(layer, state, item.WithNormals, item.Reversed, column, row, depth, color, normal);
		item.Draw(&state);

		snprintf(label, sizeof(label), "%s farther first byte", item.Name);
		Check_Equal(label, (int)Pixel_At(column, row), 0);

		snprintf(label, sizeof(label), "%s farther second byte", item.Name);
		Check_Equal(label, (int)Pixel_At(column + 1, row), 0);

		snprintf(label, sizeof(label), "%s farther first depth", item.Name);
		Check_Equal(label, (int)Depth_At(column, row), depth + 1);
	}
}


/// <summary>
/// Checks that the shading drawers remap through the normal lighting lookup.
/// Two voxels sharing a color but carrying different normals must come out as different
/// palette entries, which fails if a drawer reads the color where the normal belongs or
/// leaves the remap out.
/// </summary>
static void Test_Lighting_Remap(void)
{
	static struct {
		char const * Name;
		VoxelFuncPtr Draw;
		bool Reversed;
	} const cases[] = {
		{ "Draw_Voxel_Regular_Normals_Lighting", &Draw_Voxel_Regular_Normals_Lighting, false },
		{ "Draw_Voxel_Reverse_Normals_Lighting", &Draw_Voxel_Reverse_Normals_Lighting, true },
		{ "Draw_Voxel_Regular_Normals_ZBuffer_Lighting", &Draw_Voxel_Regular_Normals_ZBuffer_Lighting, false },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer_Lighting", &Draw_Voxel_Reverse_Normals_ZBuffer_Lighting, true },
	};

	for (auto const & item : cases) {
		int const column = 70;
		int const row = 50;
		unsigned char const color = 0x30;

		for (unsigned char normal : { (unsigned char)0x02, (unsigned char)0x25 }) {
			Clear_Buffers();

			VoxelLayer layer;
			VoxelFuncArgumentStruct state;
			Build_Single_Voxel(layer, state, true, item.Reversed, column, row, 8, color, normal);
			item.Draw(&state);

			char label[160];
			snprintf(label, sizeof(label), "%s normal %u", item.Name, (unsigned)normal);
			Check_Equal(label, (int)Pixel_At(column, row), VoxelPaletteTranslateTable[VoxelNormalTranslateTable[normal]][color]);
		}
	}
}


/// <summary>
/// Checks that a skipped run of empty voxels moves the projection by the delta table.
/// The column holds two voxels three empty ones apart, so a drawer that ignores the skip,
/// or that consults the wrong table entry, puts the far voxel on the wrong row. The
/// reversed drawer meets the column's runs in the opposite order, which is why the two
/// colors land the other way around.
/// </summary>
static void Test_Delta_Table_Skip(void)
{
	static struct {
		char const * Name;
		VoxelFuncPtr Draw;
		bool Reversed;
	} const cases[] = {
		{ "Draw_Voxel_Regular_Normals", &Draw_Voxel_Regular_Normals, false },
		{ "Draw_Voxel_Reverse_Normals", &Draw_Voxel_Reverse_Normals, true },
	};

	for (auto const & item : cases) {
		int const column = 80;
		int const row = 60;
		int const gap = 3;
		unsigned char const near_color = 0x77;
		unsigned char const far_color = 0x21;

		Clear_Buffers();

		VoxelLayer layer;
		Init_Layer(layer, 1, 1, 5);

		VoxelSpan near_span;
		near_span.Skip = 0;
		near_span.Colors.push_back(near_color);
		near_span.Normals.push_back(0x01);

		VoxelSpan far_span;
		far_span.Skip = gap;
		far_span.Colors.push_back(far_color);
		far_span.Normals.push_back(0x02);

		std::vector<VoxelSpan> spans;
		spans.push_back(near_span);
		spans.push_back(far_span);
		Add_Column(layer, 0, spans, true);

		VoxelFuncArgumentStruct state;
		Bind_Layer(state, layer, item.Reversed);
		Set_Vector(state.TransformMatrix[0], column << 8, row << 8, 0);
		Set_Vector(state.TransformMatrix[1], 0, 0, 0);
		Set_Vector(state.TransformMatrix[2], 0, 0, 0);
		Set_Vector(state.TransformMatrix[3], 0, 0x0100, 0);

		item.Draw(&state);

		unsigned char at_origin = item.Reversed ? far_color : near_color;
		unsigned char across_gap = item.Reversed ? near_color : far_color;

		char label[160];

		snprintf(label, sizeof(label), "%s origin row", item.Name);
		Check_Equal(label, (int)Pixel_At(column, row), at_origin);

		snprintf(label, sizeof(label), "%s across the gap", item.Name);
		Check_Equal(label, (int)Pixel_At(column, row + gap + 1), across_gap);

		snprintf(label, sizeof(label), "%s inside the gap", item.Name);
		Check_Equal(label, (int)Pixel_At(column, row + 1), 0);
	}
}


static unsigned int Digest(unsigned char const * data, size_t size)
{
	unsigned int hash = 2166136261u;

	for (size_t i = 0; i < size; i++) {
		hash ^= data[i];
		hash *= 16777619u;
	}

	return hash;
}


static unsigned int Random_Next(unsigned int & seed)
{
	seed = seed * 1664525u + 1013904223u;
	return seed >> 16;
}


/*
** Builds a layer whose columns hold a repeatable spread of runs, gaps and empty columns,
** so that a walk exercises the span tables, the strides and the delta table together.
*/
static void Build_Sample_Layer(VoxelLayer & layer, bool with_normals, unsigned int seed)
{
	int const x_size = 6;
	int const y_size = 5;
	int const z_size = 8;

	Init_Layer(layer, x_size, y_size, z_size);

	for (int index = 0; index < x_size * y_size; index++) {

		if ((Random_Next(seed) % 7u) == 0u) {
			continue;
		}

		std::vector<VoxelSpan> spans;
		int used = 0;

		while (used < z_size) {
			VoxelSpan span;

			int skip = (int)(Random_Next(seed) % 3u);
			if (skip > z_size - used) {
				skip = z_size - used;
			}
			used += skip;
			span.Skip = skip;

			int count = 1 + (int)(Random_Next(seed) % 3u);
			if (count > z_size - used) {
				count = z_size - used;
			}
			used += count;

			for (int i = 0; i < count; i++) {
				span.Colors.push_back((unsigned char)(Random_Next(seed) & 0xFFu));
				span.Normals.push_back((unsigned char)(Random_Next(seed) & 0xFFu));
			}

			spans.push_back(span);
		}

		Add_Column(layer, index, spans, with_normals);
	}
}


/// <summary>
/// Reduces a whole draw of every drawer to a digest of the two buffers.
/// The expected values were captured from the drawers as they stand, so this pins the run
/// length walk, the span table selection, the strides and the delta table against any
/// unintended change.
/// </summary>
static void Test_Layer_Digests(void)
{
	static struct {
		char const * Name;
		VoxelFuncPtr Draw;
		bool WithNormals;
		bool Reversed;
		unsigned int ColorDigest;
		unsigned int DepthDigest;
	} const cases[] = {
		{ "Draw_Voxel_Regular_Normals", &Draw_Voxel_Regular_Normals, true, false, 0x69A38E79u, 0x5E509DC5u },
		{ "Draw_Voxel_Reverse_Normals", &Draw_Voxel_Reverse_Normals, true, true, 0xB4DE9707u, 0x5E509DC5u },
		{ "Draw_Voxel_Regular_Normals_ZBuffer", &Draw_Voxel_Regular_Normals_ZBuffer, true, false, 0x4BFC72E7u, 0x19062BABu },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer", &Draw_Voxel_Reverse_Normals_ZBuffer, true, true, 0xFD0DAC39u, 0xF1163F2Fu },
		{ "Draw_Voxel_Regular_Normals_Lighting", &Draw_Voxel_Regular_Normals_Lighting, true, false, 0x64BB22FBu, 0x5E509DC5u },
		{ "Draw_Voxel_Reverse_Normals_Lighting", &Draw_Voxel_Reverse_Normals_Lighting, true, true, 0x45A367A7u, 0x5E509DC5u },
		{ "Draw_Voxel_Regular_Normals_ZBuffer_Lighting", &Draw_Voxel_Regular_Normals_ZBuffer_Lighting, true, false, 0x9AE757C7u, 0x19062BABu },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer_Lighting", &Draw_Voxel_Reverse_Normals_ZBuffer_Lighting, true, true, 0x50DCD54Bu, 0xF1163F2Fu },
		{ "Draw_Voxel_Regular", &Draw_Voxel_Regular, false, false, 0x03902ACFu, 0x5E509DC5u },
		{ "Draw_Voxel_Reverse", &Draw_Voxel_Reverse, false, true, 0xC7684BA0u, 0x5E509DC5u },
		{ "Draw_Voxel_Regular_ZBuffer", &Draw_Voxel_Regular_ZBuffer, false, false, 0xA62E27D3u, 0x10118063u },
		{ "Draw_Voxel_Reverse_ZBuffer", &Draw_Voxel_Reverse_ZBuffer, false, true, 0x963FDD91u, 0xA88DB46Fu },
	};

	VoxelLayer with_normals;
	VoxelLayer without_normals;
	Build_Sample_Layer(with_normals, true, 0x13579BDFu);
	Build_Sample_Layer(without_normals, false, 0x2468ACE0u);

	for (auto const & item : cases) {
		Clear_Buffers();

		VoxelFuncArgumentStruct state;
		Bind_Layer(state, item.WithNormals ? with_normals : without_normals, item.Reversed);

		Set_Vector(state.TransformMatrix[0], 0x1400, 0x1400, 0x1000);
		Set_Vector(state.TransformMatrix[1], 0x0300, 0x0100, 0x0080);
		Set_Vector(state.TransformMatrix[2], 0x0180, 0x0300, 0x0040);
		Set_Vector(state.TransformMatrix[3], 0x0080, 0x0100, 0x0100);

		item.Draw(&state);

		unsigned int color_digest = Digest(VoxelDrawBuffer, VOXEL_BITMAP_SIZE);
		unsigned int depth_digest = Digest(VoxelDrawZBuffer, VOXEL_BITMAP_SIZE);

		Checks += 2;

		if (color_digest != item.ColorDigest || depth_digest != item.DepthDigest) {
			char detail[160];
			snprintf(detail, sizeof(detail), "digests are 0x%08Xu and 0x%08Xu, expected 0x%08Xu and 0x%08Xu",
				color_digest, depth_digest, item.ColorDigest, item.DepthDigest);
			Report_Failure(item.Name, detail);
		}
	}
}


/// <summary>
/// Checks that a voxel on the buffer's last byte leaves the guard bytes alone.
/// Every drawer that covers a pair of buffer bytes writes the byte after the one the voxel
/// projects onto, and the last byte of the buffer is the single position where that byte is
/// outside it. The depth buffered drawers are given a cleared depth buffer so that the
/// voxel wins its depth test and both of its stores are made.
/// </summary>
static void Test_Buffer_End_Clip(void)
{
	static struct {
		char const * Name;
		VoxelFuncPtr Draw;
		bool WithNormals;
		bool Reversed;
		bool Shaded;
		bool Depth;
	} const cases[] = {
		{ "Draw_Voxel_Regular_Normals", &Draw_Voxel_Regular_Normals, true, false, false, false },
		{ "Draw_Voxel_Reverse_Normals", &Draw_Voxel_Reverse_Normals, true, true, false, false },
		{ "Draw_Voxel_Regular_Normals_ZBuffer", &Draw_Voxel_Regular_Normals_ZBuffer, true, false, false, true },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer", &Draw_Voxel_Reverse_Normals_ZBuffer, true, true, false, true },
		{ "Draw_Voxel_Regular_Normals_Lighting", &Draw_Voxel_Regular_Normals_Lighting, true, false, true, false },
		{ "Draw_Voxel_Reverse_Normals_Lighting", &Draw_Voxel_Reverse_Normals_Lighting, true, true, true, false },
		{ "Draw_Voxel_Regular_Normals_ZBuffer_Lighting", &Draw_Voxel_Regular_Normals_ZBuffer_Lighting, true, false, true, true },
		{ "Draw_Voxel_Reverse_Normals_ZBuffer_Lighting", &Draw_Voxel_Reverse_Normals_ZBuffer_Lighting, true, true, true, true },
		{ "Draw_Voxel_Regular_ZBuffer", &Draw_Voxel_Regular_ZBuffer, false, false, false, true },
		{ "Draw_Voxel_Reverse_ZBuffer", &Draw_Voxel_Reverse_ZBuffer, false, true, false, true },
	};

	for (auto const & item : cases) {
		int const column = VOXEL_BITMAP_WIDTH - 1;
		int const row = VOXEL_BITMAP_HEIGHT - 1;
		int const depth = 12;
		unsigned char const color = 0x6D;
		unsigned char const normal = 0x17;

		unsigned char expected = color;
		if (item.Shaded) {
			expected = VoxelPaletteTranslateTable[VoxelNormalTranslateTable[normal]][color];
		}

		Clear_Buffers();

		VoxelLayer layer;
		VoxelFuncArgumentStruct state;
		Build_Single_Voxel(layer, state, item.WithNormals, item.Reversed, column, row, depth, color, normal);
		item.Draw(&state);

		char label[160];

		snprintf(label, sizeof(label), "%s last byte", item.Name);
		Check_Equal(label, (int)Pixel_At(column, row), expected);

		if (item.Depth) {
			snprintf(label, sizeof(label), "%s last depth", item.Name);
			Check_Equal(label, (int)Depth_At(column, row), depth);
		}

		snprintf(label, sizeof(label), "%s draw guard", item.Name);
		Check_Equal(label, Guard_Written(VoxelDrawBuffer), 0);

		snprintf(label, sizeof(label), "%s depth guard", item.Name);
		Check_Equal(label, Guard_Written(VoxelDrawZBuffer), 0);
	}
}


int main(void)
{
	Init_Lighting_Tables();

	Test_Pixel_Coverage();
	Test_Depth_Buffer();
	Test_Lighting_Remap();
	Test_Delta_Table_Skip();
	Test_Buffer_End_Clip();
	Test_Layer_Digests();

	printf("%d checks, %d failures\n", Checks, Failures);

	return Failures == 0 ? 0 : 1;
}
