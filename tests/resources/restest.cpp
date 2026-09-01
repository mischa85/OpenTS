/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Holds the language tables rcimage.py generates to what the lookups in data.cpp assume of
// them: that both are sorted by identifier and hold no duplicates, since the lookups
// bisect them, and that every entry points at something.

#include "language/languageimage.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

static int Failures = 0;
static int Checks = 0;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;

	Failures++;
	printf("FAIL %s\n", name);
}


int main(void)
{
	Check("the string table is not empty", LanguageStringCount > 0);
	Check("the dialog table is not empty", LanguageDialogCount > 0);
	Check("the version table is not empty", LanguageVersionCount > 0);

	bool sorted = true;
	bool present = true;
	for (unsigned int index = 0; index < LanguageStringCount; index++) {
		if (index > 0 && LanguageStrings[index].Id <= LanguageStrings[index - 1].Id) sorted = false;
		if (LanguageStrings[index].Text == NULL) present = false;
	}
	Check("the strings are sorted and hold no duplicate identifier", sorted);
	Check("every string has text", present);

	sorted = true;
	present = true;
	for (unsigned int index = 0; index < LanguageDialogCount; index++) {
		if (index > 0 && LanguageDialogs[index].Id <= LanguageDialogs[index - 1].Id) sorted = false;
		if (LanguageDialogs[index].Data == NULL || LanguageDialogs[index].Size == 0) present = false;
	}
	Check("the dialogs are sorted and hold no duplicate identifier", sorted);
	Check("every dialog carries a template", present);

	// A dialog template opens with its style and its item count, and the interpreter reads
	// both, so a truncated one would be taken for a dialog with no items.
	bool sized = true;
	for (unsigned int index = 0; index < LanguageDialogCount; index++) {
		if (LanguageDialogs[index].Size < 18) sized = false;
	}
	Check("every dialog template is long enough to carry a header", sized);

	present = true;
	for (unsigned int index = 0; index < LanguageVersionCount; index++) {
		if (LanguageVersion[index].Key == NULL || LanguageVersion[index].Value == NULL) present = false;
	}
	Check("every version entry has a key and a value", present);

	// data.cpp asks for these two by name when it reports which language is loaded.
	for (char const * key : {"InternalName", "FileVersion"}) {
		bool found = false;
		for (unsigned int index = 0; index < LanguageVersionCount; index++) {
			if (strcmp(LanguageVersion[index].Key, key) == 0) found = true;
		}
		Check(key, found);
	}

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
