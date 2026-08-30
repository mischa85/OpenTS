/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the process and module entry points: CommandLineToArgvW and the quoting it
// undoes, GetCommandLineA and GetCommandLineW, GetModuleHandleA, GetModuleFileNameA, and
// the slim reader/writer lock.
//
// The command line cases build on both targets. On Windows they establish what the shell
// API actually does with a backslash before a quote and with a run of quotes; on
// WebAssembly they hold win32process.cpp to that same account. A case that passed against
// the substitute alone would be worth nothing.
//
// The rest is asked only of the WebAssembly target, because it is where the answers are
// made up rather than reported. The one that matters most is the last: startup.cpp takes
// the directory out of GetModuleFileNameA and makes it current before the first archive is
// opened, so the check below performs that same sequence and requires the current
// directory to come back unchanged. An answer that failed it would move the engine off its
// game data.

#if defined(__EMSCRIPTEN__)
#include "win32compat.h"
#else
#include <windows.h>
#include <shellapi.h>
#endif

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


/*
** The harness compares wide strings itself. wchar_t is sixteen bits here, as it is under
** MSVC and as the engine's layouts require, while the C library the WebAssembly target
** links was built for the thirty-two bit form, so wcscmp would read the wrong units.
*/
static bool Wide_Equals(WCHAR const * wide, char const * narrow)
{
	size_t index = 0;

	while (narrow[index] != '\0') {
		if (wide[index] != (WCHAR)(unsigned char)narrow[index]) return(false);
		index++;
	}

	return(wide[index] == L'\0');
}


static size_t Wide_Length(WCHAR const * wide)
{
	size_t length = 0;
	while (wide[length] != L'\0') length++;
	return(length);
}


static void Widen(WCHAR * destination, char const * source, size_t count)
{
	size_t index = 0;

	while (source[index] != '\0' && index + 1 < count) {
		destination[index] = (WCHAR)(unsigned char)source[index];
		index++;
	}

	destination[index] = L'\0';
}


/*
** One command line and the arguments it is required to split into. The empty entry after
** the last argument closes the list, so an argument is never itself empty here; the pair
** of quotes that produces an empty argument is checked on its own below.
*/
struct SplitCaseType
{
	char const * CommandLine;
	char const * Arguments[8];
};


static SplitCaseType const SplitCases[] = {

	// The plain form: an unquoted first token, then blanks separating the rest.
	{"prog one two", {"prog", "one", "two", nullptr}},

	// Runs of blanks collapse, and tabs separate as spaces do.
	{"prog   one\ttwo ", {"prog", "one", "two", nullptr}},

	// A quoted first token ends at its closing quote rather than at a blank, which is how
	// an installed path with a space in it survives.
	{"\"C:\\Program Files\\Game.exe\" -SCENARIO=GDI1A.MAP",
		{"C:\\Program Files\\Game.exe", "-SCENARIO=GDI1A.MAP", nullptr}},

	// A quoted run holds blanks inside one argument.
	{"prog \"a b c\" d e", {"prog", "a b c", "d", "e", nullptr}},

	// A quoted run may open and close inside an argument rather than around it.
	{"prog d\"e f\"g h", {"prog", "de fg", "h", nullptr}},

	// Backslashes are ordinary characters when no quote follows them.
	{"prog a\\\\\\b d", {"prog", "a\\\\\\b", "d", nullptr}},

	// An odd backslash before a quote escapes it into the argument. Each pair ahead of it
	// collapses to one.
	{"prog a\\\\\\\"b c", {"prog", "a\\\"b", "c", nullptr}},

	// An even number leaves the quote to open a run, and still collapses in pairs.
	{"prog a\\\\\\\\\"b c\" d", {"prog", "a\\\\b c", "d", nullptr}},

	// Both forms in one argument, with a lone escaped backslash after it.
	{"prog \"ab\\\"c\" \"\\\\\" d", {"prog", "ab\"c", "\\", "d", nullptr}},

	// Inside a quoted run, a pair of quotes stands for one of its own.
	{"prog \"a\"\"b\"", {"prog", "a\"b", nullptr}},

	// Outside one, the same pair opens and closes and contributes nothing.
	{"prog a\"\"b", {"prog", "ab", nullptr}},

	// An unclosed run reaches the end of the command line.
	{"prog \"a b", {"prog", "a b", nullptr}}
};


static void Test_Split(void)
{
	for (SplitCaseType const & test : SplitCases) {

		WCHAR line[256];
		Widen(line, test.CommandLine, 256);

		int count = 0;
		LPWSTR * const argv = CommandLineToArgvW(line, &count);

		if (argv == nullptr) {
			Checks++;
			Failures++;
			printf("FAIL split of [%s]: no arguments returned\n", test.CommandLine);
			continue;
		}

		int expected = 0;
		while (test.Arguments[expected] != nullptr) expected++;

		Checks++;
		if (count != expected) {
			Failures++;
			printf("FAIL split of [%s]: got %d arguments, expected %d\n",
						test.CommandLine, count, expected);
		} else {
			for (int index = 0; index < count; index++) {
				Checks++;
				if (!Wide_Equals(argv[index], test.Arguments[index])) {
					Failures++;
					printf("FAIL split of [%s]: argument %d is not [%s]\n",
								test.CommandLine, index, test.Arguments[index]);
				}
			}
		}

		Check("LocalFree releases the argument block", LocalFree(argv) == nullptr);
	}
}


