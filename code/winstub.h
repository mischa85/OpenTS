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

#include "point.h"
#include "win.h"

class Surface;
class PaletteClass;

void Create_Main_Window ( HINSTANCE instance , int command_show , int width , int height);

Point2D Load_Title_Screen(char const * name, Surface * surface, PaletteClass * palette, bool fill = false);

unsigned int Build_Number(void);
