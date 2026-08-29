/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The low level voxel drawers, reached through the VoxelDrawFunctions dispatch table in
// voxlib.cpp. They work from VoxelFuncArgumentStruct alone, so this unit needs neither the
// voxel library nor any platform header.

#include "voxdraw.hh"

#include <climits>


/// The three variants that follow build the same table and differ only in whether each
/// store goes through the local reference or names VoxelPixelDeltaTable outright. Which
/// variant a drawer calls is deliberate; do not merge them.

/// <summary>
/// Fills in the voxel projection delta table.
/// This routine is used by the low level voxel drawers before they walk an object. It
/// tabulates the screen offset of every Z step in the column, so that skipping over a run
/// of empty voxels costs a single lookup.
/// </summary>
/// <param name="state">The projection setup for the object about to be drawn.</param>
inline void Fill_Delta_Table1(VoxelFuncArgumentStruct * state)
{
	short (&ztable)[VOXEL_BITMAP_WIDTH][2] = VoxelPixelDeltaTable;

	ztable[0][0] = 0;
	ztable[0][1] = 0;

	Vector3i16 & step_z = state->TransformMatrix[3];

	for (unsigned int z = 1; z < state->ZSize; z++) {
		ztable[z][0] = ztable[z - 1][0] + step_z.I;
		ztable[z][1] = ztable[z - 1][1] + step_z.J;
	}
}


/// <summary>
/// Fills in the voxel projection delta table.
/// This routine is used by the low level voxel drawers before they walk an object. It
/// tabulates the screen offset of every Z step in the column, so that skipping over a run
/// of empty voxels costs a single lookup.
/// </summary>
/// <param name="state">The projection setup for the object about to be drawn.</param>
inline void Fill_Delta_Table3(VoxelFuncArgumentStruct * state)
{
	short (&ztable)[VOXEL_BITMAP_WIDTH][2] = VoxelPixelDeltaTable;

	ztable[0][0] = 0;
	ztable[0][1] = 0;

	Vector3i16 & step_z = state->TransformMatrix[3];

	for (unsigned int z = 1; z < state->ZSize; z++) {
		ztable[z][0] = ztable[z - 1][0] + step_z.I;
		VoxelPixelDeltaTable[z][1] = ztable[z - 1][1] + step_z.J;
	}
}


/// <summary>
/// Fills in the voxel projection delta table.
/// This routine is used by the low level voxel drawers before they walk an object. It
/// tabulates the screen offset of every Z step in the column, so that skipping over a run
/// of empty voxels costs a single lookup.
/// </summary>
/// <param name="state">The projection setup for the object about to be drawn.</param>
inline void Fill_Delta_Table2(VoxelFuncArgumentStruct * state)
{
	short (&ztable)[VOXEL_BITMAP_WIDTH][2] = VoxelPixelDeltaTable;

	ztable[0][0] = 0;
	ztable[0][1] = 0;

	Vector3i16 & step_z = state->TransformMatrix[3];

	for (unsigned int z = 1; z < state->ZSize; z++) {
		VoxelPixelDeltaTable[z][0] = ztable[z - 1][0] + step_z.I;
		VoxelPixelDeltaTable[z][1] = ztable[z - 1][1] + step_z.J;
	}
}


/// <summary>
/// Draws a voxel object without shading it.
/// This is the low level drawer for the forward orientations, used when the draw system
/// has both lighting and the depth buffer switched off. The normal byte carried by each
/// voxel is stepped over rather than consulted, and the raw color indices go straight into
/// the voxel draw buffer. It is reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Regular_Normals(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned int data_offset = ((unsigned int *)state->StartOffset)[state->StartIndex];
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr++;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];

					remaining -= delta;

					// Byte 1 - run length(forward)
					unsigned int run_length = *ptr;
					ptr++;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							unsigned char color_index = *ptr;
							ptr++;

							/*
							 * Byte 3 - normal index
							 */
							ptr++;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, color_index);

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							run_length--;
						}
					}

					// Byte 4 - run length(backward)
					ptr++;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a voxel object without shading it.
