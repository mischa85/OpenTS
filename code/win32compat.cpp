/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The out-of-line half of the WebAssembly target's Win32 substitute. win32compat.h
// explains what this stands for and why nothing here succeeds quietly.

#include "always.h"

#include "crtcompat.h"
#include "win32compat.h"

#if defined(__EMSCRIPTEN__)

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>



/*
** The layout contract. Every value below is what MSVC reports for the same construct on
** Win32 x86, so a change to win32compat.h that moves a field or resizes a type fails the
** build here rather than silently reshaping a saved game or a network packet.
*/
static_assert(sizeof(void *) == 4, "wasm32 must be ILP32, as Win32 x86 is");
static_assert(sizeof(BYTE) == 1 && sizeof(WORD) == 2 && sizeof(DWORD) == 4, "");
static_assert(sizeof(LONG) == 4 && sizeof(ULONG) == 4 && sizeof(BOOL) == 4, "");
static_assert(sizeof(LONGLONG) == 8 && alignof(LONGLONG) == 8, "");
static_assert(sizeof(WPARAM) == 4 && sizeof(LPARAM) == 4 && sizeof(LRESULT) == 4, "");
static_assert(sizeof(HRESULT) == 4 && sizeof(HANDLE) == 4 && sizeof(HWND) == 4, "");
static_assert(sizeof(WCHAR) == 2, "-fshort-wchar keeps OLECHAR two bytes, as Windows has it");

static_assert(sizeof(POINT) == 8 && offsetof(POINT, y) == 4, "");
static_assert(sizeof(RECT) == 16 && offsetof(RECT, bottom) == 12, "");
static_assert(sizeof(SIZE) == 8, "");
static_assert(sizeof(MSG) == 28 && offsetof(MSG, pt) == 20, "");
static_assert(sizeof(GUID) == 16 && alignof(GUID) == 4, "");
static_assert(sizeof(FILETIME) == 8, "");
static_assert(sizeof(SYSTEMTIME) == 16, "");
static_assert(sizeof(LARGE_INTEGER) == 8 && sizeof(ULARGE_INTEGER) == 8, "");
static_assert(offsetof(LARGE_INTEGER, u.HighPart) == 4, "");
static_assert(sizeof(CRITICAL_SECTION) == 24, "");
static_assert(sizeof(WIN32_FIND_DATAA) == 320 && offsetof(WIN32_FIND_DATAA, cFileName) == 44, "");
static_assert(sizeof(OSVERSIONINFOA) == 148, "");
static_assert(sizeof(WAVEFORMATEX) == 18, "mmsystem.h packs the wave formats to one byte");
static_assert(sizeof(BITMAPFILEHEADER) == 14, "wingdi.h packs the file header to two bytes");
static_assert(sizeof(BITMAPINFOHEADER) == 40, "");
static_assert(sizeof(RGBQUAD) == 4, "");
static_assert(sizeof(STATSTG) == 72 && offsetof(STATSTG, cbSize) == 8, "");
static_assert(sizeof(DSBUFFERDESC) == 20, "");
static_assert(sizeof(WNDCLASSA) == 40, "");
static_assert(sizeof(SCROLLINFO) == 28, "");
static_assert(sizeof(LOGFONTA) == 60, "");
static_assert(sizeof(EXCEPTION_RECORD) == 80, "");
static_assert(sizeof(CONTEXT) == 716, "");


/*
** Reporting. Each entry point names itself the first time it is reached; a stub inside
** a frame loop would otherwise bury everything else in the log.
*/
static char const * ReportedFunctions[512];
static int ReportedCount = 0;


static bool Already_Reported(char const * function)
{
	for (int index = 0; index < ReportedCount; index++) {
		if (ReportedFunctions[index] == function || strcmp(ReportedFunctions[index], function) == 0) {
			return(true);
		}
	}

	if (ReportedCount < (int)(sizeof(ReportedFunctions) / sizeof(ReportedFunctions[0]))) {
		ReportedFunctions[ReportedCount++] = function;
	}
	return(false);
}


void Win32_Stub_Reached(char const * function)
{
	if (Already_Reported(function)) return;
	fprintf(stderr, "OpenTS: unimplemented Win32 entry point %s reached; it reports failure.\n", function);
	fflush(stderr);
}


void Win32_Stub_Fatal(char const * function)
{
	fprintf(stderr, "OpenTS: unimplemented Win32 entry point %s reached, and it has no way to "
		"report failure to its caller. Stopping rather than continuing on a result that was "
		"never produced.\n", function);
	fflush(stderr);
	abort();
}


/*
** The last-error slot. Nothing here produces a Win32 error code of its own, but callers
** that set one expect to read it back, so the slot is real.
*/
static DWORD LastError = 0;


DWORD GetLastError(void)
{
	return(LastError);
}


void SetLastError(DWORD error)
{
	LastError = error;
}


/*
** Interface identity. See win32compat.h: __uuidof has no Windows meaning here, so this
** hands back a null identifier and says so. Any QueryInterface written against it fails.
*/
IID const & Win32_Uuid_Of(char const * type)
{
	(void)type;
	return(WIN32_STUB(GUID_NULL));
}


extern "C" const GUID GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
extern "C" const IID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IStream = {0x0000000C, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IPersist = {0x0000010C, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IPersistStream = {0x00000109, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IClassFactory = {0x00000001, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};


BOOL IsEqualGUID(REFGUID first, REFGUID second)
{
	return(memcmp(&first, &second, sizeof(GUID)) == 0);
}


/*
** The clocks. A monotonic host clock is available and correct, so these compute their
** answer instead of reporting a stub. GetTickCount and timeGetTime are both defined as
** milliseconds since the first call, which is what the engine uses them for.
*/
static bool ClockStarted = false;
static struct timespec ClockOrigin;


static unsigned long long Monotonic_Nanoseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!ClockStarted) {
		ClockOrigin = now;
		ClockStarted = true;
	}

	return((unsigned long long)(now.tv_sec - ClockOrigin.tv_sec) * 1000000000ULL
		+ (unsigned long long)now.tv_nsec - (unsigned long long)ClockOrigin.tv_nsec);
}


DWORD GetTickCount(void)
{
	return((DWORD)(Monotonic_Nanoseconds() / 1000000ULL));
}


DWORD timeGetTime(void)
{
	return(GetTickCount());
}


BOOL QueryPerformanceCounter(LARGE_INTEGER * count)
{
	if (count == nullptr) return(FALSE);
	count->QuadPart = (LONGLONG)Monotonic_Nanoseconds();
	return(TRUE);
}


BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency)
{
	if (frequency == nullptr) return(FALSE);
	frequency->QuadPart = 1000000000LL;
	return(TRUE);
}


