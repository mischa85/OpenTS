/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The language resources a target carries with it. rcimage.py compiles language.rc into
// the tables declared here, so that a target with no module loader reads the same strings
// and dialog templates the Visual Studio build loads out of Language.dll.
//
// The strings arrive narrowed to the code page the game draws in, so nothing converts them
// at runtime. A dialog template is the bytes the Win32 dialog interpreter expects and is
// carried through unaltered. Both tables are sorted by identifier.

#pragma once


struct LanguageStringEntry
{
	unsigned int Id;
	char const * Text;
};


struct LanguageBlobEntry
{
	unsigned int Id;
	unsigned char const * Data;
	unsigned int Size;
};


struct LanguageVersionEntry
{
	char const * Key;
	char const * Value;
};


extern LanguageStringEntry const LanguageStrings[];
extern unsigned int const LanguageStringCount;

extern LanguageBlobEntry const LanguageDialogs[];
extern unsigned int const LanguageDialogCount;

extern LanguageVersionEntry const LanguageVersion[];
extern unsigned int const LanguageVersionCount;