/// This is the low level drawer for the reversed orientations, used when the draw system
/// has both lighting and the depth buffer switched off. The normal byte carried by each
/// voxel is stepped over rather than consulted, and the raw color indices go straight into
/// the voxel draw buffer. It is reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Reverse_Normals(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table1(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned int data_offset = ((unsigned int *)state->EndOffset)[state->StartIndex];
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					// Byte 4 - run length(backward)
					unsigned int run_length = *ptr;
					ptr--;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 3 - normal index
							 */
							ptr--;

							/*
							 * Byte 2 - color index
							 */
							unsigned char color_index = *ptr;
							ptr--;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, color_index);

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							run_length--;
						}
					}

					// Byte 1 - run length(forward)
					ptr--;

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr--;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];

					remaining -= delta;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a depth buffered voxel object without shading it.
/// This is the low level drawer for the forward orientations, used when the draw system
/// has the depth buffer switched on but lighting switched off. The normal byte carried by
/// each voxel is stepped over rather than consulted. It is reached through the
/// VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Regular_Normals_ZBuffer(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table3(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;
	unsigned short pixel_z = state->TransformMatrix[0].K;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;
		unsigned short row_start_z = pixel_z;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;
			unsigned short column_start_z = pixel_z;
			unsigned int data_offset = ((unsigned int *)state->StartOffset)[state->StartIndex];

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					/*
					 * Byte 0 - pixel delta. This one byte variable serves as
					 * both the pixel delta and, further down, the color index.
					 */
					unsigned char value = *ptr;
					ptr++;

					pixel_x += VoxelPixelDeltaTable[value][0];
					pixel_y += VoxelPixelDeltaTable[value][1];
					pixel_z += state->TransformMatrix[3].K * value;

					remaining -= value;

					// Byte 1 - run length(forward)
					unsigned int run_length = *ptr;
					ptr++;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							value = *ptr;
							ptr++;

							/*
							 * Byte 3 - normal index
							 */
							ptr++;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							if ((pixel_z >> 8) > VoxelDrawZBuffer[buffer_index]) {
								Put_Voxel_Pair(VoxelDrawZBuffer, buffer_index, (unsigned char)(pixel_z >> 8));
								Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, value);
							}

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							pixel_z += state->TransformMatrix[3].K;
							run_length--;
						}
					}

					// Byte 4 - run length(backward)
					ptr++;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			pixel_z = column_start_z + state->TransformMatrix[1].K;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		pixel_z = row_start_z + state->TransformMatrix[2].K;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a depth buffered voxel object without shading it.
/// This is the low level drawer for the reversed orientations, used when the draw system
/// has the depth buffer switched on but lighting switched off. The normal byte carried by
/// each voxel is stepped over rather than consulted. It is reached through the
/// VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Reverse_Normals_ZBuffer(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;
	unsigned short pixel_z = state->TransformMatrix[0].K;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;
		unsigned short row_start_z = pixel_z;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;
			unsigned short column_start_z = pixel_z;
			unsigned int data_offset = ((unsigned int *)state->EndOffset)[state->StartIndex];

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					// Byte 4 - run length(backward)
					unsigned int run_length = *ptr;
					ptr--;

					/*
					 * One byte variable serves as both the color index and,
					 * further down, the pixel delta.
					 */
					unsigned char value;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 3 - normal index
							 */
							ptr--;

							/*
							 * Byte 2 - color index
							 */
							value = *ptr;
							ptr--;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							if ((pixel_z >> 8) > VoxelDrawZBuffer[buffer_index]) {
								Put_Voxel_Pair(VoxelDrawZBuffer, buffer_index, (unsigned char)(pixel_z >> 8));
								Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, value);
							}

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							pixel_z += state->TransformMatrix[3].K;
							run_length--;
						}
					}

					// Byte 1 - run length(forward)
					ptr--;

					/*
					 * Byte 0 - pixel delta
					 */
					value = *ptr;
					ptr--;

					pixel_x += VoxelPixelDeltaTable[value][0];
					pixel_y += VoxelPixelDeltaTable[value][1];
					pixel_z += state->TransformMatrix[3].K * value;

					remaining -= value;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			pixel_z = column_start_z + state->TransformMatrix[1].K;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		pixel_z = row_start_z + state->TransformMatrix[2].K;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a shaded voxel object.
