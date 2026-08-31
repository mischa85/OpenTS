/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#ifndef _WIN32

#include "platform.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

/*
 * FILETIME counts 100-nanosecond ticks since 1601-01-01; the offset converts from the POSIX
 * epoch.
 */
static unsigned long long const FILETIME_PER_SECOND = 10000000ULL;
static unsigned long long const FILETIME_UNIX_EPOCH = 116444736000000000ULL;


static void Fill_System_Time(SYSTEMTIME * time, struct tm const & fields, unsigned milliseconds)
{
	time->wYear = (WORD)(fields.tm_year + 1900);
	time->wMonth = (WORD)(fields.tm_mon + 1);
	time->wDayOfWeek = (WORD)fields.tm_wday;
	time->wDay = (WORD)fields.tm_mday;
	time->wHour = (WORD)fields.tm_hour;
	time->wMinute = (WORD)fields.tm_min;
	time->wSecond = (WORD)fields.tm_sec;
	time->wMilliseconds = (WORD)milliseconds;
}


void GetSystemTime(SYSTEMTIME * time)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm fields;
	time_t const seconds = now.tv_sec;
	gmtime_r(&seconds, &fields);

	Fill_System_Time(time, fields, (unsigned)(now.tv_usec / 1000));
}


void GetLocalTime(SYSTEMTIME * time)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm fields;
	time_t const seconds = now.tv_sec;
	localtime_r(&seconds, &fields);

	Fill_System_Time(time, fields, (unsigned)(now.tv_usec / 1000));
}


BOOL SystemTimeToFileTime(SYSTEMTIME const * time, FILETIME * filetime)
{
	struct tm fields;
	memset(&fields, 0, sizeof(fields));
	fields.tm_year = time->wYear - 1900;
	fields.tm_mon = time->wMonth - 1;
	fields.tm_mday = time->wDay;
	fields.tm_hour = time->wHour;
	fields.tm_min = time->wMinute;
	fields.tm_sec = time->wSecond;

	time_t const seconds = timegm(&fields);
	if (seconds == (time_t)-1) {
		return(FALSE);
	}

	unsigned long long const ticks = (unsigned long long)seconds * FILETIME_PER_SECOND
												+ FILETIME_UNIX_EPOCH
												+ (unsigned long long)time->wMilliseconds * 10000ULL;
	filetime->dwLowDateTime = (DWORD)ticks;
	filetime->dwHighDateTime = (DWORD)(ticks >> 32);
	return(TRUE);
}


BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency)
{
	frequency->QuadPart = 1000000000LL;
	return(TRUE);
}


BOOL QueryPerformanceCounter(LARGE_INTEGER * counter)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return(FALSE);
	}

	counter->QuadPart = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
	return(TRUE);
}


static FILETIME File_Time_From_Unix(time_t seconds)
{
	unsigned long long const ticks = (unsigned long long)seconds * FILETIME_PER_SECOND
												+ FILETIME_UNIX_EPOCH;
	FILETIME filetime;
	filetime.dwLowDateTime = (DWORD)ticks;
	filetime.dwHighDateTime = (DWORD)(ticks >> 32);
	return(filetime);
}


/// <summary>
/// Matches a file name against a wildcard, the way the Windows scans being stood in for did:
/// case-insensitively, with '*' spanning any run and '?' any one character.
/// </summary>
static bool Wildcard_Match(char const * pattern, char const * name)
{
	if (*pattern == '\0') {
		return(*name == '\0');
	}

	if (*pattern == '*') {
		for (char const * rest = name; ; rest++) {
			if (Wildcard_Match(pattern + 1, rest)) {
				return(true);
			}
			if (*rest == '\0') {
				return(false);
			}
		}
	}

	if (*name == '\0') {
		return(false);
	}

	if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*name)) {
		return(Wildcard_Match(pattern + 1, name + 1));
	}

	return(false);
}


static DWORD Attributes_Of(char const * path, char const * name)
{
	struct stat status;
	if (stat(path, &status) != 0) {
		return(INVALID_FILE_ATTRIBUTES);
	}

	DWORD attributes = 0;
	if (S_ISDIR(status.st_mode)) {
		attributes |= FILE_ATTRIBUTE_DIRECTORY;
	}
	if (name != NULL && name[0] == '.') {
		attributes |= FILE_ATTRIBUTE_HIDDEN;
	}
	if (attributes == 0) {
		attributes = FILE_ATTRIBUTE_NORMAL;
	}
	return(attributes);
}


struct FindState {
	DIR * Directory;
	std::string Folder;
	std::string Pattern;
};


static bool Find_Step(FindState * state, WIN32_FIND_DATAA * data)
{
	for (;;) {
		struct dirent const * entry = readdir(state->Directory);
		if (entry == NULL) {
			return(false);
		}

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		if (!Wildcard_Match(state->Pattern.c_str(), entry->d_name)) {
			continue;
		}

		std::string const path = state->Folder + entry->d_name;

		struct stat status;
		if (stat(path.c_str(), &status) != 0) {
			continue;
		}

		data->dwFileAttributes = Attributes_Of(path.c_str(), entry->d_name);
		data->ftLastWriteTime = File_Time_From_Unix(status.st_mtime);
		data->nFileSizeLow = (DWORD)status.st_size;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", entry->d_name);
		return(true);
	}
}