static void Fill_System_Time(SYSTEMTIME * result, struct tm const & parts, long milliseconds)
{
	result->wYear = (WORD)(parts.tm_year + 1900);
	result->wMonth = (WORD)(parts.tm_mon + 1);
	result->wDayOfWeek = (WORD)parts.tm_wday;
	result->wDay = (WORD)parts.tm_mday;
	result->wHour = (WORD)parts.tm_hour;
	result->wMinute = (WORD)parts.tm_min;
	result->wSecond = (WORD)parts.tm_sec;
	result->wMilliseconds = (WORD)milliseconds;
}


void GetSystemTime(SYSTEMTIME * time)
{
	if (time == nullptr) return;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	struct tm parts;
	gmtime_r(&now.tv_sec, &parts);
	Fill_System_Time(time, parts, now.tv_nsec / 1000000);
}


void GetLocalTime(SYSTEMTIME * time)
{
	if (time == nullptr) return;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	struct tm parts;
	localtime_r(&now.tv_sec, &parts);
	Fill_System_Time(time, parts, now.tv_nsec / 1000000);
}


int wsprintfA(LPSTR output, LPCSTR format, ...)
{
	va_list args;

	va_start(args, format);
	int result = vsprintf(output, format, args);
	va_end(args);
	return(result);
}


/*
** Everything below is a stub. Each returns what its Win32 original returns on failure,
** after naming itself once.
*/

