/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#ifndef _WIN32
#include "win32compat.h"
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#endif

/*
** The compound file a saved game is written into. Windows has one in OLE; the WebAssembly
** target has none, so StgCreateDocfile and StgOpenStorage resolve to these there. The bytes
** are the container Microsoft publishes as MS-CFB, which is what lets a save cross between
** the two builds rather than only between browsers.
**
** Only a root storage holding streams is implemented, because that is all a save file is. A
** sub-storage is refused rather than half supported, and the property set that OLE layers
** over a storage is not offered at all -- savever.cpp writes its values as ordinary streams
** as well, and reads them back that way when no property set answers.
**
** This is built on every target rather than behind the Emscripten check its binding sits
** behind, because the harness in tests/save checks the writer here against OLE's own reader,
** and that can only run on Windows.
*/

HRESULT DocFile_Create(char const * path, DWORD mode, IStorage ** storage);
HRESULT DocFile_Open(char const * path, DWORD mode, IStorage ** storage);
HRESULT DocFile_Is_Storage_File(char const * path);
