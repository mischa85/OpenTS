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

EXTERN_C const IID IID_ILinkStream;

MIDL_INTERFACE("0D5CD78E-6470-11D2-9B74-00104B972FE8")
ILinkStream : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE Link_Stream(IUnknown *stream) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unlink_Stream(IUnknown **stream) = 0;
};

/*
 * ILinkStream com smart pointer declaration.
 */
//_COM_SMARTPTR_TYPEDEF(ILinkStream, __uuidof(ILinkStream));