HMODULE GetModuleHandleA(LPCSTR) { return(WIN32_STUB((HMODULE)nullptr)); }
DWORD GetModuleFileNameA(HMODULE, LPSTR filename, DWORD size) { if (filename != nullptr && size > 0) filename[0] = '\0'; return(WIN32_STUB(0)); }
HMODULE LoadLibraryA(LPCSTR) { return(WIN32_STUB((HMODULE)nullptr)); }
BOOL FreeLibrary(HMODULE) { return(WIN32_STUB(FALSE)); }
FARPROC GetProcAddress(HMODULE, LPCSTR) { return(WIN32_STUB((FARPROC)nullptr)); }
LPSTR GetCommandLineA(void) { return(WIN32_STUB((LPSTR)"")); }
LPWSTR GetCommandLineW(void) { static WCHAR empty[1] = {0}; return(WIN32_STUB(empty)); }
void ExitProcess(UINT code) { exit((int)code); }
HANDLE GetCurrentProcess(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
HANDLE GetCurrentThread(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
DWORD GetCurrentThreadId(void) { return(WIN32_STUB(0)); }
DWORD GetCurrentProcessId(void) { return(WIN32_STUB(0)); }
BOOL SetPriorityClass(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL SetThreadPriority(HANDLE, int) { return(WIN32_STUB(FALSE)); }
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, DWORD, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL TerminateThread(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
DWORD ResumeThread(HANDLE) { return(WIN32_STUB((DWORD)-1)); }
DWORD SuspendThread(HANDLE) { return(WIN32_STUB((DWORD)-1)); }
void Sleep(DWORD) { WIN32_STUB_VOID(); }
BOOL GetVersionExA(LPOSVERSIONINFOA) { return(WIN32_STUB(FALSE)); }
void GetSystemInfo(LPSYSTEM_INFO) { WIN32_STUB_VOID(); }
void GlobalMemoryStatus(LPMEMORYSTATUS) { WIN32_STUB_VOID(); }


void OutputDebugStringA(LPCSTR string)
{
	if (string != nullptr) fputs(string, stderr);
}


/*
** The critical section is a no-op on a single-threaded target, but it is still reported:
** the engine's threading has not been ported, so any code that reaches one is running
** somewhere its assumptions have not been checked.
*/
void InitializeCriticalSection(LPCRITICAL_SECTION section) { if (section != nullptr) memset(section, 0, sizeof(*section)); WIN32_STUB_VOID(); }
void DeleteCriticalSection(LPCRITICAL_SECTION) { WIN32_STUB_VOID(); }
void EnterCriticalSection(LPCRITICAL_SECTION) { WIN32_STUB_VOID(); }
void LeaveCriticalSection(LPCRITICAL_SECTION) { WIN32_STUB_VOID(); }
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION) { return(WIN32_STUB(FALSE)); }

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL SetEvent(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL ResetEvent(HANDLE) { return(WIN32_STUB(FALSE)); }
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL ReleaseMutex(HANDLE) { return(WIN32_STUB(FALSE)); }
DWORD WaitForSingleObject(HANDLE, DWORD) { return(WIN32_STUB(WAIT_FAILED)); }
DWORD WaitForMultipleObjects(DWORD, HANDLE const *, BOOL, DWORD) { return(WIN32_STUB(WAIT_FAILED)); }

/*
** The interlocked operations are single-threaded here, so they are implemented rather
** than stubbed: the arithmetic is the whole contract once there is only one thread.
*/
LONG InterlockedIncrement(LONG volatile * addend) { LONG value = *addend + 1; *addend = value; return(value); }
LONG InterlockedDecrement(LONG volatile * addend) { LONG value = *addend - 1; *addend = value; return(value); }
LONG InterlockedExchange(LONG volatile * target, LONG value) { LONG old = *target; *target = value; return(old); }

HANDLE CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD read, LPOVERLAPPED) { if (read != nullptr) *read = 0; return(WIN32_STUB(FALSE)); }
BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD written, LPOVERLAPPED) { if (written != nullptr) *written = 0; return(WIN32_STUB(FALSE)); }
DWORD SetFilePointer(HANDLE, LONG, PLONG, DWORD) { return(WIN32_STUB(INVALID_SET_FILE_POINTER)); }
DWORD GetFileSize(HANDLE, LPDWORD) { return(WIN32_STUB(INVALID_FILE_SIZE)); }
BOOL SetEndOfFile(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL FlushFileBuffers(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL CloseHandle(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL DeleteFileA(LPCSTR) { return(WIN32_STUB(FALSE)); }
BOOL MoveFileA(LPCSTR, LPCSTR) { return(WIN32_STUB(FALSE)); }
BOOL CopyFileA(LPCSTR, LPCSTR, BOOL) { return(WIN32_STUB(FALSE)); }
DWORD GetFileAttributesA(LPCSTR) { return(WIN32_STUB(INVALID_FILE_SIZE)); }
BOOL SetFileAttributesA(LPCSTR, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL GetFileTime(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME) { return(WIN32_STUB(FALSE)); }
BOOL SetFileTime(HANDLE, FILETIME const *, FILETIME const *, FILETIME const *) { return(WIN32_STUB(FALSE)); }
HANDLE FindFirstFileA(LPCSTR, LPWIN32_FIND_DATAA) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL FindNextFileA(HANDLE, LPWIN32_FIND_DATAA) { return(WIN32_STUB(FALSE)); }
BOOL FindClose(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL CreateDirectoryA(LPCSTR, LPSECURITY_ATTRIBUTES) { return(WIN32_STUB(FALSE)); }
BOOL RemoveDirectoryA(LPCSTR) { return(WIN32_STUB(FALSE)); }
DWORD GetCurrentDirectoryA(DWORD, LPSTR) { return(WIN32_STUB(0)); }
BOOL SetCurrentDirectoryA(LPCSTR) { return(WIN32_STUB(FALSE)); }
UINT GetDriveTypeA(LPCSTR) { return(WIN32_STUB(DRIVE_UNKNOWN)); }
DWORD GetLogicalDrives(void) { return(WIN32_STUB(0)); }
BOOL GetDiskFreeSpaceA(LPCSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD) { return(WIN32_STUB(FALSE)); }
BOOL GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD) { return(WIN32_STUB(FALSE)); }
DWORD GetTempPathA(DWORD, LPSTR) { return(WIN32_STUB(0)); }
BOOL FileTimeToSystemTime(FILETIME const *, LPSYSTEMTIME) { return(WIN32_STUB(FALSE)); }
BOOL SystemTimeToFileTime(SYSTEMTIME const *, LPFILETIME) { return(WIN32_STUB(FALSE)); }
BOOL FileTimeToLocalFileTime(FILETIME const *, LPFILETIME) { return(WIN32_STUB(FALSE)); }


LONG CompareFileTime(FILETIME const * first, FILETIME const * second)
{
	unsigned long long left = ((unsigned long long)first->dwHighDateTime << 32) | first->dwLowDateTime;
	unsigned long long right = ((unsigned long long)second->dwHighDateTime << 32) | second->dwLowDateTime;

	if (left < right) return(-1);
	if (left > right) return(1);
	return(0);
}


/*
** The global heap is ordinary malloc here. Windows' movable-memory modes are not
** honored, so a caller that asks for one is reported.
*/
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes)
{
	if ((flags & GMEM_MOVEABLE) != 0) WIN32_STUB_VOID();

	void * block = malloc(bytes);
	if (block != nullptr && (flags & GMEM_ZEROINIT) != 0) memset(block, 0, bytes);
	return((HGLOBAL)block);
}


HGLOBAL GlobalFree(HGLOBAL memory) { free(memory); return(nullptr); }
LPVOID GlobalLock(HGLOBAL memory) { return(memory); }
BOOL GlobalUnlock(HGLOBAL) { return(FALSE); }

LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM) { return(WIN32_STUB(0)); }
BOOL PostMessageA(HWND, UINT, WPARAM, LPARAM) { return(WIN32_STUB(FALSE)); }
LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return(WIN32_STUB(0)); }
BOOL PeekMessageA(LPMSG, HWND, UINT, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL GetMessageA(LPMSG, HWND, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL TranslateMessage(MSG const *) { return(WIN32_STUB(FALSE)); }
LRESULT DispatchMessageA(MSG const *) { return(WIN32_STUB(0)); }
void PostQuitMessage(int) { WIN32_STUB_VOID(); }
ATOM RegisterClassA(WNDCLASSA const *) { return(WIN32_STUB(0)); }
ATOM RegisterClassExA(WNDCLASSEXA const *) { return(WIN32_STUB(0)); }
BOOL UnregisterClassA(LPCSTR, HINSTANCE) { return(WIN32_STUB(FALSE)); }
HWND CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) { return(WIN32_STUB((HWND)nullptr)); }
BOOL DestroyWindow(HWND) { return(WIN32_STUB(FALSE)); }
BOOL ShowWindow(HWND, int) { return(WIN32_STUB(FALSE)); }
BOOL UpdateWindow(HWND) { return(WIN32_STUB(FALSE)); }
BOOL MoveWindow(HWND, int, int, int, int, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return(WIN32_STUB(FALSE)); }
BOOL GetWindowRect(HWND, LPRECT rect) { if (rect != nullptr) SetRectEmpty(rect); return(WIN32_STUB(FALSE)); }
BOOL GetClientRect(HWND, LPRECT rect) { if (rect != nullptr) SetRectEmpty(rect); return(WIN32_STUB(FALSE)); }
BOOL ClientToScreen(HWND, LPPOINT) { return(WIN32_STUB(FALSE)); }
BOOL ScreenToClient(HWND, LPPOINT) { return(WIN32_STUB(FALSE)); }
BOOL AdjustWindowRect(LPRECT, DWORD, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL InvalidateRect(HWND, RECT const *, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL ValidateRect(HWND, RECT const *) { return(WIN32_STUB(FALSE)); }
HDC BeginPaint(HWND, LPPAINTSTRUCT) { return(WIN32_STUB((HDC)nullptr)); }
BOOL EndPaint(HWND, PAINTSTRUCT const *) { return(WIN32_STUB(FALSE)); }
HDC GetDC(HWND) { return(WIN32_STUB((HDC)nullptr)); }
int ReleaseDC(HWND, HDC) { return(WIN32_STUB(0)); }
int FillRect(HDC, RECT const *, HBRUSH) { return(WIN32_STUB(0)); }
LONG GetWindowLongA(HWND, int) { return(WIN32_STUB(0)); }
LONG SetWindowLongA(HWND, int, LONG) { return(WIN32_STUB(0)); }
BOOL SetWindowTextA(HWND, LPCSTR) { return(WIN32_STUB(FALSE)); }
int GetWindowTextA(HWND, LPSTR text, int count) { if (text != nullptr && count > 0) text[0] = '\0'; return(WIN32_STUB(0)); }
BOOL EnableWindow(HWND, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL IsWindow(HWND) { return(WIN32_STUB(FALSE)); }
BOOL IsWindowVisible(HWND) { return(WIN32_STUB(FALSE)); }
BOOL IsIconic(HWND) { return(WIN32_STUB(FALSE)); }
HWND SetFocus(HWND) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetFocus(void) { return(WIN32_STUB((HWND)nullptr)); }
HWND SetCapture(HWND) { return(WIN32_STUB((HWND)nullptr)); }
BOOL ReleaseCapture(void) { return(WIN32_STUB(FALSE)); }
HWND GetActiveWindow(void) { return(WIN32_STUB((HWND)nullptr)); }
HWND SetActiveWindow(HWND) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetForegroundWindow(void) { return(WIN32_STUB((HWND)nullptr)); }
BOOL SetForegroundWindow(HWND) { return(WIN32_STUB(FALSE)); }
BOOL BringWindowToTop(HWND) { return(WIN32_STUB(FALSE)); }
HWND GetDesktopWindow(void) { return(WIN32_STUB((HWND)nullptr)); }
HWND FindWindowA(LPCSTR, LPCSTR) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetParent(HWND) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetDlgItem(HWND, int) { return(WIN32_STUB((HWND)nullptr)); }
LRESULT SendDlgItemMessageA(HWND, int, UINT, WPARAM, LPARAM) { return(WIN32_STUB(0)); }
UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC) { return(WIN32_STUB(0)); }
BOOL KillTimer(HWND, UINT_PTR) { return(WIN32_STUB(FALSE)); }
int GetSystemMetrics(int) { return(WIN32_STUB(0)); }
HCURSOR LoadCursorA(HINSTANCE, LPCSTR) { return(WIN32_STUB((HCURSOR)nullptr)); }
HICON LoadIconA(HINSTANCE, LPCSTR) { return(WIN32_STUB((HICON)nullptr)); }
HCURSOR SetCursor(HCURSOR) { return(WIN32_STUB((HCURSOR)nullptr)); }
int ShowCursor(BOOL) { return(WIN32_STUB(0)); }
BOOL GetCursorPos(LPPOINT) { return(WIN32_STUB(FALSE)); }
BOOL SetCursorPos(int, int) { return(WIN32_STUB(FALSE)); }
BOOL ClipCursor(RECT const *) { return(WIN32_STUB(FALSE)); }
SHORT GetKeyState(int) { return(WIN32_STUB(0)); }
SHORT GetAsyncKeyState(int) { return(WIN32_STUB(0)); }
BOOL GetKeyboardState(PBYTE) { return(WIN32_STUB(FALSE)); }
UINT MapVirtualKeyA(UINT, UINT) { return(WIN32_STUB(0)); }
int MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int) { return(WIN32_STUB(0)); }
int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL) { return(WIN32_STUB(0)); }


/*
** A message box is the one place the engine tells the player something and then waits.
** There is nothing to wait on and nothing to show, so the text goes to the log and the
** call reports the cancelled answer rather than pretending the player pressed OK.
*/
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
	fprintf(stderr, "OpenTS message box [%s]: %s\n",
		caption != nullptr ? caption : "", text != nullptr ? text : "");
	fflush(stderr);
	return(WIN32_STUB(IDCANCEL));
}


LONG RegOpenKeyExA(HKEY, LPCSTR, DWORD, DWORD, PHKEY result) { if (result != nullptr) *result = nullptr; return(WIN32_STUB(ERROR_FILE_NOT_FOUND)); }
LONG RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, PHKEY result, LPDWORD) { if (result != nullptr) *result = nullptr; return(WIN32_STUB(ERROR_ACCESS_DENIED)); }
LONG RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return(WIN32_STUB(ERROR_FILE_NOT_FOUND)); }
LONG RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, BYTE const *, DWORD) { return(WIN32_STUB(ERROR_ACCESS_DENIED)); }
LONG RegDeleteValueA(HKEY, LPCSTR) { return(WIN32_STUB(ERROR_FILE_NOT_FOUND)); }
LONG RegCloseKey(HKEY) { return(WIN32_STUB(ERROR_INVALID_HANDLE)); }

