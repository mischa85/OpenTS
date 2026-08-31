/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Who the process is and what it was asked to do. win32compat.h declares the module,
// process, thread, command line, and lock entry points; this is where they are answered,
// and it sits apart from win32compat.cpp because none of them has a host call underneath.
// A page is not a process with an image on disk, an argument vector, or a loader, so each
// answer is built out of what this target does have.
//
// The module file name is the one with a trap in it. startup.cpp asks for it, splits the
// directory off it, and makes that the current directory before the first archive is
// opened. An answer naming any other directory therefore moves the engine off its game
// data and nothing loads. What is reported is consequently the current directory by
// construction, which leaves that call where it found things wherever the module runs:
// "/" under a page's preloaded filesystem, and the shell's directory under -sNODERAWFS=1.
//
// The slim reader/writer lock is here for the reason the timer is in win32timer.cpp: what
// it needs is not a stub but a statement about the target, and the statement is that there
// is one thread.

#include "always.h"

#include "win32compat.h"

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>


/*
** What the module file is called. The wasm binary the browser fetched is not a file in the
** filesystem the engine reads, so no name here can be opened; every caller wants the
** directory rather than the file, and this is what stands in the file's place.
*/
static char const PROGRAM_FILE_NAME[] = "OpenTS.wasm";

/*
** The token GetModuleHandle hands back for the running program. Only its address is asked
** of it: the engine keeps it in ProgramInstance and passes it back to the module and
** resource calls, none of which read through it.
*/
static char ProcessModule = 0;


static HMODULE Process_Module(void)
{
	return((HMODULE)&ProcessModule);
}


/*
** Where the program is, as a path. The answer is fixed on the first call rather than
** recomputed, because a module's path does not follow the current directory around.
*/
static char const * Program_Path(void)
{
	static char path[MAX_PATH];

	if (path[0] == '\0') {
		char working[MAX_PATH];

		if (::getcwd(working, sizeof(working)) == nullptr) {
			return(nullptr);
		}

		size_t const length = strlen(working);
		char const * const separator = (length > 0 && working[length - 1] == '/') ? "" : "/";

		int const written = snprintf(path, sizeof(path), "%s%s%s", working, separator, PROGRAM_FILE_NAME);
		if (written < 0 || (size_t)written >= sizeof(path)) {
			path[0] = '\0';
			return(nullptr);
		}
	}

	return(path);
}


HMODULE GetModuleHandleA(LPCSTR name)
{
	if (name == nullptr) {
		SetLastError(NO_ERROR);
		return(Process_Module());
	}

	/*
	** The named modules the engine looks for are KERNEL32.DLL and ntdll.dll, neither of
	** which a page has loaded. Not loaded is both the truth and what Windows answers for a
	** module that is not in the process, so each caller takes the branch it already keeps
	** for an older Windows. Windows reports ERROR_MOD_NOT_FOUND, which win32compat.h has no
	** constant for and no caller here reads.
	*/
	SetLastError(ERROR_FILE_NOT_FOUND);
	return(nullptr);
}


