/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stands in for <basetyps.h> and <initguid.h> on the WebAssembly target. Both exist to
// settle one question: whether DEFINE_GUID declares an interface identifier or defines
// it. A translation unit defines INITGUID before including this to claim the definitions,
// exactly as it does under Windows, and every other one gets the declaration.
//
// This header is deliberately not #pragma once. Including it again after INITGUID changes
// is the whole mechanism.

#if defined(__EMSCRIPTEN__)

#include "win32compat.h"

#undef DEFINE_GUID

#if defined(INITGUID)
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID name
#endif

#endif	// __EMSCRIPTEN__