UINT GetPrivateProfileIntA(LPCSTR, LPCSTR, INT defaultvalue, LPCSTR) { return(WIN32_STUB((UINT)defaultvalue)); }
DWORD GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR defaultvalue, LPSTR returned, DWORD size, LPCSTR)
{
	if (returned != nullptr && size > 0) {
		strncpy(returned, defaultvalue != nullptr ? defaultvalue : "", size - 1);
		returned[size - 1] = '\0';
		return(WIN32_STUB((DWORD)strlen(returned)));
	}
	return(WIN32_STUB(0));
}
BOOL WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR) { return(WIN32_STUB(FALSE)); }

HRESULT CoInitialize(LPVOID) { return(WIN32_STUB(E_NOTIMPL)); }
void CoUninitialize(void) { WIN32_STUB_VOID(); }
HRESULT CoCreateInstance(REFCLSID, IUnknown *, DWORD, REFIID, LPVOID * object) { if (object != nullptr) *object = nullptr; return(WIN32_STUB(E_NOTIMPL)); }
LPVOID CoTaskMemAlloc(SIZE_T size) { return(malloc(size)); }
void CoTaskMemFree(LPVOID block) { free(block); }
HRESULT CreateStreamOnHGlobal(HGLOBAL, BOOL, LPSTREAM * stream) { if (stream != nullptr) *stream = nullptr; return(WIN32_STUB(E_NOTIMPL)); }
HRESULT CLSIDFromString(LPCOLESTR, LPCLSID classid) { if (classid != nullptr) *classid = GUID_NULL; return(WIN32_STUB(E_NOTIMPL)); }

HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int) { return(WIN32_STUB((HINSTANCE)nullptr)); }


/*
** The MSVC C runtime routines crtcompat.h declares out of line. These are ordinary
** library code with defined behavior, not stubs.
*/

static char * Convert_Unsigned(unsigned long value, char * buffer, int radix, bool negative)
{
	char digits[sizeof(unsigned long) * 8 + 1];
	int count = 0;

	if (radix < 2 || radix > 36) {
		buffer[0] = '\0';
		return(buffer);
	}

	do {
		unsigned long digit = value % (unsigned long)radix;
		digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
		value /= (unsigned long)radix;
	} while (value != 0);

	char * out = buffer;
	if (negative) *out++ = '-';
	while (count > 0) *out++ = digits[--count];
	*out = '\0';
	return(buffer);
}


int _getch(void)
{
	return(WIN32_STUB(-1));
}


char * itoa(int value, char * buffer, int radix)
{
	bool negative = (radix == 10 && value < 0);
	unsigned long magnitude = negative ? (unsigned long)(-(long)value) : (unsigned long)(unsigned int)value;

	return(Convert_Unsigned(magnitude, buffer, radix, negative));
}


char * ltoa(long value, char * buffer, int radix)
{
	bool negative = (radix == 10 && value < 0);
	unsigned long magnitude = negative ? (unsigned long)(-value) : (unsigned long)value;

	return(Convert_Unsigned(magnitude, buffer, radix, negative));
}


char * ultoa(unsigned long value, char * buffer, int radix)
{
	return(Convert_Unsigned(value, buffer, radix, false));
}


static void Copy_Component(char * destination, char const * start, char const * end)
{
	if (destination == nullptr) return;

	while (start < end) *destination++ = *start++;
	*destination = '\0';
}


void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext)
{
	char const * cursor = path;
	char const * drive_end = cursor;

	if (path[0] != '\0' && path[1] == ':') drive_end = path + 2;
	Copy_Component(drive, path, drive_end);

	char const * dir_end = drive_end;
	for (cursor = drive_end; *cursor != '\0'; cursor++) {
		if (*cursor == '\\' || *cursor == '/') dir_end = cursor + 1;
	}
	Copy_Component(dir, drive_end, dir_end);

	char const * ext_start = cursor;
	for (char const * scan = dir_end; *scan != '\0'; scan++) {
		if (*scan == '.') ext_start = scan;
	}
	Copy_Component(fname, dir_end, ext_start);
	Copy_Component(ext, ext_start, cursor);
}


void _makepath(char * path, char const * drive, char const * dir, char const * fname, char const * ext)
{
	char * out = path;

	if (drive != nullptr && drive[0] != '\0') {
		*out++ = drive[0];
		*out++ = ':';
	}

	if (dir != nullptr && dir[0] != '\0') {
		while (*dir != '\0') *out++ = *dir++;
		if (out[-1] != '\\' && out[-1] != '/') *out++ = '\\';
	}

	if (fname != nullptr) {
		while (*fname != '\0') *out++ = *fname++;
	}

	if (ext != nullptr && ext[0] != '\0') {
		if (ext[0] != '.') *out++ = '.';
		while (*ext != '\0') *out++ = *ext++;
	}

	*out = '\0';
}


/*
** GDI, the window manager's remainder, resources, the console, OLE, and the multimedia
** timer. All stubs.
*/
HDC CreateCompatibleDC(HDC) { return(WIN32_STUB((HDC)nullptr)); }
BOOL DeleteDC(HDC) { return(WIN32_STUB(FALSE)); }
HBITMAP CreateDIBSection(HDC, BITMAPINFO const *, UINT, void ** bits, HANDLE, DWORD) { if (bits != nullptr) *bits = nullptr; return(WIN32_STUB((HBITMAP)nullptr)); }
HGDIOBJ SelectObject(HDC, HGDIOBJ) { return(WIN32_STUB((HGDIOBJ)nullptr)); }
BOOL DeleteObject(HGDIOBJ) { return(WIN32_STUB(FALSE)); }
int GetObjectA(HGDIOBJ, int, LPVOID) { return(WIN32_STUB(0)); }
BOOL GdiFlush(void) { return(WIN32_STUB(FALSE)); }
int SetStretchBltMode(HDC, int) { return(WIN32_STUB(0)); }
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return(WIN32_STUB(FALSE)); }
int StretchDIBits(HDC, int, int, int, int, int, int, int, int, void const *, BITMAPINFO const *, UINT, DWORD) { return(WIN32_STUB(0)); }
COLORREF SetTextColor(HDC, COLORREF) { return(WIN32_STUB((COLORREF)0xFFFFFFFF)); }
COLORREF SetBkColor(HDC, COLORREF) { return(WIN32_STUB((COLORREF)0xFFFFFFFF)); }
int SetBkMode(HDC, int) { return(WIN32_STUB(0)); }
HGDIOBJ GetStockObject(int) { return(WIN32_STUB((HGDIOBJ)nullptr)); }