/// This is the low level drawer for the forward orientations, used when the draw system
/// has lighting switched on but the depth buffer switched off. Each voxel's color is
/// remapped through the normal lighting lookup before it reaches the draw buffer. It is
/// reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Regular_Normals_Lighting(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table1(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned int data_offset = ((unsigned int *)state->StartOffset)[state->StartIndex];
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr++;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];

					remaining -= delta;

					// Byte 1 - run length(forward)
					unsigned int run_length = *ptr;
					ptr++;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							unsigned char color_index = *ptr;
							ptr++;

							/*
							 * Byte 3 - normal index
							 */
							unsigned char normal_index = *ptr;
							unsigned char table_index = VoxelNormalTranslateTable[normal_index];
							ptr++;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							color_index = VoxelPaletteTranslateTable[table_index][color_index];

							Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, color_index);

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							run_length--;
						}
					}

					// Byte 4 - run length(backward)
					ptr++;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a shaded voxel object.
/// This is the low level drawer for the reversed orientations, used when the draw system
/// has lighting switched on but the depth buffer switched off. Each voxel's color is
/// remapped through the normal lighting lookup before it reaches the draw buffer. It is
/// reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Reverse_Normals_Lighting(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned int data_offset = ((unsigned int *)state->EndOffset)[state->StartIndex];
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					// Byte 4 - run length(backward)
					unsigned int run_length = *ptr;
					ptr--;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 3 - normal index
							 */
							unsigned char normal_index = *ptr;
							unsigned char table_index = VoxelNormalTranslateTable[normal_index];
							ptr--;

							/*
							 * Byte 2 - color index
							 */
							unsigned char color_index = *ptr;
							ptr--;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							color_index = VoxelPaletteTranslateTable[table_index][color_index];

							Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, color_index);

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							run_length--;
						}
					}

					// Byte 1 - run length(forward)
					ptr--;

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr--;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];

					remaining -= delta;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a shaded and depth buffered voxel object.
/// This is the low level drawer for the forward orientations, used when the draw system
/// has both lighting and the depth buffer switched on. Each voxel is tested against the
/// voxel depth buffer, and the ones that survive are remapped through the normal lighting
/// lookup. It is reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Regular_Normals_ZBuffer_Lighting(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;
	unsigned short pixel_z = state->TransformMatrix[0].K;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;
		unsigned short row_start_z = pixel_z;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;
			unsigned short column_start_z = pixel_z;
			unsigned int data_offset = ((unsigned int *)state->StartOffset)[state->StartIndex];

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr++;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];
					pixel_z += state->TransformMatrix[3].K * delta;

					remaining -= delta;

					// Byte 1 - run length(forward)
					unsigned int run_length = *ptr;
					ptr++;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							if ((pixel_z >> 8) > VoxelDrawZBuffer[buffer_index]) {

								/*
								 * Byte 2 - color index
								 */
								unsigned char color_index = *ptr;
								ptr++;

								/*
								 * Byte 3 - normal index
								 */
								unsigned char normal_index = *ptr;
								unsigned char table_index = VoxelNormalTranslateTable[normal_index];
								ptr++;

								color_index = VoxelPaletteTranslateTable[table_index][color_index];

								Put_Voxel_Pair(VoxelDrawZBuffer, buffer_index, (unsigned char)(pixel_z >> 8));
								Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, color_index);
							} else {
								/*
								 * Byte 2 - color index
								 */
								ptr++;

								/*
								 * Byte 3 - normal index
								 */
								ptr++;
							}

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							pixel_z += state->TransformMatrix[3].K;
							run_length--;
						}
					}

					// Byte 4 - run length(backward)
					ptr++;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			pixel_z = column_start_z + state->TransformMatrix[1].K;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		pixel_z = row_start_z + state->TransformMatrix[2].K;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a shaded and depth buffered voxel object.
