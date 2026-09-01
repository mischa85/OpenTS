/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the two questions every target has to answer for itself: where the running
// program is, and what it was asked to do. platform.h declares both, and each target
// answers them its own way, so the contract is what is checked rather than the mechanism.
//
// The case that matters most is the last. startup.cpp takes the directory out of the
// executable path and makes it current before the first archive is opened, so the check
// below performs that same sequence and requires the current directory to come back
// unchanged. An answer that failed it would move the engine off its game data.

#include "platform.h"

#include <cstdio>
#include <cstring>


static int Failures = 0;
static int Checks = 0;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;

	Failures++;
	printf("FAIL %s\n", name);
}


static void Test_Executable_Path(void)
{
	char path[MAX_PATH];

	memset(path, 0, sizeof(path));
	Check("the program has a path", Platform_Executable_Path(path, sizeof(path)));
	Check("the path is not empty", path[0] != '\0');
	Check("the path is absolute", path[0] == PATH_SEP_CHAR);

	char again[MAX_PATH];
	memset(again, 0, sizeof(again));
	Check("the same question answers the same way",
				Platform_Executable_Path(again, sizeof(again)) && strcmp(again, path) == 0);
}


/*
** The arguments the harness is launched with are fixed by its own CMakeLists, so what the
** host handed over is known here and can be compared rather than merely counted. The one
** holding spaces is the case the engine cares about: a game directory written with spaces
** has to arrive as the single argument it was typed as.
*/
static void Test_Arguments(void)
{
	int count = 0;
	char const * const * const argv = Platform_Command_Line_Arguments(&count);

	Check("the arguments are reported", argv != nullptr);
	if (argv == nullptr) return;

	// Index zero names the program the way the host was asked to run it, which may be a
	// bare or relative name. startup.cpp puts the resolved path there instead, and this is
	// the reason it has to.
	Check("the program itself is index zero", count >= 1 && argv[0] != nullptr && argv[0][0] != '\0');
	if (count < 1) return;

	Check("every argument arrived", count == 4);
	if (count != 4) return;

	Check("a plain argument is passed through", strcmp(argv[1], "-CD") == 0);
	Check("spaces do not split an argument", strcmp(argv[2], "a path with spaces") == 0);
	Check("a quote is not eaten", strcmp(argv[3], "say \"hello\"") == 0);
}


/*
** The sequence the game data depends on. startup.cpp splits the executable path and makes
** the directory half current before it opens anything, so the steps below are that call
** site. What has to hold afterwards is that the program is in the directory the sequence
** landed in: every archive is opened by bare name from there.
*/
static void Test_Working_Directory_Follows_The_Program(void)
{
	char before[MAX_PATH];
	Check("the current directory is readable", GetCurrentDirectoryA(sizeof(before), before) > 0);

	char path_to_exe[MAX_PATH];
	Check("the executable path is available",
				Platform_Executable_Path(path_to_exe, sizeof(path_to_exe)));

	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char name[_MAX_FNAME];
	char extension[_MAX_EXT];
	char directory[MAX_PATH];

	_splitpath(path_to_exe, drive, dir, name, extension);
	_makepath(directory, drive, dir, nullptr, nullptr);

	Check("the directory the game runs out of is reachable", SetCurrentDirectoryA(directory) != FALSE);

	char after[MAX_PATH];
	Check("the current directory is still readable", GetCurrentDirectoryA(sizeof(after), after) > 0);

	char landed[MAX_PATH];
	size_t const length = strlen(after);
	char const * const separator = (length > 0 && after[length - 1] == PATH_SEP_CHAR) ? "" : PATH_SEP_STR;
	snprintf(landed, sizeof(landed), "%s%s%s%s", after, separator, name, extension);

	Check("the program is in the directory the sequence lands in", strcmp(landed, path_to_exe) == 0);

	SetCurrentDirectoryA(before);
}


int main(void)
{
	Test_Executable_Path();
	Test_Arguments();
	Test_Working_Directory_Follows_The_Program();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