HWND GetTopWindow(HWND) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetWindow(HWND, UINT) { return(WIN32_STUB((HWND)nullptr)); }
BOOL IsWindowEnabled(HWND) { return(WIN32_STUB(FALSE)); }
BOOL CloseWindow(HWND) { return(WIN32_STUB(FALSE)); }
int MapWindowPoints(HWND, HWND, LPPOINT, UINT) { return(WIN32_STUB(0)); }
BOOL RedrawWindow(HWND, RECT const *, HRGN, UINT) { return(WIN32_STUB(FALSE)); }
HMONITOR MonitorFromWindow(HWND, DWORD) { return(WIN32_STUB((HMONITOR)nullptr)); }
BOOL GetMonitorInfoA(HMONITOR, LPMONITORINFO) { return(WIN32_STUB(FALSE)); }
BOOL SetDlgItemTextA(HWND, int, LPCSTR) { return(WIN32_STUB(FALSE)); }
UINT GetDlgItemTextA(HWND, int, LPSTR text, int count) { if (text != nullptr && count > 0) text[0] = '\0'; return(WIN32_STUB(0)); }
BOOL EndDialog(HWND, INT_PTR) { return(WIN32_STUB(FALSE)); }
BOOL IsDialogMessageA(HWND, LPMSG) { return(WIN32_STUB(FALSE)); }
int TranslateAcceleratorA(HWND, HACCEL, LPMSG) { return(WIN32_STUB(0)); }
int ToAscii(UINT, UINT, BYTE const *, LPWORD, UINT) { return(WIN32_STUB(0)); }
BOOL CharToOemBuffA(LPCSTR, LPSTR, DWORD) { return(WIN32_STUB(FALSE)); }
HLOCAL LocalFree(HLOCAL memory) { free(memory); return(nullptr); }

int LoadStringA(HINSTANCE, UINT, LPSTR buffer, int size) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
HRSRC FindResourceA(HMODULE, LPCSTR, LPCSTR) { return(WIN32_STUB((HRSRC)nullptr)); }
HGLOBAL LoadResource(HMODULE, HRSRC) { return(WIN32_STUB((HGLOBAL)nullptr)); }
LPVOID LockResource(HGLOBAL) { return(WIN32_STUB((LPVOID)nullptr)); }
DWORD SizeofResource(HMODULE, HRSRC) { return(WIN32_STUB(0)); }
DWORD GetFileVersionInfoSizeA(LPCSTR, LPDWORD handle) { if (handle != nullptr) *handle = 0; return(WIN32_STUB(0)); }
BOOL GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID) { return(WIN32_STUB(FALSE)); }
BOOL VerQueryValueA(LPCVOID, LPCSTR, LPVOID * buffer, PUINT length) { if (buffer != nullptr) *buffer = nullptr; if (length != nullptr) *length = 0; return(WIN32_STUB(FALSE)); }

BOOL AllocConsole(void) { return(WIN32_STUB(FALSE)); }
BOOL FreeConsole(void) { return(WIN32_STUB(FALSE)); }
BOOL SetConsoleTitleA(LPCSTR) { return(WIN32_STUB(FALSE)); }
HANDLE GetStdHandle(DWORD) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL SetConsoleMode(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL GetConsoleMode(HANDLE, LPDWORD) { return(WIN32_STUB(FALSE)); }
BOOL TerminateProcess(HANDLE, UINT code) { exit((int)code); }
void RaiseException(DWORD, DWORD, DWORD, ULONG_PTR const *) { WIN32_STUB_ABORT(); }
LPWSTR * CommandLineToArgvW(LPCWSTR, int * count) { if (count != nullptr) *count = 0; return(WIN32_STUB((LPWSTR *)nullptr)); }

void AcquireSRWLockExclusive(PSRWLOCK) { WIN32_STUB_VOID(); }
void ReleaseSRWLockExclusive(PSRWLOCK) { WIN32_STUB_VOID(); }
void AcquireSRWLockShared(PSRWLOCK) { WIN32_STUB_VOID(); }
void ReleaseSRWLockShared(PSRWLOCK) { WIN32_STUB_VOID(); }
void InitializeSRWLock(PSRWLOCK lock) { if (lock != nullptr) lock->Ptr = nullptr; }

BSTR SysAllocString(OLECHAR const *) { return(WIN32_STUB((BSTR)nullptr)); }
void SysFreeString(BSTR) { WIN32_STUB_VOID(); }
UINT SysStringLen(BSTR) { return(WIN32_STUB(0)); }
HRESULT StringFromCLSID(REFCLSID, LPOLESTR * string) { if (string != nullptr) *string = nullptr; return(WIN32_STUB(E_NOTIMPL)); }
HRESULT OleSaveToStream(IPersistStream *, IStream *) { return(WIN32_STUB(E_NOTIMPL)); }
HRESULT OleLoadFromStream(IStream *, REFIID, void ** object) { if (object != nullptr) *object = nullptr; return(WIN32_STUB(E_NOTIMPL)); }

MMRESULT timeGetDevCaps(LPTIMECAPS, UINT) { return(WIN32_STUB(TIMERR_NOCANDO)); }
MMRESULT timeSetEvent(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT) { return(WIN32_STUB(0)); }
MMRESULT timeKillEvent(UINT) { return(WIN32_STUB(TIMERR_NOCANDO)); }

BOOL InitCommonControls(void) { return(WIN32_STUB(FALSE)); }


/*
** comdef.h turns a failed HRESULT into a thrown _com_error. Nothing here catches one, so
** the failure is reported and the process stops rather than unwinding into code that has
** no handler.
*/
void _com_issue_error(HRESULT result)
{
	fprintf(stderr, "OpenTS: COM call failed with HRESULT 0x%08lx and there is no COM runtime "
		"to report it through.\n", (unsigned long)result);
	fflush(stderr);
	abort();
}


BOOL DeviceIoControl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD returned, LPOVERLAPPED) { if (returned != nullptr) *returned = 0; return(WIN32_STUB(FALSE)); }
HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL Module32First(HANDLE, LPMODULEENTRY32) { return(WIN32_STUB(FALSE)); }
BOOL Module32Next(HANDLE, LPMODULEENTRY32) { return(WIN32_STUB(FALSE)); }


