/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The state the low level voxel drawers work from, kept apart from voxdrsys.h so that
// voxdraw.cpp needs no platform header. The drawers were reached from assembly through
// this struct alone, which is why it carries copies of the layer data rather than a
// reference to the library.

#pragma once

#include "vector3i.h"

#include "voxel.hh"


#define VOXEL_BITMAP_WIDTH 256
#define VOXEL_BITMAP_HEIGHT 256
#define VOXEL_BITMAP_BPP 1
#define VOXEL_BITMAP_SIZE (VOXEL_BITMAP_WIDTH * VOXEL_BITMAP_HEIGHT * VOXEL_BITMAP_BPP)


/*
 * Struct used to pass data to the low-level voxel drawing functions.
 */
struct VoxelFuncArgumentStruct {
	/*
	 * These are copies of the layer's two span tables and of its run length encoded voxel
	 * data, taken so that the low level drawers need no access to the library itself. A
	 * drawer reads whichever span table matches the direction it walks the layer in.
	 */
	unsigned char * StartOffset;
	unsigned char * EndOffset;
	unsigned char * DataOffset;

	/*
	 * This is the index into the span table of the column the walk begins at, chosen from
	 * the anchor corner so that the layer is drawn back to front. The drawers advance it as
	 * they go, so it names the current column once drawing is under way.
	 */
	int StartIndex;

	/*
	 * These are the amounts added to StartIndex to reach the next column and the next row,
	 * signed to suit the direction the anchor corner sends the walk in.
	 */
	int StrideX;
	int StrideY;

	/*
	 * This is the projection the drawers work in. The first entry is the screen position of
	 * the anchor corner and the other three are the steps taken per voxel along X, Y and Z,
	 * all expressed as 8.8 fixed point so that the walk can be stepped in integers.
	 */
	Vector3i16 TransformMatrix[4];

	/*
	 * These are the dimensions of the layer being drawn, measured in voxels.
	 */
	unsigned char XSize;
	unsigned char YSize;
	unsigned char ZSize;
};


extern "C" {
extern unsigned char VoxelPaletteTranslateTable[MAX_PALETTE_LOOKUP_ENTRIES][VOXEL_PALETTE_SIZE];
extern unsigned char VoxelDrawBuffer[];
extern unsigned char VoxelDrawZBuffer[];
extern short VoxelPixelDeltaTable[VOXEL_BITMAP_WIDTH][2];
extern unsigned char VoxelNormalTranslateTable[VOXEL_PALETTE_SIZE];
}


/// <summary>
/// Stores one value across the pair of buffer bytes that a voxel covers.
/// </summary>
/// <param name="buffer">The draw or depth buffer being written.</param>
/// <param name="index">The buffer byte the voxel projects onto.</param>
/// <param name="value">The color index or depth to store.</param>
/// <remarks>A voxel that lands on the buffer's last byte covers that byte alone, since the
/// second byte of the pair falls outside the buffer.</remarks>
inline void Put_Voxel_Pair(unsigned char * buffer, unsigned int index, unsigned char value)
{
	buffer[index] = value;

	if (index + 1 < VOXEL_BITMAP_SIZE) {
		buffer[index + 1] = value;
	}
}


typedef void (__cdecl *VoxelFuncPtr)(VoxelFuncArgumentStruct *);

void __cdecl Draw_Voxel_Regular_Normals(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Reverse_Normals(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Regular_Normals_ZBuffer(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Reverse_Normals_ZBuffer(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Regular_Normals_Lighting(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Reverse_Normals_Lighting(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Regular_Normals_ZBuffer_Lighting(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Reverse_Normals_ZBuffer_Lighting(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Regular(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Reverse(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Regular_ZBuffer(VoxelFuncArgumentStruct * state);
void __cdecl Draw_Voxel_Reverse_ZBuffer(VoxelFuncArgumentStruct * state);
