/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "dllver.h"

#ifdef _WIN32
#include <shlwapi.h>
#else
#include "win32compat.h"
#endif

/// Sample code from https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions

/// <summary>
/// Fetches the version number of a library.
/// This routine is used to find out which release of a system library is installed by
/// asking the library itself. Libraries that predate the version query are recognized by
/// their lack of it. The library is loaded only for the duration of the query.
/// </summary>
/// <returns>Returns with the packed major and minor version number. Zero is returned if
/// the library could not be loaded or cannot report its version.</returns>
/// <remarks>Pass a fully qualified path. Loading a library by bare name lets the search
/// order decide which one answers.</remarks>
DWORD GetDllVersion(LPCTSTR lpszDllName)
{
	HINSTANCE hinstDll;
	DWORD dwVersion = 0;

	/*
	 * For security purposes, LoadLibrary should be provided with a fully qualified
	 * path to the DLL. The lpszDllName variable should be tested to ensure that it
	 * is a fully qualified path before it is used.
	 */
	hinstDll = LoadLibrary(lpszDllName);

	if (hinstDll)
	{
		DLLGETVERSIONPROC pDllGetVersion;
		pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hinstDll, "DllGetVersion");

		/*
		 * Because some DLLs might not implement this function, you must test for
		 * it explicitly. Depending on the particular DLL, the lack of a DllGetVersion
		 * function can be a useful indicator of the version.
		 */
		if (pDllGetVersion)
		{
			DLLVERSIONINFO dvi;
			HRESULT hr;

			ZeroMemory(&dvi, sizeof(dvi));
			dvi.cbSize = sizeof(dvi);

			hr = (*pDllGetVersion)(&dvi);

			if (SUCCEEDED(hr))
			{
				dwVersion = PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion);
			}
		}
		FreeLibrary(hinstDll);
	}
	return(dwVersion);
}