/// This is the low level drawer for the reversed orientations, used when the draw system
/// has both lighting and the depth buffer switched on. Each voxel is tested against the
/// voxel depth buffer, and the ones that survive are remapped through the normal lighting
/// lookup. It is reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Reverse_Normals_ZBuffer_Lighting(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table1(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;
	unsigned short pixel_z = state->TransformMatrix[0].K;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;
		unsigned short row_start_z = pixel_z;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;
			unsigned short column_start_z = pixel_z;
			unsigned int data_offset = ((unsigned int *)state->EndOffset)[state->StartIndex];

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					// Byte 4 - run length(backward)
					unsigned int run_length = *ptr;
					ptr--;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							if ((pixel_z >> 8) > VoxelDrawZBuffer[buffer_index]) {

								/*
								 * Byte 3 - normal index
								 */
								unsigned char normal_index = *ptr;
								unsigned char table_index = VoxelNormalTranslateTable[normal_index];
								ptr--;

								/*
								 * Byte 2 - color index
								 */
								unsigned char color_index = *ptr;
								ptr--;

								color_index = VoxelPaletteTranslateTable[table_index][color_index];

								Put_Voxel_Pair(VoxelDrawZBuffer, buffer_index, (unsigned char)(pixel_z >> 8));
								Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, color_index);
							} else {

								/*
								 * Byte 3 - normal index
								 */
								ptr--;

								/*
								 * Byte 2 - color index
								 */
								ptr--;
							}

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							pixel_z += state->TransformMatrix[3].K;
							run_length--;
						}
					}

					// Byte 1 - run length(forward)
					ptr--;

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr--;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];
					pixel_z += state->TransformMatrix[3].K * delta;

					remaining -= delta;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			pixel_z = column_start_z + state->TransformMatrix[1].K;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		pixel_z = row_start_z + state->TransformMatrix[2].K;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a voxel object that carries no normals.
/// This is the low level drawer for the forward orientations, used when the draw system
/// has both lighting and the depth buffer switched off. The color indices go straight into
/// the voxel draw buffer. It is reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Regular(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table1(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned int data_offset = ((unsigned int *)state->StartOffset)[state->StartIndex];
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr++;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];

					remaining -= delta;

					// Byte 1 - run length(forward)
					unsigned int run_length = *ptr;
					ptr++;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							unsigned char color_index = *ptr;
							ptr++;

							/*
							 * A layer without normals covers one buffer pixel per voxel rather
							 * than the pair every other drawer writes.
							 */
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							VoxelDrawBuffer[buffer_index] = color_index;

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							run_length--;
						}
					}

					// Byte 4 - run length(backward)
					ptr++;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a voxel object that carries no normals.
/// This is the low level drawer for the reversed orientations, used when the draw system
/// has both lighting and the depth buffer switched off. The color indices go straight into
/// the voxel draw buffer. It is reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Reverse(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned int data_offset = ((unsigned int *)state->EndOffset)[state->StartIndex];
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					// Byte 4 - run length(backward)
					unsigned int run_length = *ptr;
					ptr--;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							unsigned char color_index = *ptr;
							ptr--;

							/*
							 * A layer without normals covers one buffer pixel per voxel rather
							 * than the pair every other drawer writes.
							 */
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							VoxelDrawBuffer[buffer_index] = color_index;

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							run_length--;
						}
					}

					// Byte 1 - run length(forward)
					ptr--;

					/*
					 * Byte 0 - pixel delta
					 */
					unsigned char delta = *ptr;
					ptr--;

					pixel_x += VoxelPixelDeltaTable[delta][0];
					pixel_y += VoxelPixelDeltaTable[delta][1];

					remaining -= delta;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a depth buffered voxel object that carries no normals.
/// This is the low level drawer for the forward orientations, used when the draw system
/// has the depth buffer switched on but lighting switched off. Each voxel is tested and
/// stamped against the voxel depth buffer before its color reaches the draw buffer. It is
/// reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Regular_ZBuffer(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;
	unsigned short pixel_z = state->TransformMatrix[0].K;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;
		unsigned short row_start_z = pixel_z;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;
			unsigned short column_start_z = pixel_z;
			unsigned int data_offset = ((unsigned int *)state->StartOffset)[state->StartIndex];

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					/*
					 * Byte 0 - pixel delta. This one byte variable serves as
					 * both the pixel delta and, further down, the color index.
					 */
					unsigned char value = *ptr;
					ptr++;

					pixel_x += VoxelPixelDeltaTable[value][0];
					pixel_y += VoxelPixelDeltaTable[value][1];
					pixel_z += state->TransformMatrix[3].K * value;

					remaining -= value;

					// Byte 1 - run length(forward)
					unsigned int run_length = *ptr;
					ptr++;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							value = *ptr;
							ptr++;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							if ((pixel_z >> 8) > VoxelDrawZBuffer[buffer_index]) {
								Put_Voxel_Pair(VoxelDrawZBuffer, buffer_index, (unsigned char)(pixel_z >> 8));
								Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, value);
							}

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							pixel_z += state->TransformMatrix[3].K;
							run_length--;
						}
					}

					// Byte 4 - run length(backward)
					ptr++;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			pixel_z = column_start_z + state->TransformMatrix[1].K;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		pixel_z = row_start_z + state->TransformMatrix[2].K;
		state->StartIndex = state->StrideY + base_index;
	}
}