DWORD GetModuleFileNameA(HMODULE module, LPSTR filename, DWORD size)
{
	if (filename == nullptr || size == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	// Null names the running program, which is the only module this target has.
	if (module != nullptr && module != Process_Module()) {
		filename[0] = '\0';
		SetLastError(ERROR_INVALID_HANDLE);
		return(0);
	}

	char const * const path = Program_Path();
	if (path == nullptr) {
		filename[0] = '\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	DWORD const length = (DWORD)strlen(path);

	// Win32 truncates rather than failing, and answers with the whole of the buffer.
	if (length >= size) {
		memcpy(filename, path, size - 1);
		filename[size - 1] = '\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(size);
	}

	memcpy(filename, path, length + 1);
	SetLastError(NO_ERROR);
	return(length);
}


/*
** The host's argument list, which is the whole of this target's command line: the page
** builds it from its query string and hands it over as Module.arguments, and a shell
** running the module under node passes its own arguments through the same array. main
** receives that list and reassembles it for the engine's parser, so what is put together
** below says exactly what startup.cpp was given.
**
** The internal name is read first and the incoming property second, so that a host which
** supplied arguments some other way is still answered; a build that renames the internal
** falls back rather than reporting nothing.
*/
EM_JS(int, Process_Argument_Count, (void), {
	var args = (typeof programArgs !== "undefined" && programArgs) ||
		(typeof Module !== "undefined" && Module["arguments"]) || [];
	return args.length;
});

EM_JS(int, Process_Argument, (int index, char * buffer, int size), {
	var args = (typeof programArgs !== "undefined" && programArgs) ||
		(typeof Module !== "undefined" && Module["arguments"]) || [];
	var text = (index >= 0 && index < args.length) ? "" + args[index] : "";

	var count = 0;
	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[buffer + count] = (code > 127) ? 63 : code;
		count++;
	}
	HEAPU8[buffer + count] = 0;
	return count;
});


/*
** Adds one argument to a command line, quoted the way CommandLineToArgvW below expects to
** find it. A token holding no blank and no quote needs nothing. Anything else is wrapped,
** and the backslashes that precede a quote are doubled -- as are those that end the token,
** which would otherwise escape the closing quote rather than standing for themselves.
*/
static void Append_Argument(std::string & line, char const * text)
{
	if (!line.empty()) {
		line += ' ';
	}

	if (text[0] != '\0' && strpbrk(text, " \t\"") == nullptr) {
		line += text;
		return;
	}

	line += '"';

	for (char const * scan = text; *scan != '\0'; ) {
		size_t slashes = 0;
		while (*scan == '\\') {
			slashes++;
			scan++;
		}

		if (*scan == '\0') {
			line.append(slashes * 2, '\\');
			break;
		}

		if (*scan == '"') {
			line.append(slashes * 2 + 1, '\\');
		} else {
			line.append(slashes, '\\');
		}

		line += *scan++;
	}

	line += '"';
}


static char CommandLineText[2048];
static WCHAR CommandLineWide[2048];


static void Build_Command_Line(void)
{
	static bool built = false;

	if (built) {
		return;
	}
	built = true;

	std::string line;

	char const * const program = Program_Path();
	Append_Argument(line, (program != nullptr) ? program : PROGRAM_FILE_NAME);

	int const count = Process_Argument_Count();
	for (int index = 0; index < count; index++) {
		char argument[512];

		Process_Argument(index, argument, sizeof(argument));
		Append_Argument(line, argument);
	}

	size_t const length = (line.size() < sizeof(CommandLineText)) ? line.size() : sizeof(CommandLineText) - 1;

	memcpy(CommandLineText, line.c_str(), length);
	CommandLineText[length] = '\0';

	// The arguments arrive as ASCII, so a byte is a character and the widening is exact.
	for (size_t index = 0; index <= length; index++) {
		CommandLineWide[index] = (WCHAR)(unsigned char)CommandLineText[index];
	}
}


LPSTR GetCommandLineA(void)
{
	Build_Command_Line();
	return(CommandLineText);
}


LPWSTR GetCommandLineW(void)
{
	Build_Command_Line();
	return(CommandLineWide);
}


static size_t Wide_Length(LPCWSTR text)
{
	size_t length = 0;
	while (text[length] != L'\0') {
		length++;
	}
	return(length);
}


/*
** Splits a command line the way the shell API does, which is not the way the string was
** written. The rules the caller is held to:
**
**   - The first token is not parsed like the others. A quote opens it and the next quote
**     closes it with nothing escaped in between; an unquoted one ends at the first blank.
**   - Backslashes are ordinary characters unless a quote follows them. Then each pair
**     collapses to one backslash, and an odd one left over escapes the quote into the
**     argument instead of letting it open or close a quoted run.
**   - A run of quotes stands partly for itself: every third quote in the run is an
**     argument's own, which is what makes "" the way to write a quote inside quotes.
**   - Blanks separate arguments only outside a quoted run.
**
** The whole result is one allocation, the pointers ahead of the characters they name, so
** that LocalFree releases the arguments along with the array as it does on Windows.
*/
LPWSTR * CommandLineToArgvW(LPCWSTR commandline, int * count)
{
	if (count == nullptr || commandline == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(nullptr);
	}

	*count = 0;

	// Windows answers an empty command line with the running program's own path, which is
	// not a command line to be parsed but a single argument.
	WCHAR program[MAX_PATH];
	if (commandline[0] == L'\0') {
		char const * const path = Program_Path();
		if (path == nullptr) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return(nullptr);
		}

		size_t index = 0;
		while (path[index] != '\0' && index + 1 < MAX_PATH) {
			program[index] = (WCHAR)(unsigned char)path[index];
			index++;
		}
		program[index] = L'\0';

		LPWSTR * const single = (LPWSTR *)malloc(2 * sizeof(LPWSTR) + (index + 1) * sizeof(WCHAR));
		if (single == nullptr) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return(nullptr);
		}

		WCHAR * const text = (WCHAR *)(single + 2);
		memcpy(text, program, (index + 1) * sizeof(WCHAR));

		single[0] = text;
		single[1] = nullptr;

		*count = 1;
		SetLastError(NO_ERROR);
		return(single);
	}

	size_t const length = Wide_Length(commandline);

	/*
	** Every argument is at most as long as what it was parsed from, and the blank that
	** separated it pays for its terminator, so the characters fit in the command line plus
	** two. An argument needs a character of its own as well as a separator, which bounds
	** how many pointers can be wanted the same way.
	*/
	size_t const slots = length + 2;
	LPWSTR * const argv = (LPWSTR *)malloc(slots * sizeof(LPWSTR) + (length + 2) * sizeof(WCHAR));
	if (argv == nullptr) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return(nullptr);
	}

	WCHAR * out = (WCHAR *)(argv + slots);
	LPCWSTR scan = commandline;
	int argc = 0;

	argv[argc++] = out;
	if (*scan == L'"') {
		scan++;
		while (*scan != L'\0' && *scan != L'"') {
			*out++ = *scan++;
		}
		if (*scan == L'"') {
			scan++;
		}
	} else {
		while (*scan != L'\0' && *scan != L' ' && *scan != L'\t') {
			*out++ = *scan++;
		}
	}
	*out++ = L'\0';

	for (;;) {
		while (*scan == L' ' || *scan == L'\t') {
			scan++;
		}

		if (*scan == L'\0') {
			break;
		}

		argv[argc++] = out;

		// Odd while the argument is inside a quoted run, which is what keeps a blank from
		// ending it.
		int quotes = 0;
		size_t slashes = 0;

		for (;;) {
			WCHAR const letter = *scan;

			if (letter == L'\0') break;
			if ((letter == L' ' || letter == L'\t') && quotes == 0) break;

			if (letter == L'\\') {
				slashes++;
				scan++;
				continue;
			}

			if (letter == L'"') {
				for (size_t index = 0; index < slashes / 2; index++) {
					*out++ = L'\\';
				}

				if ((slashes & 1) != 0) {
					*out++ = L'"';
				} else {
					quotes++;
				}

				slashes = 0;
				scan++;

				while (*scan == L'"') {
					if (++quotes == 3) {
						*out++ = L'"';
						quotes = 0;
					}
					scan++;
				}

				if (quotes == 2) {
					quotes = 0;
				}
				continue;
			}

			for (size_t index = 0; index < slashes; index++) {
				*out++ = L'\\';
			}
			slashes = 0;

			*out++ = letter;
			scan++;
		}

		for (size_t index = 0; index < slashes; index++) {
			*out++ = L'\\';
		}
		*out++ = L'\0';
	}

	argv[argc] = nullptr;

	*count = argc;
	SetLastError(NO_ERROR);
	return(argv);
}