static void Test_Empty_Argument(void)
{
	WCHAR line[64];
	Widen(line, "prog \"\" tail", 64);

	int count = 0;
	LPWSTR * const argv = CommandLineToArgvW(line, &count);

	Check("a pair of quotes is an argument", argv != nullptr && count == 3);

	if (argv != nullptr) {
		if (count == 3) {
			Check("the pair of quotes is empty", Wide_Equals(argv[1], ""));
			Check("the argument after it survives", Wide_Equals(argv[2], "tail"));
		}
		LocalFree(argv);
	}
}


static void Test_Empty_Command_Line(void)
{
	WCHAR const empty[1] = {0};

	int count = 0;
	LPWSTR * const argv = CommandLineToArgvW(empty, &count);

	Check("an empty command line names the program", argv != nullptr && count == 1);

	if (argv != nullptr) {
		if (count == 1) {
			Check("the program name is not empty", Wide_Length(argv[0]) > 0);
		}
		LocalFree(argv);
	}
}


#if defined(__EMSCRIPTEN__)

static void Test_Module_Handle(void)
{
	HMODULE const self = GetModuleHandleA(nullptr);

	Check("the running program has a handle", self != nullptr);
	Check("the handle is the same one every time", GetModuleHandleA(nullptr) == self);

	// A page has loaded neither, and both callers in the engine take another path when
	// they are told so.
	Check("KERNEL32.DLL is not loaded", GetModuleHandleA("KERNEL32.DLL") == nullptr);
	Check("ntdll.dll is not loaded", GetModuleHandleA("ntdll.dll") == nullptr);
}


static void Test_Module_File_Name(void)
{
	char path[MAX_PATH];

	DWORD const length = GetModuleFileNameA(nullptr, path, sizeof(path));

	Check("the module has a file name", length > 0 && length == strlen(path));
	Check("the file name is a full path", length > 0 && path[0] == '/');

	char again[MAX_PATH];
	Check("the same handle answers the same way",
				GetModuleFileNameA(GetModuleHandleA(nullptr), again, sizeof(again)) == length
				&& strcmp(again, path) == 0);

	// Win32 truncates into the buffer it was given and answers with the whole of it.
	char cramped[8];
	DWORD const cramped_length = GetModuleFileNameA(nullptr, cramped, sizeof(cramped));

	Check("a short buffer is filled and reported full",
				cramped_length == sizeof(cramped) && strlen(cramped) == sizeof(cramped) - 1);
	Check("a short buffer reports why", GetLastError() == ERROR_INSUFFICIENT_BUFFER);

	// The only module this target has is the program itself.
	char other[MAX_PATH] = "unchanged";
	Check("an unknown module has no file name",
				GetModuleFileNameA((HMODULE)&other, other, sizeof(other)) == 0 && other[0] == '\0');
}


/*
** The check the current directory depends on. startup.cpp splits the module file name and
** makes the directory half current before it opens anything, so the sequence below is that
** call site, and the directory it lands in has to be the one it started in.
*/
static void Test_Working_Directory_Survives(void)
{
	char before[MAX_PATH];
	Check("the current directory is readable", GetCurrentDirectoryA(sizeof(before), before) > 0);

	char path_to_exe[MAX_PATH];
	Check("the module file name is available",
				GetModuleFileNameA(nullptr, path_to_exe, sizeof(path_to_exe)) > 0);

	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char directory[MAX_PATH];

	_splitpath(path_to_exe, drive, dir, nullptr, nullptr);
	_makepath(directory, drive, dir, nullptr, nullptr);

	Check("the directory the game runs out of is reachable", SetCurrentDirectoryA(directory) != FALSE);

	char after[MAX_PATH];
	Check("the current directory is still readable", GetCurrentDirectoryA(sizeof(after), after) > 0);
	Check("the current directory did not move", strcmp(before, after) == 0);
}


static void Test_Command_Line(void)
{
	char const * const narrow = GetCommandLineA();
	WCHAR const * const wide = GetCommandLineW();

	Check("the command line is not empty", narrow != nullptr && narrow[0] != '\0');
	Check("both forms say the same thing", wide != nullptr && Wide_Equals(wide, narrow));

	int count = 0;
	LPWSTR * const argv = CommandLineToArgvW(GetCommandLineW(), &count);

	Check("the command line splits", argv != nullptr && count >= 1);

	if (argv != nullptr) {
		char path[MAX_PATH];

		if (count >= 1 && GetModuleFileNameA(nullptr, path, sizeof(path)) > 0) {
			// The engine's callers skip the first token as the program's own, so it has to
			// be the program.
			Check("the first token is the module file name", Wide_Equals(argv[0], path));
		}

		LocalFree(argv);
	}
}


/*
** The lock is uncontended by construction on this target. What is checked is that taking
** it and letting it go are real calls that leave the caller running, in the order the
** logger uses them.
*/
static void Test_Locks(void)
{
	SRWLOCK lock = SRWLOCK_INIT;

	InitializeSRWLock(&lock);

	AcquireSRWLockExclusive(&lock);
	ReleaseSRWLockExclusive(&lock);

	AcquireSRWLockShared(&lock);
	ReleaseSRWLockShared(&lock);

	AcquireSRWLockExclusive(&lock);
	ReleaseSRWLockExclusive(&lock);

	Check("the lock can be taken and released", true);
}

#endif	// __EMSCRIPTEN__


int main(void)
{
	Test_Split();
	Test_Empty_Argument();
	Test_Empty_Command_Line();

#if defined(__EMSCRIPTEN__)
	Test_Module_Handle();
	Test_Module_File_Name();
	Test_Working_Directory_Survives();
	Test_Command_Line();
	Test_Locks();
#endif

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
