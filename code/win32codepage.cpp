/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The Windows-1252 code page conversions of the Win32 substitute. They live in their own
// translation unit so a harness that needs only text conversion does not drag the window
// manager in with it.

#include "win32window.h"

#if !defined(_WIN32)

#include <cstring>
#include <string>
#include <vector>


/*
** ---------------------------------------------------------------------------------------
** The code page.
** ---------------------------------------------------------------------------------------
*/

/*
** Windows-1252 is Latin-1 apart from the range 0x80 to 0x9F, which this table spells out.
** The five positions the code page leaves undefined carry the C1 control of the same
** value, which is what the Windows table does with them and what makes every byte a round
** trip. peresource.cpp holds the same mappings for the one direction it needs; it has no
** byte to return for a wide character outside them and answers with a question mark, which
** is what this does too.
*/
static unsigned short const _HighRange[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};




/// <summary>
/// Is this a code page these conversions know?
/// </summary>
/// <remarks>
/// The engine's text is Windows-1252 throughout, and CP_ACP is that code page on the
/// systems it was written for.
/// </remarks>
static bool Is_Windows_1252(UINT codepage)
{
	return(codepage == CP_ACP || codepage == 1252);
}


static unsigned short Widen_Character(unsigned char byte)
{
	if (byte < 0x80 || byte >= 0xA0) {
		return(byte);
	}

	return(_HighRange[byte - 0x80]);
}


/// <summary>
/// Finds the byte that carries a wide character, if the code page has one.
/// </summary>
/// <returns>The byte, or -1 when the character cannot be written in Windows-1252.</returns>
/// <remarks>
/// Windows also carries a best fit table that answers a near miss with a resemblance -- a
/// typographic dash with a hyphen, an accented letter with its bare one. This has none, so
/// anything the code page does not hold outright is replaced.
/// </remarks>
static int Narrow_Character(unsigned short code)
{
	if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
		return((int)code);
	}

	for (int index = 0; index < 32; index++) {
		if (_HighRange[index] == code) {
			return(0x80 + index);
		}
	}

	return(-1);
}


/// <summary>
/// Converts Windows-1252 text into UTF-16.
/// </summary>
/// <param name="multibytecount">How many bytes to convert, or -1 for a terminated string,
/// whose terminator is converted along with it.</param>
/// <param name="widecount">How much room the destination has, or zero to ask how much is
/// needed.</param>
/// <returns>int; The number of wide characters produced, or zero on failure.</returns>
int MultiByteToWideChar(UINT codepage, DWORD flags, LPCSTR multibyte, int multibytecount, LPWSTR wide, int widecount)
{
	if (multibyte == nullptr || multibytecount == 0 || widecount < 0 || (widecount > 0 && wide == nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	if (!Is_Windows_1252(codepage)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("MultiByteToWideChar: a code page other than Windows-1252", 0));
	}

	// MB_PRECOMPOSED is the default and asks for what this produces anyway. Anything else
	// changes the answer rather than describing it.
	if ((flags & ~(DWORD)MB_PRECOMPOSED) != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("MultiByteToWideChar: a conversion flag with no implementation", 0));
	}

	/*
	 * A negative count means the string names its own end, and the terminator is part of
	 * what is converted. A given count is a length in bytes and a null inside it is a
	 * character like any other.
	 */
	int length = multibytecount;
	if (length < 0) {
		length = (int)strlen(multibyte) + 1;
	}

	if (widecount == 0) {
		return(length);
	}

	if (widecount < length) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	for (int index = 0; index < length; index++) {
		wide[index] = (WCHAR)Widen_Character((unsigned char)multibyte[index]);
	}

	return(length);
}


/// <summary>
/// Converts UTF-16 text into Windows-1252.
/// </summary>
/// <param name="widecount">How many characters to convert, or -1 for a terminated string,
/// whose terminator is converted along with it.</param>
/// <param name="multibytecount">How much room the destination has, or zero to ask how much
/// is needed.</param>
/// <param name="defaultchar">What to write where the code page has no byte, or NULL for a
/// question mark.</param>
/// <param name="useddefaultchar">Set when at least one character had to be replaced.</param>
/// <returns>int; The number of bytes produced, or zero on failure.</returns>
int WideCharToMultiByte(UINT codepage, DWORD flags, LPCWSTR wide, int widecount, LPSTR multibyte, int multibytecount, LPCSTR defaultchar, LPBOOL useddefaultchar)
{
	if (wide == nullptr || widecount == 0 || multibytecount < 0 || (multibytecount > 0 && multibyte == nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	if (!Is_Windows_1252(codepage)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WideCharToMultiByte: a code page other than Windows-1252", 0));
	}

	if (flags != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WideCharToMultiByte: a conversion flag with no implementation", 0));
	}

	int length = widecount;
	if (length < 0) {
		length = 0;
		while (wide[length] != 0) length++;
		length++;
	}

	char replacement = (defaultchar != nullptr) ? defaultchar[0] : '?';
	bool replaced = false;

	std::string out;
	out.reserve((std::size_t)length);

	for (int index = 0; index < length; index++) {
		unsigned short code = (unsigned short)wide[index];

		/*
		 * A character outside the basic plane arrives as a pair and is one character, so
		 * the pair is consumed together and replaced once.
		 */
		if (code >= 0xD800 && code <= 0xDBFF && (index + 1) < length) {
			unsigned short low = (unsigned short)wide[index + 1];
			if (low >= 0xDC00 && low <= 0xDFFF) {
				index++;
				out.push_back(replacement);
				replaced = true;
				continue;
			}
		}

		int byte = Narrow_Character(code);
		if (byte < 0) {
			out.push_back(replacement);
			replaced = true;
		} else {
			out.push_back((char)byte);
		}
	}

	if (useddefaultchar != nullptr) {
		*useddefaultchar = replaced ? TRUE : FALSE;
	}

	if (multibytecount == 0) {
		return((int)out.size());
	}

	if ((std::size_t)multibytecount < out.size()) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	memcpy(multibyte, out.data(), out.size());
	return((int)out.size());
}


#endif