/*
** The slim reader/writer lock. The engine runs on the one thread the page lends it -- the
** only CreateThread calls in the tree belong to the crash reporter, which this target does
** not compile -- so a lock is never contended. Every acquisition succeeds at once and
** every release has nothing to hand on, which makes these the whole of the lock rather
** than a stub standing in for one. The state stays zero because there is nothing for it to
** record; a second thread would make that a lie.
*/
void InitializeSRWLock(PSRWLOCK lock)
{
	if (lock != nullptr) {
		lock->Ptr = nullptr;
	}
}


/*
**	A critical section is the same bargain as the slim locks below: one thread means the
**	section is never held by anyone else, so entering always succeeds at once. Try_Enter
**	must say so -- reporting failure would tell a caller it is contended and send it down
**	a back-off path that can never end.
*/
void InitializeCriticalSection(LPCRITICAL_SECTION section)
{
	if (section != nullptr) {
		memset(section, 0, sizeof(*section));
	}
}


void DeleteCriticalSection(LPCRITICAL_SECTION)
{
}


void EnterCriticalSection(LPCRITICAL_SECTION)
{
}


void LeaveCriticalSection(LPCRITICAL_SECTION)
{
}


BOOL TryEnterCriticalSection(LPCRITICAL_SECTION)
{
	return(TRUE);
}