HANDLE FindFirstFile(char const * pattern, WIN32_FIND_DATAA * data)
{
	if (pattern == NULL || data == NULL) {
		return(INVALID_HANDLE_VALUE);
	}

	std::string folder = "./";
	std::string name = pattern;

	char const * split = strrchr(pattern, '/');
	if (split != NULL) {
		folder.assign(pattern, split - pattern + 1);
		name = split + 1;
	}

	DIR * directory = opendir(folder.c_str());
	if (directory == NULL) {
		return(INVALID_HANDLE_VALUE);
	}

	FindState * state = new FindState;
	state->Directory = directory;
	state->Folder = folder;
	state->Pattern = name;

	if (!Find_Step(state, data)) {
		closedir(state->Directory);
		delete state;
		return(INVALID_HANDLE_VALUE);
	}

	return((HANDLE)state);
}


BOOL FindNextFile(HANDLE handle, WIN32_FIND_DATAA * data)
{
	if (handle == INVALID_HANDLE_VALUE || handle == NULL || data == NULL) {
		return(FALSE);
	}

	return(Find_Step((FindState *)handle, data) ? TRUE : FALSE);
}


BOOL FindClose(HANDLE handle)
{
	if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
		return(FALSE);
	}

	FindState * state = (FindState *)handle;
	closedir(state->Directory);
	delete state;
	return(TRUE);
}


DWORD GetFileAttributes(char const * path)
{
	if (path == NULL) {
		return(INVALID_FILE_ATTRIBUTES);
	}

	char const * name = strrchr(path, '/');
	return(Attributes_Of(path, name != NULL ? name + 1 : path));
}


BOOL CreateDirectory(char const * path, void *)
{
	return(mkdir(path, 0755) == 0 ? TRUE : FALSE);
}


BOOL DeleteFile(char const * path)
{
	return(unlink(path) == 0 ? TRUE : FALSE);
}


BOOL SetCurrentDirectory(char const * path)
{
	return(chdir(path) == 0 ? TRUE : FALSE);
}


DWORD GetCurrentDirectory(DWORD size, char * buffer)
{
	if (getcwd(buffer, size) == NULL) {
		return(0);
	}
	return((DWORD)strlen(buffer));
}


DWORD GetTempPath(DWORD size, char * buffer)
{
	char const * base = getenv("TMPDIR");
	if (base == NULL || base[0] == '\0') {
		base = "/tmp/";
	}

	int const written = snprintf(buffer, size, "%s%s", base,
											base[strlen(base) - 1] == '/' ? "" : "/");
	if (written <= 0 || (DWORD)written >= size) {
		return(0);
	}
	return((DWORD)written);
}


DWORD GetCurrentProcessId(void)
{
	return((DWORD)getpid());
}


void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext)
{
	if (drive != NULL) {
		drive[0] = '\0';
	}

	char const * name = strrchr(path, '/');
	name = name != NULL ? name + 1 : path;

	if (dir != NULL) {
		size_t const length = name - path;
		memcpy(dir, path, length);
		dir[length] = '\0';
	}

	char const * dot = strrchr(name, '.');
	if (fname != NULL) {
		size_t const length = dot != NULL ? (size_t)(dot - name) : strlen(name);
		memcpy(fname, name, length);
		fname[length] = '\0';
	}

	if (ext != NULL) {
		snprintf(ext, _MAX_EXT, "%s", dot != NULL ? dot : "");
	}
}


char * strupr(char * string)
{
	for (char * scan = string; *scan != '\0'; scan++) {
		*scan = (char)toupper((unsigned char)*scan);
	}
	return(string);
}


bool Parse_GUID_Text(char const * text, GUID & guid)
{
	unsigned int data1;
	unsigned int data2;
	unsigned int data3;
	unsigned int data4[8];

	if (sscanf(text, "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",
					&data1, &data2, &data3,
					&data4[0], &data4[1], &data4[2], &data4[3],
					&data4[4], &data4[5], &data4[6], &data4[7]) != 11) {
		return(false);
	}

	guid.Data1 = data1;
	guid.Data2 = (uint16_t)data2;
	guid.Data3 = (uint16_t)data3;
	for (int index = 0; index < 8; index++) {
		guid.Data4[index] = (uint8_t)data4[index];
	}
	return(true);
}


void Compose_GUID_Text(GUID const & guid, char * buffer, size_t size)
{
	snprintf(buffer, size, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
				guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}


bool Platform_Executable_Path(char * buffer, size_t size)
{
#ifdef __APPLE__
	uint32_t capacity = (uint32_t)size;
	return(_NSGetExecutablePath(buffer, &capacity) == 0);
#else
	ssize_t const length = readlink("/proc/self/exe", buffer, size - 1);
	if (length <= 0) {
		return(false);
	}
	buffer[length] = '\0';
	return(true);
#endif
}


char const * const * Platform_Command_Line_Arguments(int * argc)
{
#ifdef __APPLE__
	*argc = *_NSGetArgc();
	return((char const * const *)*_NSGetArgv());
#else
	static char storage[8192];
	static char * arguments[256];
	static int count = -1;

	if (count < 0) {
		count = 0;

		FILE * file = fopen("/proc/self/cmdline", "rb");
		if (file != NULL) {
			size_t const length = fread(storage, 1, sizeof(storage) - 1, file);
			fclose(file);
			storage[length] = '\0';

			size_t offset = 0;
			while (offset < length && count < (int)(sizeof(arguments) / sizeof(arguments[0]))) {
				arguments[count++] = storage + offset;
				offset += strlen(storage + offset) + 1;
			}
		}
	}

	*argc = count;
	return((char const * const *)arguments);
#endif
}

#endif