/// <summary>
/// Draws a depth buffered voxel object that carries no normals.
/// This is the low level drawer for the reversed orientations, used when the draw system
/// has the depth buffer switched on but lighting switched off. Each voxel is tested and
/// stamped against the voxel depth buffer before its color reaches the draw buffer. It is
/// reached through the VoxelDrawFunctions dispatch table.
/// </summary>
/// <param name="state">The projection, stride and voxel data setup for this object.</param>
void __cdecl Draw_Voxel_Reverse_ZBuffer(VoxelFuncArgumentStruct * state)
{
	/*
	 * Precompute screen projection deltas for every Z step
	 */
	Fill_Delta_Table2(state);

	/// Set starting 2D projection position
	unsigned short pixel_x = state->TransformMatrix[0].I;
	unsigned short pixel_y = state->TransformMatrix[0].J;
	unsigned short pixel_z = state->TransformMatrix[0].K;

	/// Iterate over voxel Y slices (rows)
	for (unsigned int y = 0; y < state->YSize; y++) {
		unsigned int base_index = state->StartIndex;
		unsigned short row_start_x = pixel_x;
		unsigned short row_start_y = pixel_y;
		unsigned short row_start_z = pixel_z;

		/// Iterate over voxel X columns (within the current Y row)
		for (unsigned int x = 0; x < state->XSize; x++) {
			unsigned short column_start_x = pixel_x;
			unsigned short column_start_y = pixel_y;
			unsigned short column_start_z = pixel_z;
			unsigned int data_offset = ((unsigned int *)state->EndOffset)[state->StartIndex];

			if (data_offset != UINT_MAX) {
				unsigned char * ptr = state->DataOffset + data_offset;
				unsigned int remaining = state->ZSize;

				/// Parse voxel run-length encoded data along Z axis
				while (remaining) {

					// Byte 4 - run length(backward)
					unsigned int run_length = *ptr;
					ptr--;

					/*
					 * One byte variable serves as both the color index and,
					 * further down, the pixel delta.
					 */
					unsigned char value;

					if (run_length) {
						remaining -= run_length;
						while (run_length) {

							/*
							 * Byte 2 - color index
							 */
							value = *ptr;
							ptr--;

							/// Compute buffer index and write color
							unsigned int buffer_index = (pixel_x >> 8) | (pixel_y & 0xFF00);
							if ((pixel_z >> 8) > VoxelDrawZBuffer[buffer_index]) {
								Put_Voxel_Pair(VoxelDrawZBuffer, buffer_index, (unsigned char)(pixel_z >> 8));
								Put_Voxel_Pair(VoxelDrawBuffer, buffer_index, value);
							}

							pixel_x += state->TransformMatrix[3].I;
							pixel_y += state->TransformMatrix[3].J;
							pixel_z += state->TransformMatrix[3].K;
							run_length--;
						}
					}

					// Byte 1 - run length(forward)
					ptr--;

					/*
					 * Byte 0 - pixel delta
					 */
					value = *ptr;
					ptr--;

					pixel_x += VoxelPixelDeltaTable[value][0];
					pixel_y += VoxelPixelDeltaTable[value][1];
					pixel_z += state->TransformMatrix[3].K * value;

					remaining -= value;
				}
			}

			/// Advance to next voxel in X direction
			pixel_x = column_start_x + state->TransformMatrix[1].I;
			pixel_y = column_start_y + state->TransformMatrix[1].J;
			pixel_z = column_start_z + state->TransformMatrix[1].K;
			state->StartIndex = state->StrideX + state->StartIndex;
		}

		/// Advance to next voxel row (Y direction)
		pixel_x = row_start_x + state->TransformMatrix[2].I;
		pixel_y = row_start_y + state->TransformMatrix[2].J;
		pixel_z = row_start_z + state->TransformMatrix[2].K;
		state->StartIndex = state->StrideY + base_index;
	}
}