void AcquireSRWLockExclusive(PSRWLOCK)
{
}


void ReleaseSRWLockExclusive(PSRWLOCK)
{
}


void AcquireSRWLockShared(PSRWLOCK)
{
}


void ReleaseSRWLockShared(PSRWLOCK)
{
}


/*
** ---------------------------------------------------------------------------------------
** Modules, threads, and the process.
** ---------------------------------------------------------------------------------------
*/


/*
** Stubs, apart from the handful this target can answer honestly. A stub returns what its
** Win32 original returns on failure, after naming itself once.
*/

HMODULE LoadLibraryA(LPCSTR) { return(WIN32_STUB((HMODULE)nullptr)); }
BOOL FreeLibrary(HMODULE) { return(WIN32_STUB(FALSE)); }
FARPROC GetProcAddress(HMODULE, LPCSTR) { return(WIN32_STUB((FARPROC)nullptr)); }
void ExitProcess(UINT code) { exit((int)code); }
HANDLE GetCurrentProcess(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
HANDLE GetCurrentThread(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
DWORD GetCurrentThreadId(void) { return(1); }
DWORD GetCurrentProcessId(void) { return(WIN32_STUB(0)); }
BOOL SetPriorityClass(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL SetThreadPriority(HANDLE, int) { return(WIN32_STUB(FALSE)); }
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, DWORD, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL TerminateThread(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
DWORD ResumeThread(HANDLE) { return(WIN32_STUB((DWORD)-1)); }
DWORD SuspendThread(HANDLE) { return(WIN32_STUB((DWORD)-1)); }
BOOL GetVersionExA(LPOSVERSIONINFOA) { return(WIN32_STUB(FALSE)); }
void GetSystemInfo(LPSYSTEM_INFO) { WIN32_STUB_VOID(); }
void GlobalMemoryStatus(LPMEMORYSTATUS) { WIN32_STUB_VOID(); }
BOOL TerminateProcess(HANDLE, UINT code) { exit((int)code); }
void RaiseException(DWORD, DWORD, DWORD, ULONG_PTR const *) { WIN32_STUB_ABORT(); }
HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL Module32First(HANDLE, LPMODULEENTRY32) { return(WIN32_STUB(FALSE)); }
BOOL Module32Next(HANDLE, LPMODULEENTRY32) { return(WIN32_STUB(FALSE)); }
BOOL IsDebuggerPresent(void) { return(FALSE); }
PTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(PTOP_LEVEL_EXCEPTION_FILTER) { return(WIN32_STUB((PTOP_LEVEL_EXCEPTION_FILTER)nullptr)); }


/*
** ---------------------------------------------------------------------------------------
** Debug help.
** ---------------------------------------------------------------------------------------
*/


BOOL SymInitialize(HANDLE, LPCSTR, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL SymCleanup(HANDLE) { return(WIN32_STUB(FALSE)); }
DWORD SymSetOptions(DWORD) { return(WIN32_STUB(0)); }
BOOL SymFromAddr(HANDLE, DWORD64, DWORD64 *, PSYMBOL_INFO) { return(WIN32_STUB(FALSE)); }
BOOL SymGetLineFromAddr64(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64) { return(WIN32_STUB(FALSE)); }

#endif	// __EMSCRIPTEN__