HWND CreateDialogParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM) { return(WIN32_STUB((HWND)nullptr)); }
HWND CreateDialogIndirectParamA(HINSTANCE, LPCDLGTEMPLATE, HWND, DLGPROC, LPARAM) { return(WIN32_STUB((HWND)nullptr)); }
INT_PTR DialogBoxParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM) { return(WIN32_STUB(-1)); }
BOOL EnumChildWindows(HWND, WNDENUMPROC, LPARAM) { return(WIN32_STUB(FALSE)); }
int GetClassNameA(HWND, LPSTR classname, int count) { if (classname != nullptr && count > 0) classname[0] = '\0'; return(WIN32_STUB(0)); }
HWND ChildWindowFromPoint(HWND, POINT) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetCapture(void) { return(WIN32_STUB((HWND)nullptr)); }
BOOL IsChild(HWND, HWND) { return(WIN32_STUB(FALSE)); }
HMENU GetMenu(HWND) { return(WIN32_STUB((HMENU)nullptr)); }
HMENU GetSystemMenu(HWND, BOOL) { return(WIN32_STUB((HMENU)nullptr)); }
BOOL DeleteMenu(HMENU, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL EnableMenuItem(HMENU, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL DestroyCursor(HCURSOR) { return(WIN32_STUB(FALSE)); }
HBRUSH CreateSolidBrush(COLORREF) { return(WIN32_STUB((HBRUSH)nullptr)); }
int SaveDC(HDC) { return(WIN32_STUB(0)); }
BOOL RestoreDC(HDC, int) { return(WIN32_STUB(FALSE)); }
int SetGraphicsMode(HDC, int) { return(WIN32_STUB(0)); }
int GetDeviceCaps(HDC, int) { return(WIN32_STUB(0)); }
HFONT CreateFontIndirectA(LOGFONTA const *) { return(WIN32_STUB((HFONT)nullptr)); }
BOOL GetTextMetricsA(HDC, LPTEXTMETRICA) { return(WIN32_STUB(FALSE)); }
BOOL TextOutA(HDC, int, int, LPCSTR, int) { return(WIN32_STUB(FALSE)); }
BOOL GetScrollInfo(HWND, int, LPSCROLLINFO) { return(WIN32_STUB(FALSE)); }
int SetScrollInfo(HWND, int, LPCSCROLLINFO, BOOL) { return(WIN32_STUB(0)); }
BOOL GetFileInformationByHandle(HANDLE, LPBY_HANDLE_FILE_INFORMATION) { return(WIN32_STUB(FALSE)); }
BOOL FileTimeToDosDateTime(FILETIME const *, LPWORD, LPWORD) { return(WIN32_STUB(FALSE)); }
BOOL DosDateTimeToFileTime(WORD, WORD, LPFILETIME) { return(WIN32_STUB(FALSE)); }
int GetTimeFormatA(LCID, DWORD, SYSTEMTIME const *, LPCSTR, LPSTR text, int count) { if (text != nullptr && count > 0) text[0] = '\0'; return(WIN32_STUB(0)); }
int GetDateFormatA(LCID, DWORD, SYSTEMTIME const *, LPCSTR, LPSTR text, int count) { if (text != nullptr && count > 0) text[0] = '\0'; return(WIN32_STUB(0)); }
BOOL SetStdHandle(DWORD, HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL GetConsoleScreenBufferInfo(HANDLE, PCONSOLE_SCREEN_BUFFER_INFO) { return(WIN32_STUB(FALSE)); }
HWND GetConsoleWindow(void) { return(WIN32_STUB((HWND)nullptr)); }
BOOL IsDebuggerPresent(void) { return(FALSE); }
BOOL WriteConsoleA(HANDLE, void const *, DWORD, LPDWORD written, LPVOID) { if (written != nullptr) *written = 0; return(WIN32_STUB(FALSE)); }
HIMAGELIST ImageList_Create(int, int, UINT, int, int) { return(WIN32_STUB((HIMAGELIST)nullptr)); }
BOOL ImageList_Destroy(HIMAGELIST) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_BeginDrag(HIMAGELIST, int, int, int) { return(WIN32_STUB(FALSE)); }
void ImageList_EndDrag(void) { WIN32_STUB_VOID(); }
BOOL ImageList_DragEnter(HWND, int, int) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_DragLeave(HWND) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_DragMove(int, int) { return(WIN32_STUB(FALSE)); }
BOOL ImageList_DragShowNolock(BOOL) { return(WIN32_STUB(FALSE)); }

extern "C" const IID IID_IPropertyStorage = {0x00000138, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IPropertySetStorage = {0x0000013A, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

void PropVariantInit(PROPVARIANT * value) { if (value != nullptr) memset(value, 0, sizeof(*value)); }
HRESULT PropVariantClear(PROPVARIANT * value) { if (value != nullptr) memset(value, 0, sizeof(*value)); return(S_OK); }

int WSACancelAsyncRequest(HANDLE) { return(WIN32_STUB(-1)); }


DWORD FormatMessageA(DWORD, LPCVOID, DWORD, DWORD, LPSTR buffer, DWORD size, va_list *) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
BOOL SetConsoleScreenBufferSize(HANDLE, COORD) { return(WIN32_STUB(FALSE)); }
PTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(PTOP_LEVEL_EXCEPTION_FILTER) { return(WIN32_STUB((PTOP_LEVEL_EXCEPTION_FILTER)nullptr)); }

BOOL CheckDlgButton(HWND, int, UINT) { return(WIN32_STUB(FALSE)); }
UINT IsDlgButtonChecked(HWND, int) { return(WIN32_STUB(0)); }
int GetDlgCtrlID(HWND) { return(WIN32_STUB(0)); }
BOOL GetUpdateRect(HWND, LPRECT rect, BOOL) { if (rect != nullptr) SetRectEmpty(rect); return(WIN32_STUB(FALSE)); }
int GetBkMode(HDC) { return(WIN32_STUB(0)); }
COLORREF GetBkColor(HDC) { return(WIN32_STUB((COLORREF)0xFFFFFFFF)); }
COLORREF GetTextColor(HDC) { return(WIN32_STUB((COLORREF)0xFFFFFFFF)); }
UINT SetTextAlign(HDC, UINT) { return(WIN32_STUB((UINT)0xFFFFFFFF)); }
LRESULT CallWindowProcA(WNDPROC, HWND, UINT, WPARAM, LPARAM) { return(WIN32_STUB(0)); }
HWND WindowFromPoint(POINT) { return(WIN32_STUB((HWND)nullptr)); }
BOOL RegisterHotKey(HWND, int, UINT, UINT) { return(WIN32_STUB(FALSE)); }
BOOL UnregisterHotKey(HWND, int) { return(WIN32_STUB(FALSE)); }
DWORD GetWindowContextHelpId(HWND) { return(WIN32_STUB(0)); }
BOOL WinHelpA(HWND, LPCSTR, UINT, ULONG_PTR) { return(WIN32_STUB(FALSE)); }
BOOL SetViewportOrgEx(HDC, int, int, LPPOINT) { return(WIN32_STUB(FALSE)); }
BOOL SetWindowOrgEx(HDC, int, int, LPPOINT) { return(WIN32_STUB(FALSE)); }
BOOL DPtoLP(HDC, LPPOINT, int) { return(WIN32_STUB(FALSE)); }
BOOL LPtoDP(HDC, LPPOINT, int) { return(WIN32_STUB(FALSE)); }
HBITMAP CreateBitmap(int, int, UINT, UINT, void const *) { return(WIN32_STUB((HBITMAP)nullptr)); }
HICON CreateIconIndirect(PICONINFO) { return(WIN32_STUB((HICON)nullptr)); }
BOOL EnumDisplaySettingsA(LPCSTR, DWORD, LPDEVMODEA) { return(WIN32_STUB(FALSE)); }

extern "C" const FMTID FMTID_SummaryInformation = {0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}};
extern "C" const IID IID_IStorage = {0x0000000B, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

HRESULT StgCreateDocfile(OLECHAR const *, DWORD, DWORD, IStorage ** storage) { if (storage != nullptr) *storage = nullptr; return(WIN32_STUB(E_NOTIMPL)); }
HRESULT StgOpenStorage(OLECHAR const *, IStorage *, DWORD, void *, DWORD, IStorage ** storage) { if (storage != nullptr) *storage = nullptr; return(WIN32_STUB(E_NOTIMPL)); }
HRESULT StgIsStorageFile(OLECHAR const *) { return(WIN32_STUB(S_FALSE)); }
HRESULT CoFileTimeNow(FILETIME * filetime) { if (filetime != nullptr) { filetime->dwLowDateTime = 0; filetime->dwHighDateTime = 0; } return(WIN32_STUB(E_NOTIMPL)); }
HRESULT CoRegisterClassObject(REFCLSID, IUnknown *, DWORD, DWORD, LPDWORD registration) { if (registration != nullptr) *registration = 0; return(WIN32_STUB(E_NOTIMPL)); }
HRESULT CoRevokeClassObject(DWORD) { return(WIN32_STUB(E_NOTIMPL)); }


DWORD GetAdaptersInfo(PIP_ADAPTER_INFO, PULONG size) { if (size != nullptr) *size = 0; return(WIN32_STUB(ERROR_BUFFER_OVERFLOW)); }


HANDLE OpenMutexA(DWORD, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }
HRESULT OleInitialize(LPVOID) { return(WIN32_STUB(E_NOTIMPL)); }
void OleUninitialize(void) { WIN32_STUB_VOID(); }
BOOL GetTextExtentPoint32A(HDC, LPCSTR, int, LPSIZE size) { if (size != nullptr) { size->cx = 0; size->cy = 0; } return(WIN32_STUB(FALSE)); }
HFONT CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR) { return(WIN32_STUB((HFONT)nullptr)); }
BOOL ModifyWorldTransform(HDC, void const *, DWORD) { return(WIN32_STUB(FALSE)); }


BOOL SymInitialize(HANDLE, LPCSTR, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL SymCleanup(HANDLE) { return(WIN32_STUB(FALSE)); }
DWORD SymSetOptions(DWORD) { return(WIN32_STUB(0)); }
BOOL SymFromAddr(HANDLE, DWORD64, DWORD64 *, PSYMBOL_INFO) { return(WIN32_STUB(FALSE)); }
BOOL SymGetLineFromAddr64(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64) { return(WIN32_STUB(FALSE)); }


int MessageBoxIndirectA(MSGBOXPARAMSA const *) { return(WIN32_STUB(IDCANCEL)); }


HWND GetNextDlgTabItem(HWND, HWND, BOOL) { return(WIN32_STUB((HWND)nullptr)); }
HWND GetNextDlgGroupItem(HWND, HWND, BOOL) { return(WIN32_STUB((HWND)nullptr)); }


int GetKeyNameTextA(LONG, LPSTR buffer, int size) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
int DrawTextA(HDC, LPCSTR, int, LPRECT, UINT) { return(WIN32_STUB(0)); }


/*
** The Windows-only half of Winsock. Everything socket-shaped comes from the host; these
** are the calls that have no BSD counterpart.
*/
#include "winsockcompat.h"

int WSAStartup(WORD, LPWSADATA data)
{
	if (data != nullptr) memset(data, 0, sizeof(*data));
	return(WIN32_STUB(WSASYSNOTREADY));
}


int WSACleanup(void) { return(WIN32_STUB(SOCKET_ERROR)); }
int WSAGetLastError(void) { return(errno != 0 ? WSABASEERR + errno : 0); }
void WSASetLastError(int error) { errno = error > WSABASEERR ? error - WSABASEERR : error; }
int WSAAsyncSelect(SOCKET, HWND, unsigned int, long) { return(WIN32_STUB(SOCKET_ERROR)); }
int ioctlsocket(SOCKET, long, unsigned long *) { return(WIN32_STUB(SOCKET_ERROR)); }


#endif	// __EMSCRIPTEN__
