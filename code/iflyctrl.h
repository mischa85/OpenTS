/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#ifdef _WIN32
#include <comdef.h>
#else
#include "win32compat.h"
#endif

/// Names and comments from TLBs

EXTERN_C const IID IID_IFlyControl;

MIDL_INTERFACE("820F501C-4F39-11D2-9B70-00104B972FE8")
IFlyControl : public IUnknown
{
public:
	/*
	 * Landing altitude
	 */
	virtual LONG STDMETHODCALLTYPE Landing_Altitude(void) = 0;

	/*
	 * Lading direction
	 */
	virtual LONG STDMETHODCALLTYPE Landing_Direction(void) = 0;

	/*
	 * Loaded with cargo?
	 */
	virtual BOOL STDMETHODCALLTYPE Is_Loaded(void) = 0;

	/*
	 * Does it strafe over the target rather than hover?
	 */
	virtual LONG STDMETHODCALLTYPE Is_Strafe(void) = 0;

	/*
	 * Is the aircraft locked into straight flight?
	 */
	virtual LONG STDMETHODCALLTYPE Is_Locked(void) = 0;
};

/*
 * IFlyControl com smart pointer declaration.
 */
_COM_SMARTPTR_TYPEDEF(IFlyControl, __uuidof(IFlyControl));
