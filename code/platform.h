/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

/*
 * The operating system surface the portable code builds on. On Windows this is <windows.h>
 * itself. Elsewhere it is the small slice of that surface the ported code still calls -- the
 * file attribute, directory scan, time, path, and GUID pieces -- implemented over POSIX in
 * platform_posix.cpp. The slice shrinks as subsystems are replaced outright; docs/PORTING.md
 * records the plan.
 */

#ifdef _WIN32

#include <windows.h>

#define PATH_SEP_CHAR '\\'
#define PATH_SEP_STR "\\"

#else

#include <cerrno>
#include <cstddef>
#include <cstdint>

#define PATH_SEP_CHAR '/'
#define PATH_SEP_STR "/"

typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef int32_t LONG;
typedef int BOOL;
typedef unsigned int UINT;
typedef void * HANDLE;
typedef void * HMODULE;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

/*
 * POSIX paths have no useful fixed bound, so the buffers sized by these are simply generous
 * rather than faithful to the Windows values.
 */
#define MAX_PATH 1024
#define _MAX_PATH MAX_PATH
#define _MAX_DRIVE 8
#define _MAX_DIR MAX_PATH
#define _MAX_FNAME 256
#define _MAX_EXT 256

/*
 * The last-error channel is errno, so an error set by a shimmed call reads back through the
 * same pair the Windows code uses.
 */
#define ERROR_ALREADY_EXISTS EEXIST
inline DWORD GetLastError(void) { return((DWORD)errno); }
inline void SetLastError(DWORD error) { errno = (int)error; }

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME;

typedef union _ULARGE_INTEGER {
	struct {
		DWORD LowPart;
		DWORD HighPart;
	};
	unsigned long long QuadPart;
} ULARGE_INTEGER;

typedef union _LARGE_INTEGER {
	struct {
		DWORD LowPart;
		LONG HighPart;
	};
	long long QuadPart;
} LARGE_INTEGER;

typedef struct _SYSTEMTIME {
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
} SYSTEMTIME;

void GetSystemTime(SYSTEMTIME * time);
void GetLocalTime(SYSTEMTIME * time);
BOOL SystemTimeToFileTime(SYSTEMTIME const * time, FILETIME * filetime);

/*
 * The performance counter runs in nanoseconds, so the reported frequency is fixed.
 */
BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency);
BOOL QueryPerformanceCounter(LARGE_INTEGER * counter);

#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeLow;
	char cFileName[MAX_PATH];
} WIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;

/*
 * The scan matches the way the Windows one does: the pattern's directory part picks the
 * folder, the name part is a case-insensitive wildcard, and a dotfile reports itself hidden.
 */
HANDLE FindFirstFile(char const * pattern, WIN32_FIND_DATAA * data);
BOOL FindNextFile(HANDLE handle, WIN32_FIND_DATAA * data);
BOOL FindClose(HANDLE handle);

DWORD GetFileAttributes(char const * path);
BOOL CreateDirectory(char const * path, void * security);
BOOL DeleteFile(char const * path);
BOOL SetCurrentDirectory(char const * path);
DWORD GetCurrentDirectory(DWORD size, char * buffer);
DWORD GetTempPath(DWORD size, char * buffer);
DWORD GetCurrentProcessId(void);

#define GetFileAttributesA GetFileAttributes
#define CreateDirectoryA CreateDirectory
#define DeleteFileA DeleteFile

void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext);

typedef struct _GUID {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
} GUID;
typedef GUID CLSID;
typedef GUID IID;

inline bool operator == (GUID const & left, GUID const & right)
{
	return(__builtin_memcmp(&left, &right, sizeof(GUID)) == 0);
}

inline bool operator != (GUID const & left, GUID const & right)
{
	return(!(left == right));
}

/*
 * The brace and hyphen form CLSIDFromString and StringFromCLSID exchange, which is also the
 * form game data writes class identifiers in.
 */
bool Parse_GUID_Text(char const * text, GUID & guid);
void Compose_GUID_Text(GUID const & guid, char * buffer, size_t size);

/*
 * POSIX-only helpers with no Windows namesake.
 */
bool Platform_Executable_Path(char * buffer, size_t size);
char const * const * Platform_Command_Line_Arguments(int * argc);

#endif
