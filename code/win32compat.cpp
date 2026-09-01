/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The out-of-line half of the WebAssembly target's Win32 substitute. win32compat.h
// explains what this stands for and why nothing here succeeds quietly. The file entry
// points are adapters over filesystem.h, which owns where a name actually lives.

#include "always.h"

#include "crtcompat.h"
#include "docfile.h"
#include "filesystem.h"
#include "hostfile.h"
#include "iso9660.h"
#include "misc.h"
#include "platform.h"
#include "video.h"
#include "win32compat.h"

#if !defined(_WIN32)

#if defined(__EMSCRIPTEN__)
#include "browser.h"
#include "isohttp.h"
#include <emscripten.h>
#endif

#include <ctime>
#include <dirent.h>

#if defined(__APPLE__)
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>



/*
** The layout contract. Every value below is what MSVC reports for the same construct on
** Win32 x86, so a change to win32compat.h that moves a field or resizes a type fails the
** build here rather than silently reshaping a saved game or a network packet.
*/
static_assert(sizeof(BYTE) == 1 && sizeof(WORD) == 2 && sizeof(DWORD) == 4, "");
static_assert(sizeof(LONG) == 4 && sizeof(ULONG) == 4 && sizeof(BOOL) == 4, "");
static_assert(sizeof(LONGLONG) == 8 && alignof(LONGLONG) == 8, "");
static_assert(sizeof(HRESULT) == 4, "");
static_assert(sizeof(WPARAM) == sizeof(void *) && sizeof(LPARAM) == sizeof(void *), "");

#if defined(__EMSCRIPTEN__)
/*
** The parts of the contract only wasm32 keeps: a 64-bit native host has 8-byte pointers
** and a 4-byte wchar_t, and nothing that crosses a save or a packet depends on either.
*/
static_assert(sizeof(void *) == 4, "wasm32 must be ILP32, as Win32 x86 is");
static_assert(sizeof(LRESULT) == 4 && sizeof(HANDLE) == 4 && sizeof(HWND) == 4, "");
static_assert(sizeof(WCHAR) == 2, "-fshort-wchar keeps OLECHAR two bytes, as Windows has it");
#endif

static_assert(sizeof(POINT) == 8 && offsetof(POINT, y) == 4, "");
static_assert(sizeof(RECT) == 16 && offsetof(RECT, bottom) == 12, "");
static_assert(sizeof(SIZE) == 8, "");
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(MSG) == 28 && offsetof(MSG, pt) == 20, "");
#endif
static_assert(sizeof(GUID) == 16 && alignof(GUID) == 4, "");
static_assert(sizeof(FILETIME) == 8, "");
static_assert(sizeof(SYSTEMTIME) == 16, "");
static_assert(sizeof(LARGE_INTEGER) == 8 && sizeof(ULARGE_INTEGER) == 8, "");
static_assert(offsetof(LARGE_INTEGER, u.HighPart) == 4, "");
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(CRITICAL_SECTION) == 24, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(WIN32_FIND_DATAA) == 320 && offsetof(WIN32_FIND_DATAA, cFileName) == 44, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(OSVERSIONINFOA) == 148, "");
#endif
static_assert(sizeof(WAVEFORMATEX) == 18, "mmsystem.h packs the wave formats to one byte");
static_assert(sizeof(BITMAPFILEHEADER) == 14, "wingdi.h packs the file header to two bytes");
static_assert(sizeof(BITMAPINFOHEADER) == 40, "");
static_assert(sizeof(RGBQUAD) == 4, "");
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(STATSTG) == 72 && offsetof(STATSTG, cbSize) == 8, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(DSBUFFERDESC) == 20, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(WNDCLASSA) == 40, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(SCROLLINFO) == 28, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(LOGFONTA) == 60, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(EXCEPTION_RECORD) == 80, "");
#endif
#if defined(__EMSCRIPTEN__)
static_assert(sizeof(CONTEXT) == 716, "");
#endif


/*
** ---------------------------------------------------------------------------------------
** The last error and interface identity.
** ---------------------------------------------------------------------------------------
*/


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


extern "C" const GUID GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
extern "C" const IID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_ISequentialStream = {0x0C733A30, 0x2A1C, 0x11CE, {0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D}};
extern "C" const IID IID_IStream = {0x0000000C, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IPersist = {0x0000010C, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IPersistStream = {0x00000109, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IClassFactory = {0x00000001, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};


BOOL IsEqualGUID(REFGUID first, REFGUID second)
{
	return(memcmp(&first, &second, sizeof(GUID)) == 0);
}


/*
** ---------------------------------------------------------------------------------------
** The clocks.
** ---------------------------------------------------------------------------------------
*/


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
** ---------------------------------------------------------------------------------------
** The file layer.
** ---------------------------------------------------------------------------------------
*/


/*
** Adapters over filesystem.h, which is where a name is resolved and a file is opened. What
** is left here is what a Win32 caller expects on top of that: handles, the last-error slot,
** the search family, and the attribute and time conversions. win32compat.h states what a
** caller sees; what follows are the decisions behind it.
*/

/*
** Errors. Only the conditions the file layer can actually produce are distinguished; the
** rest arrive as the generic failure Win32 uses for the same purpose.
*/
static DWORD Win32_Error_From_Errno(int error)
{
	switch (error) {
		case ENOENT:	return(ERROR_FILE_NOT_FOUND);
		case ENOTDIR:	return(ERROR_PATH_NOT_FOUND);
		case EACCES:
		case EPERM:
		case EROFS:
		case EISDIR:	return(ERROR_ACCESS_DENIED);
		case EBADF:		return(ERROR_INVALID_HANDLE);
		case EIO:		return(ERROR_READ_FAULT);
		case ENOMEM:	return(ERROR_NOT_ENOUGH_MEMORY);
		case EEXIST:	return(ERROR_FILE_EXISTS);
		case EINVAL:	return(ERROR_INVALID_PARAMETER);
		case ENOSPC:	return(ERROR_DISK_FULL);
		case EMFILE:
		case ENFILE:	return(ERROR_TOO_MANY_OPEN_FILES);
		case ENOTEMPTY:	return(ERROR_DIRECTORY);
		default:		return(ERROR_GEN_FAILURE);
	}
}


/*
** Handles. A Win32 HANDLE is opaque, so each open file and each search in progress takes a
** slot in a table and the handle is that slot's index plus one. Handing out a descriptor
** directly would collide with both values the API reserves -- zero, and the all-ones
** INVALID_HANDLE_VALUE -- and a descriptor cannot carry what a handle has to remember
** anyway: which kind of object it names, the path a delete-on-close open must remove, and
** the position of a search through its matches. Checking the kind is what keeps a file
** handle passed to FindNextFileA from being read as a search.
**
** What a file handle remembers of the file itself is the stream, which knows whether it is
** reading the host or a mounted disc. A mutex is the third kind, and holds nothing but the
** count of acquisitions outstanding against it.
*/
enum HandleKindType
{
	HANDLE_KIND_FREE,
	HANDLE_KIND_FILE,
	HANDLE_KIND_FIND,
	HANDLE_KIND_MUTEX
};

/*
** One match a search will report. A search spans the host directory and the image, and
** the two answer for a name's size and date differently, so each match remembers which
** side it came from.
*/
struct FindMatchType
{
	std::string Name;
	ISOEntryClass Image;
};

struct HandleEntryType
{
	HandleKindType Kind = HANDLE_KIND_FREE;

	std::unique_ptr<FileStreamClass> Stream;
	bool DeleteOnClose = false;
	std::string Path;

	unsigned int Held = 0;

	std::string Directory;
	std::vector<FindMatchType> Matches;
	std::size_t Position = 0;
};

/*
** The table is reached through a function so that it is built on the first call rather than
** during static initialization, where the engine's own static objects could open a file
** before a namespace-scope container had been constructed.
*/
static std::vector<HandleEntryType> & Handle_Table(void)
{
	static std::vector<HandleEntryType> table;

	return(table);
}


static HANDLE Handle_From_Index(std::size_t index)
{
	return((HANDLE)(ULONG_PTR)(index + 1));
}


static HandleEntryType * Entry_From_Handle(HANDLE handle)
{
	std::vector<HandleEntryType> & table = Handle_Table();
	ULONG_PTR const value = (ULONG_PTR)handle;

	if (value == 0 || value > table.size()) return(nullptr);

	HandleEntryType * const entry = &table[value - 1];
	if (entry->Kind == HANDLE_KIND_FREE) return(nullptr);
	return(entry);
}


static HandleEntryType * Entry_From_Handle(HANDLE handle, HandleKindType kind)
{
	HandleEntryType * const entry = Entry_From_Handle(handle);

	if (entry == nullptr || entry->Kind != kind) return(nullptr);
	return(entry);
}


static HandleEntryType * Entry_From_File_Handle(HANDLE handle)
{
	return(Entry_From_Handle(handle, HANDLE_KIND_FILE));
}


static std::size_t Allocate_Handle(void)
{
	std::vector<HandleEntryType> & table = Handle_Table();

	for (std::size_t index = 0; index < table.size(); index++) {
		if (table[index].Kind == HANDLE_KIND_FREE) return(index);
	}

	table.emplace_back();
	return(table.size() - 1);
}


static void Release_Handle(HandleEntryType * entry)
{
	entry->Kind = HANDLE_KIND_FREE;
	entry->Stream.reset();
	entry->DeleteOnClose = false;
	entry->Path.clear();
	entry->Held = 0;
	entry->Directory.clear();
	entry->Matches.clear();
	entry->Position = 0;
}


// The disc images. hostfile.h owns the mounting and the lookup; these are the names the
// substitute publishes for them, so a caller reaching for the Win32 spelling finds the
// same discs the file layer opens through.
bool Win32_Mount_Image(char const * location)
{
	return(Mount_Disc_Image(location));
}


void Win32_Unmount_Image(void)
{
	Unmount_Disc_Images();
}


// A hint is advisory wherever it lands, so a handle on an ordinary host file accepts one
// and does nothing with it.
// What a stream's failure looks like to a Win32 caller. A read the transport declined has
// not failed: the bytes are on their way, and ERROR_IO_PENDING is what says so.
static DWORD Win32_Error_From_Stream(FileStreamClass const & stream)
{
	if (stream.Declined()) return(ERROR_IO_PENDING);

	int const error = stream.Error();
	return((error != 0) ? Win32_Error_From_Errno(error) : ERROR_GEN_FAILURE);
}


static FileHintType Hint_From_Image_Hint(ISOHintType kind)
{
	if (kind == ISO_HINT_SOON) return(FILE_HINT_SOON);
	if (kind == ISO_HINT_DONE) return(FILE_HINT_DONE);
	return(FILE_HINT_SEQUENTIAL);
}


/// <summary>Says what a run of an already open file is about to be used for.</summary>
bool Win32_Hint_Handle(HANDLE file, ISOHintType kind, unsigned int offset, unsigned int length)
{
	HandleEntryType * const entry = Entry_From_File_Handle(file);

	if (entry == nullptr || !entry->Stream) return(false);

	FileStatusType status;
	if (!entry->Stream->Status(status) || offset >= status.Size) return(false);

	entry->Stream->Hint(Hint_From_Image_Hint(kind), offset, length);
	return(true);
}


/// <summary>Says what a run of a file on a mounted image is about to be used for.</summary>
bool Win32_Hint_File(char const * filename, ISOHintType kind, unsigned int offset, unsigned int length)
{
	if (filename == nullptr || *filename == '\0') return(false);

	ISOEntryClass found;
	std::shared_ptr<ISOVolumeClass> volume = Image_File_Entry(filename, found);

	if (!volume) return(false);
	if (offset >= found.Size) return(false);

	std::uint32_t const span = (length != 0) ? (std::uint32_t)length : (found.Size - offset);

	volume->Hint(found, kind, (std::uint32_t)offset, span);
	return(true);
}


/*
** File times. Win32 counts hundred-nanosecond intervals from the start of 1601 and the
** host counts seconds from the start of 1970; this is the distance between the two epochs.
*/
static long long const FiletimeEpochOffset = 116444736000000000LL;


static FILETIME Filetime_From_Host(long long seconds, long nanoseconds)
{
	unsigned long long const ticks = (unsigned long long)(seconds * 10000000LL + nanoseconds / 100 + FiletimeEpochOffset);
	FILETIME result;

	result.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
	result.dwHighDateTime = (DWORD)(ticks >> 32);
	return(result);
}


/*
** A directory record on the image carries the date packed the way DOS wrote it, and a
** record whose date the volume left unset reports none at all.
*/
static FILETIME Filetime_From_Image(unsigned int datetime)
{
	FILETIME result = {0, 0};

	if (datetime != 0) {
		DosDateTimeToFileTime((WORD)(datetime >> 16), (WORD)(datetime & 0xFFFF), &result);
	}

	return(result);
}


static FILETIME Filetime_From_Ticks(unsigned long long ticks)
{
	FILETIME result;

	result.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
	result.dwHighDateTime = (DWORD)(ticks >> 32);
	return(result);
}


static long long Ticks_From_Filetime(FILETIME const * filetime)
{
	return((long long)(((unsigned long long)filetime->dwHighDateTime << 32) | filetime->dwLowDateTime));
}


static bool Calendar_From_Filetime(FILETIME const * filetime, struct tm * parts, long * milliseconds)
{
	long long const ticks = Ticks_From_Filetime(filetime) - FiletimeEpochOffset;
	long long seconds = ticks / 10000000LL;
	long long remainder = ticks % 10000000LL;

	if (remainder < 0) {
		remainder += 10000000LL;
		seconds--;
	}

	time_t const when = (time_t)seconds;
	if (::gmtime_r(&when, parts) == nullptr) return(false);
	if (milliseconds != nullptr) *milliseconds = (long)(remainder / 10000LL);
	return(true);
}


/*
** Attributes. Only three of Windows' bits have a counterpart the host can answer for. A
** name beginning with a dot is the host's own way of saying an entry is not for display,
** which is close enough to the hidden attribute that mapping it keeps the engine's scans --
** which discard hidden entries -- from picking up dot files. Windows never reports an empty
** attribute set, so a plain writable file reports itself normal.
*/
static DWORD Attributes_From_Stat(char const * name, struct stat const & info)
{
	DWORD attributes = 0;

	if (S_ISDIR(info.st_mode)) attributes |= FILE_ATTRIBUTE_DIRECTORY;
	if ((info.st_mode & S_IWUSR) == 0) attributes |= FILE_ATTRIBUTE_READONLY;

	if (name != nullptr && name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
		attributes |= FILE_ATTRIBUTE_HIDDEN;
	}

	if (attributes == 0) attributes = FILE_ATTRIBUTE_NORMAL;
	return(attributes);
}


/*
** DOS wildcard matching, as the Win32 search uses it: '?' stands for one character, '*' for
** any run of them, and the comparison ignores case. `*.*` is the one form whose meaning
** does not follow from those two rules -- DOS spelled it "every file", and it matches a
** name with no extension as well.
*/
static bool Match_Wildcard(char const * pattern, char const * name)
{
	if (strcmp(pattern, "*.*") == 0) pattern = "*";

	char const * patternmark = nullptr;
	char const * namemark = nullptr;

	while (*name != '\0') {
		if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*name)) {
			pattern++;
			name++;
			continue;
		}

		if (*pattern == '*') {
			patternmark = ++pattern;
			namemark = name;
			continue;
		}

		if (patternmark != nullptr) {
			pattern = patternmark;
			name = ++namemark;
			continue;
		}

		return(false);
	}

	while (*pattern == '*') pattern++;
	return(*pattern == '\0');
}


HANDLE CreateFileA(LPCSTR filename, DWORD access, DWORD sharemode, LPSECURITY_ATTRIBUTES attributes,
	DWORD creation, DWORD flags, HANDLE templatefile)
{
	/*
	** Sharing is advisory here. POSIX has no mandatory locking, so a pair of opens that
	** Windows would have refused both succeed; see win32compat.h.
	*/
	(void)sharemode;
	(void)attributes;

	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_HANDLE_VALUE);
	}

	if (templatefile != nullptr && templatefile != INVALID_HANDLE_VALUE) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("CreateFileA with a template file", INVALID_HANDLE_VALUE));
	}

	if ((access & GENERIC_EXECUTE) != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("CreateFileA with execute access", INVALID_HANDLE_VALUE));
	}

	bool const wantsread = (access & (GENERIC_READ | GENERIC_ALL)) != 0;
	bool const wantswrite = (access & (GENERIC_WRITE | GENERIC_ALL)) != 0;

	/*
	** Zero access is Win32's query-only open, which asks after the file's metadata and never
	** its contents. A read stream answers everything such a caller can ask, so it is not
	** distinguished from a read.
	*/
	FileAccessType wanted = FILE_ACCESS_READ;
	if (wantsread && wantswrite) wanted = FILE_ACCESS_READ_WRITE;
	else if (wantswrite) wanted = FILE_ACCESS_WRITE;

	FileDispositionType disposition;

	switch (creation) {
		case CREATE_NEW:			disposition = FILE_CREATE_NEW; break;
		case CREATE_ALWAYS:			disposition = FILE_CREATE_ALWAYS; break;
		case OPEN_EXISTING:			disposition = FILE_OPEN_EXISTING; break;
		case OPEN_ALWAYS:			disposition = FILE_OPEN_ALWAYS; break;
		case TRUNCATE_EXISTING:		disposition = FILE_TRUNCATE_EXISTING; break;

		default:
			SetLastError(ERROR_INVALID_PARAMETER);
			return(WIN32_UNSUPPORTED("CreateFileA with a creation disposition it does not implement",
				INVALID_HANDLE_VALUE));
	}

	std::unique_ptr<FileStreamClass> stream = Open_File_Stream(filename, wanted, disposition,
		(flags & FILE_FLAG_SEQUENTIAL_SCAN) != 0);
	if (!stream) {
		SetLastError(Win32_Error_From_Errno(File_Layer_Error()));
		return(INVALID_HANDLE_VALUE);
	}

	bool const existed = stream->Existed();

	std::size_t const index = Allocate_Handle();
	HandleEntryType & entry = Handle_Table()[index];

	entry.Kind = HANDLE_KIND_FILE;
	entry.DeleteOnClose = ((flags & FILE_FLAG_DELETE_ON_CLOSE) != 0);
	entry.Stream = std::move(stream);

	// Only a delete-on-close open needs the resolved path, and resolving one costs a walk.
	if (entry.DeleteOnClose) entry.Path = Host_File_Path(filename);

	/*
	** Both dispositions that accept a file already there report the fact through the
	** last-error slot on an otherwise successful open, and callers read it back.
	*/
	SetLastError((existed && (creation == CREATE_ALWAYS || creation == OPEN_ALWAYS))
		? ERROR_ALREADY_EXISTS : NO_ERROR);

	return(Handle_From_Index(index));
}


BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD toread, LPDWORD read, LPOVERLAPPED overlapped)
{
	if (read != nullptr) *read = 0;

	if (overlapped != nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("ReadFile with an overlapped request", FALSE));
	}

	HandleEntryType * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (buffer == nullptr && toread != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	unsigned int got = 0;
	bool const served = entry->Stream->Read(buffer, (unsigned int)toread, got);

	if (read != nullptr) *read = (DWORD)got;

	if (!served) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD towrite, LPDWORD written, LPOVERLAPPED overlapped)
{
	if (written != nullptr) *written = 0;

	if (overlapped != nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WriteFile with an overlapped request", FALSE));
	}

	HandleEntryType * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (buffer == nullptr && towrite != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	unsigned int put = 0;
	bool const served = entry->Stream->Write(buffer, (unsigned int)towrite, put);

	if (written != nullptr) *written = (DWORD)put;

	if (!served) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distancehigh, DWORD method)
{
	HandleEntryType * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(INVALID_SET_FILE_POINTER);
	}

	FileOriginType origin;

	switch (method) {
		case FILE_BEGIN:	origin = FILE_ORIGIN_BEGIN; break;
		case FILE_CURRENT:	origin = FILE_ORIGIN_CURRENT; break;
		case FILE_END:		origin = FILE_ORIGIN_END; break;

		default:
			SetLastError(ERROR_INVALID_PARAMETER);
			return(INVALID_SET_FILE_POINTER);
	}

	/*
	** Without a high word the distance is a signed thirty-two bit offset. With one the pair
	** is a signed sixty-four bit offset whose low half is unsigned, so the sign has to come
	** from the high word alone.
	*/
	long long const offset = (distancehigh != nullptr)
		? (long long)(((unsigned long long)(long long)*distancehigh << 32) | (unsigned long long)(DWORD)distance)
		: (long long)distance;

	long long position = 0;

	if (!entry->Stream->Seek(offset, origin, position)) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(INVALID_SET_FILE_POINTER);
	}

	if (distancehigh != nullptr) *distancehigh = (LONG)((unsigned long long)position >> 32);

	/*
	** A file long enough to hold a position whose low word is all ones would answer a
	** successful seek with the failure value, which is why Win32 has the caller clear the
	** last-error slot beforehand and read it back after. Clearing it here keeps that check
	** meaningful.
	*/
	SetLastError(NO_ERROR);
	return((DWORD)((unsigned long long)position & 0xFFFFFFFFULL));
}


DWORD GetFileSize(HANDLE file, LPDWORD sizehigh)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(INVALID_FILE_SIZE);
	}

	FileStatusType status;

	if (!entry->Stream->Status(status)) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(INVALID_FILE_SIZE);
	}

	/*
	** With no high word there is nowhere to put the upper half of a size that needs one.
	*/
	if (sizehigh == nullptr && status.Size > 0xFFFFFFFFULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_FILE_SIZE);
	}

	if (sizehigh != nullptr) *sizehigh = (DWORD)(status.Size >> 32);

	SetLastError(NO_ERROR);
	return((DWORD)(status.Size & 0xFFFFFFFFULL));
}


BOOL SetEndOfFile(HANDLE file)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (!entry->Stream->Truncate()) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL FlushFileBuffers(HANDLE file)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (!entry->Stream->Flush()) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** A file handle or a mutex closes here. Every other kind of Win32 object this target hands
** out comes from a stub that returned failure, and a search closes through FindClose.
*/
BOOL CloseHandle(HANDLE object)
{
	HandleEntryType * const mutex = Entry_From_Handle(object, HANDLE_KIND_MUTEX);

	if (mutex != nullptr) {
		Release_Handle(mutex);

		SetLastError(NO_ERROR);
		return(TRUE);
	}

	HandleEntryType * const entry = Entry_From_File_Handle(object);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->DeleteOnClose) {
		::unlink(entry->Path.c_str());
		if (Host_Path_Is_Persistent(entry->Path)) Host_Persistent_Touched();
	}

	Release_Handle(entry);

	Host_Flush_Persistent();

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL DeleteFileA(LPCSTR filename)
{
	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (!Delete_File_Entry(filename)) {
		SetLastError(Win32_Error_From_Errno(File_Layer_Error()));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** Win32 refuses to move onto an existing name, where rename would replace it silently.
*/
BOOL MoveFileA(LPCSTR existing, LPCSTR newname)
{
	if (existing == nullptr || newname == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const target = Host_File_Path(newname);
	if (Host_Path_Present(target)) {
		SetLastError(ERROR_ALREADY_EXISTS);
		return(FALSE);
	}

	std::string const source = Host_File_Path(existing);

	if (::rename(source.c_str(), target.c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	if (Host_Path_Is_Persistent(source) || Host_Path_Is_Persistent(target)) Host_Persistent_Touched();
	Host_Flush_Persistent();

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL CopyFileA(LPCSTR existing, LPCSTR newname, BOOL failifexists)
{
	if (existing == nullptr || newname == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const target = Host_File_Path(newname);
	if (failifexists && Host_Path_Present(target)) {
		SetLastError(ERROR_FILE_EXISTS);
		return(FALSE);
	}

	int const source = ::open(Host_File_Path(existing).c_str(), O_RDONLY);
	if (source < 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	int const destination = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0666);
	if (destination < 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		::close(source);
		return(FALSE);
	}

	char block[64 * 1024];
	bool copied = true;

	for (;;) {
		ssize_t const got = ::read(source, block, sizeof(block));

		if (got < 0) {
			if (errno == EINTR) continue;
			copied = false;
			break;
		}
		if (got == 0) break;

		ssize_t placed = 0;
		while (placed < got) {
			ssize_t const put = ::write(destination, block + placed, (size_t)(got - placed));
			if (put < 0) {
				if (errno == EINTR) continue;
				copied = false;
				break;
			}
			placed += put;
		}

		if (!copied) break;
	}

	int const failure = errno;

	::close(source);
	::close(destination);

	if (!copied) {
		SetLastError(Win32_Error_From_Errno(failure));
		return(FALSE);
	}

	if (Host_Path_Is_Persistent(target)) Host_Persistent_Touched();
	Host_Flush_Persistent();

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD GetFileAttributesA(LPCSTR filename)
{
	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_FILE_ATTRIBUTES);
	}

	std::string const path = Host_File_Path(filename);
	struct stat info;

	if (::stat(path.c_str(), &info) != 0) {
		int const failure = errno;
		ISOEntryClass found;

		/*
		** A name CreateFileA can open has to be a name this reports on, or a caller that
		** tests before opening would decide the file is missing.
		*/
		if (Image_File_Entry(filename, found)) {
			SetLastError(NO_ERROR);
			return(FILE_ATTRIBUTE_READONLY);
		}

		SetLastError(Win32_Error_From_Errno(failure));
		return(INVALID_FILE_ATTRIBUTES);
	}

	std::size_t const mark = path.find_last_of('/');

	SetLastError(NO_ERROR);
	return(Attributes_From_Stat((mark == std::string::npos) ? path.c_str() : path.c_str() + mark + 1, info));
}


/*
** Of Windows' attributes only the read-only bit describes something the host stores, so it
** is the only one applied. The rest -- archive, system, temporary, and the others -- name
** Windows bookkeeping with no counterpart, and are accepted and dropped rather than
** reported, since a caller that sets them is not asking for behavior it will then observe.
*/
BOOL SetFileAttributesA(LPCSTR filename, DWORD attributes)
{
	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const path = Host_File_Path(filename);
	struct stat info;

	if (::stat(path.c_str(), &info) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	mode_t mode = info.st_mode & (mode_t)07777;

	if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
		mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
	} else {
		mode |= (mode_t)S_IWUSR;
	}

	if (::chmod(path.c_str(), mode) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** The host has no creation time. Its closest record is the inode change time, which moves
** whenever the file's metadata does, so a caller reading the creation time back gets the
** last time anything about the file changed rather than the moment it appeared.
*/
BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	FileStatusType status;

	if (!entry->Stream->Status(status)) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	if (creation != nullptr) *creation = Filetime_From_Ticks(status.Creation);
	if (access != nullptr) *access = Filetime_From_Ticks(status.Access);
	if (write != nullptr) *write = Filetime_From_Ticks(status.Write);

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** A null time means leave that one alone, as it does under Win32. The creation time cannot
** be set on a POSIX host -- there is none stored -- so it is accepted and dropped.
*/
BOOL SetFileTime(HANDLE file, FILETIME const * creation, FILETIME const * access, FILETIME const * write)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	unsigned long long const created = (creation != nullptr) ? (unsigned long long)Ticks_From_Filetime(creation) : 0ull;
	unsigned long long const touched = (access != nullptr) ? (unsigned long long)Ticks_From_Filetime(access) : 0ull;
	unsigned long long const written = (write != nullptr) ? (unsigned long long)Ticks_From_Filetime(write) : 0ull;

	if (!entry->Stream->Set_Times(created, touched, written)) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION information)
{
	if (information == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	FileStatusType status;

	if (!entry->Stream->Status(status)) {
		SetLastError(Win32_Error_From_Stream(*entry->Stream));
		return(FALSE);
	}

	memset(information, 0, sizeof(*information));

	// Windows never reports an empty attribute set, so a plain writable file reports itself
	// normal. Nothing the file layer answers distinguishes a hidden or system file here.
	information->dwFileAttributes = status.IsReadOnly ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
	information->ftCreationTime = Filetime_From_Ticks(status.Creation);
	information->ftLastAccessTime = Filetime_From_Ticks(status.Access);
	information->ftLastWriteTime = Filetime_From_Ticks(status.Write);
	information->dwVolumeSerialNumber = (DWORD)status.Volume;
	information->nFileSizeHigh = (DWORD)(status.Size >> 32);
	information->nFileSizeLow = (DWORD)(status.Size & 0xFFFFFFFFULL);
	information->nNumberOfLinks = (DWORD)status.Links;
	information->nFileIndexHigh = (DWORD)(status.Identifier >> 32);
	information->nFileIndexLow = (DWORD)(status.Identifier & 0xFFFFFFFFULL);

	SetLastError(NO_ERROR);
	return(TRUE);
}


static void Fill_Find_Data(LPWIN32_FIND_DATAA data, std::string const & directory, FindMatchType const & match)
{
	memset(data, 0, sizeof(*data));

	struct stat info;

	if (match.Image.Is_Valid()) {
		FILETIME const recorded = Filetime_From_Image(match.Image.DateTime);

		data->dwFileAttributes = FILE_ATTRIBUTE_READONLY;
		data->ftCreationTime = recorded;
		data->ftLastAccessTime = recorded;
		data->ftLastWriteTime = recorded;
		data->nFileSizeLow = (DWORD)match.Image.Size;

	} else if (::stat(Host_File_Path((directory + match.Name).c_str()).c_str(), &info) == 0) {
		data->dwFileAttributes = Attributes_From_Stat(match.Name.c_str(), info);
		data->ftCreationTime = Filetime_From_Host(info.st_ctim.tv_sec, info.st_ctim.tv_nsec);
		data->ftLastAccessTime = Filetime_From_Host(info.st_atim.tv_sec, info.st_atim.tv_nsec);
		data->ftLastWriteTime = Filetime_From_Host(info.st_mtim.tv_sec, info.st_mtim.tv_nsec);
		data->nFileSizeHigh = (DWORD)((unsigned long long)info.st_size >> 32);
		data->nFileSizeLow = (DWORD)((unsigned long long)info.st_size & 0xFFFFFFFFULL);

	} else {
		data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	}

	strncpy(data->cFileName, match.Name.c_str(), sizeof(data->cFileName) - 1);
	data->cFileName[sizeof(data->cFileName) - 1] = '\0';
}


/*
** Adds what the persistent directory holds to a search of the game directory, which is the
** directory it stands in front of. A search anywhere else does not reach it, and a name the
** game directory already answered for is left alone so that the two agree with the order
** Host_Path resolves a bare name in.
*/
static void Persistent_Matches(std::string const & directory, std::string const & leaf, std::vector<FindMatchType> & matches)
{
	std::string const & root = Host_Persistent_Root();
	if (root.empty() || !directory.empty()) return;

	DIR * const scan = ::opendir(root.c_str());
	if (scan == nullptr) return;

	for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
		if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;

		bool taken = false;

		for (FindMatchType const & already : matches) {
			if (::strcasecmp(already.Name.c_str(), item->d_name) == 0) taken = true;
		}
		if (taken) continue;

		FindMatchType match;
		match.Name = item->d_name;
		matches.push_back(std::move(match));
	}

	::closedir(scan);
}


/*
** Adds what the images hold to a search the host has already answered. Every disc and every
** search directory on it contributes, so a search finds a file wherever among them it is --
** which is what lets one scan for MAPS*.MIX collect the archive each disc carries under that
** name.
**
** Both halves are matched by Match_Wildcard rather than by the reader's own matcher, which
** does not carry the DOS rule that `*.*` means every file. One matcher over both halves is
** what makes a search of an image and a search of a directory report the same set.
**
** A name already answered for is left alone, so a search reports the same copy of a name
** that CreateFileA opens: the host's, then the first disc in order that carries it.
*/
static void Image_Matches(std::string const & directory, std::string const & leaf, std::vector<FindMatchType> & matches)
{
	std::string inside;
	if (!Image_Relative_Path(directory.c_str(), inside)) return;

	for (MountedImageType const & image : Mounted_Disc_Images()) {
		for (std::string const & search : image.Directories) {
			std::string const path = Image_Search_Path(search, inside);

			ISOEntryClass folder = image.Volume->Root();
			if (!path.empty() && (!image.Volume->Find(path.c_str(), folder) || !folder.IsDirectory)) continue;

			std::vector<std::string> names;
			if (!image.Volume->Enumerate(folder, names)) continue;

			for (std::string const & name : names) {
				if (!Match_Wildcard(leaf.c_str(), name.c_str())) continue;

				bool taken = false;

				for (FindMatchType const & already : matches) {
					if (::strcasecmp(already.Name.c_str(), name.c_str()) == 0) taken = true;
				}
				if (taken) continue;

				FindMatchType match;
				match.Name = name;

				if (!image.Volume->Find_In(folder, name.c_str(), match.Image)) continue;
				matches.push_back(std::move(match));
			}
		}
	}
}


/*
** The whole search runs here and the matches are kept on the handle, rather than the
** directory being held open and read entry by entry. The engine scans a directory it is
** also writing into -- the debug log's own folder is swept for stale logs while a log is
** open in it -- and a snapshot gives that a defined answer.
**
** Sorting the matches is a deliberate departure from Windows, which returns whatever order
** the filesystem holds. The order decides which ECACHE*.MIX overrides which, so leaving it
** to the host would let two machines with the same files disagree.
*/
HANDLE FindFirstFileA(LPCSTR filename, LPWIN32_FIND_DATAA data)
{
	if (filename == nullptr || data == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_HANDLE_VALUE);
	}

	std::string pattern(filename);

	for (char & character : pattern) {
		if (character == '\\') character = '/';
	}

	std::size_t const split = pattern.find_last_of('/');
	std::string const requested = (split == std::string::npos) ? std::string() : pattern.substr(0, split + 1);
	std::string directory = requested;
	std::string const leaf = (split == std::string::npos) ? pattern : pattern.substr(split + 1);

	if (!directory.empty()) directory = Host_File_Path(directory.c_str());

	std::vector<FindMatchType> matches;

	if (leaf.find_first_of("*?") == std::string::npos) {

		/*
		** A search with no wildcard names one entry, and Win32 answers with that entry
		** alone. The map generator asks after its cache directory this way.
		*/
		std::string const resolved = Host_File_Path((directory + leaf).c_str());
		struct stat info;

		if (::stat(resolved.c_str(), &info) == 0) {
			std::size_t const mark = resolved.find_last_of('/');
			FindMatchType match;

			directory = (mark == std::string::npos) ? std::string() : resolved.substr(0, mark + 1);
			match.Name = (mark == std::string::npos) ? resolved : resolved.substr(mark + 1);
			matches.push_back(std::move(match));
		}

	} else {

		DIR * const scan = ::opendir(directory.empty() ? "." : directory.c_str());

		if (scan != nullptr) {
			for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
				if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;

				FindMatchType match;
				match.Name = item->d_name;
				matches.push_back(std::move(match));
			}
			::closedir(scan);
		}
	}

	Persistent_Matches(requested, leaf, matches);

	/*
	** The image is searched under the name the caller wrote rather than the one the host
	** resolved to, since the two filesystems answer for case separately.
	*/
	Image_Matches(requested, leaf, matches);

	std::sort(matches.begin(), matches.end(), [](FindMatchType const & left, FindMatchType const & right) {
		int const order = ::strcasecmp(left.Name.c_str(), right.Name.c_str());
		return(order != 0 ? order < 0 : left.Name < right.Name);
	});

	if (matches.empty()) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return(INVALID_HANDLE_VALUE);
	}

	std::size_t const index = Allocate_Handle();
	HandleEntryType & entry = Handle_Table()[index];

	entry.Kind = HANDLE_KIND_FIND;
	entry.Directory = directory;
	entry.Matches = std::move(matches);
	entry.Position = 1;

	Fill_Find_Data(data, entry.Directory, entry.Matches[0]);

	SetLastError(NO_ERROR);
	return(Handle_From_Index(index));
}


BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data)
{
	if (data == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	HandleEntryType * const entry = Entry_From_Handle(find, HANDLE_KIND_FIND);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Position >= entry->Matches.size()) {
		SetLastError(ERROR_NO_MORE_FILES);
		return(FALSE);
	}

	Fill_Find_Data(data, entry->Directory, entry->Matches[entry->Position]);
	entry->Position++;

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL FindClose(HANDLE find)
{
	HandleEntryType * const entry = Entry_From_Handle(find, HANDLE_KIND_FIND);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	Release_Handle(entry);

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** Win32 reports a directory that was already there as a failure with ERROR_ALREADY_EXISTS
** rather than as success, and callers test for exactly that.
*/
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attributes)
{
	(void)attributes;

	if (path == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (::mkdir(Host_File_Path(path).c_str(), (mode_t)0777) != 0) {
		SetLastError(errno == EEXIST ? (DWORD)ERROR_ALREADY_EXISTS : Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL RemoveDirectoryA(LPCSTR path)
{
	if (path == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (::rmdir(Host_File_Path(path).c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** On success Win32 answers with the length written, not counting the terminator; when the
** buffer is too small it answers with the length needed, counting it.
*/
DWORD GetCurrentDirectoryA(DWORD length, LPSTR buffer)
{
	char working[MAX_PATH * 4];

	if (::getcwd(working, sizeof(working)) == nullptr) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(0);
	}

	DWORD const needed = (DWORD)strlen(working);

	if (buffer == nullptr || length <= needed) {
		SetLastError(NO_ERROR);
		return(needed + 1);
	}

	memcpy(buffer, working, needed + 1);

	SetLastError(NO_ERROR);
	return(needed);
}


BOOL SetCurrentDirectoryA(LPCSTR path)
{
	if (path == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (::chdir(Host_File_Path(path).c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


/*
** Win32 answers with the length written, not counting the terminator, and the path ends in
** its separator.
*/
DWORD GetTempPathA(DWORD length, LPSTR buffer)
{
	char const * const temporary = "/tmp/";
	DWORD const needed = (DWORD)strlen(temporary);

	if (buffer == nullptr || length <= needed) {
		SetLastError(NO_ERROR);
		return(needed + 1);
	}

	memcpy(buffer, temporary, needed + 1);

	SetLastError(NO_ERROR);
	return(needed);
}


LONG CompareFileTime(FILETIME const * first, FILETIME const * second)
{
	unsigned long long left = ((unsigned long long)first->dwHighDateTime << 32) | first->dwLowDateTime;
	unsigned long long right = ((unsigned long long)second->dwHighDateTime << 32) | second->dwLowDateTime;

	if (left < right) return(-1);
	if (left > right) return(1);
	return(0);
}


BOOL FileTimeToSystemTime(FILETIME const * filetime, LPSYSTEMTIME systemtime)
{
	if (filetime == nullptr || systemtime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	long milliseconds = 0;

	if (!Calendar_From_Filetime(filetime, &parts, &milliseconds)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	Fill_System_Time(systemtime, parts, milliseconds);
	return(TRUE);
}


BOOL SystemTimeToFileTime(SYSTEMTIME const * systemtime, LPFILETIME filetime)
{
	if (systemtime == nullptr || filetime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	memset(&parts, 0, sizeof(parts));

	parts.tm_year = (int)systemtime->wYear - 1900;
	parts.tm_mon = (int)systemtime->wMonth - 1;
	parts.tm_mday = (int)systemtime->wDay;
	parts.tm_hour = (int)systemtime->wHour;
	parts.tm_min = (int)systemtime->wMinute;
	parts.tm_sec = (int)systemtime->wSecond;

	time_t const seconds = ::timegm(&parts);
	if (seconds == (time_t)-1) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	*filetime = Filetime_From_Host((long long)seconds, (long)systemtime->wMilliseconds * 1000000L);
	return(TRUE);
}


/*
** The zone offset is taken at the instant being converted rather than at the current one,
** so a time recorded under the other side of a daylight-saving change converts correctly.
*/
BOOL FileTimeToLocalFileTime(FILETIME const * filetime, LPFILETIME local)
{
	if (filetime == nullptr || local == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	long long const ticks = Ticks_From_Filetime(filetime) - FiletimeEpochOffset;
	time_t const when = (time_t)(ticks / 10000000LL);

	struct tm parts;
	if (::localtime_r(&when, &parts) == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	time_t const shifted = ::timegm(&parts);
	long long const offset = (long long)(shifted - when) * 10000000LL;

	unsigned long long const adjusted = (unsigned long long)(Ticks_From_Filetime(filetime) + offset);
	local->dwLowDateTime = (DWORD)(adjusted & 0xFFFFFFFFULL);
	local->dwHighDateTime = (DWORD)(adjusted >> 32);
	return(TRUE);
}


/*
** The DOS packing: a date word of the year past 1980, the month, and the day; a time word
** of the hour, the minute, and the second halved. Nothing outside 1980 through 2107 fits.
*/
BOOL FileTimeToDosDateTime(FILETIME const * filetime, LPWORD dosdate, LPWORD dostime)
{
	if (filetime == nullptr || dosdate == nullptr || dostime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	if (!Calendar_From_Filetime(filetime, &parts, nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	int const year = parts.tm_year + 1900;
	if (year < 1980 || year > 2107) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	*dosdate = (WORD)(((year - 1980) << 9) | ((parts.tm_mon + 1) << 5) | parts.tm_mday);
	*dostime = (WORD)((parts.tm_hour << 11) | (parts.tm_min << 5) | (parts.tm_sec / 2));
	return(TRUE);
}


BOOL DosDateTimeToFileTime(WORD dosdate, WORD dostime, LPFILETIME filetime)
{
	if (filetime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	memset(&parts, 0, sizeof(parts));

	parts.tm_year = ((dosdate >> 9) & 0x7F) + 1980 - 1900;
	parts.tm_mon = ((dosdate >> 5) & 0x0F) - 1;
	parts.tm_mday = dosdate & 0x1F;
	parts.tm_hour = (dostime >> 11) & 0x1F;
	parts.tm_min = (dostime >> 5) & 0x3F;
	parts.tm_sec = (dostime & 0x1F) * 2;

	time_t const seconds = ::timegm(&parts);
	if (seconds == (time_t)-1) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	*filetime = Filetime_From_Host((long long)seconds, 0);
	return(TRUE);
}


/*
** ---------------------------------------------------------------------------------------
** Events, mutexes, and the interlocked operations.
** ---------------------------------------------------------------------------------------
*/


/*
** An event is something one thread waits on for another to signal, and there is no other
** thread here to signal it, so nothing below keeps a state. Each reports itself instead:
** code that reaches one is waiting on something this target cannot deliver.
*/

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL SetEvent(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL ResetEvent(HANDLE) { return(WIN32_STUB(FALSE)); }


/*
** Mutual exclusion where there is one thread.
**
** A page runs the engine on the thread it owns and nothing here starts another -- see the
** CreateThread stub -- so a mutex is never contended. Every acquisition succeeds at once
** and no wait ever blocks, which is the answer Windows itself would give under the same
** conditions rather than an approximation of one. So these are implemented: a caller that
** reads the result as a plain success or failure, as the audio maintenance pass does, gets
** the right answer instead of the failure a stub would have to report.
**
** Ownership is still counted, so a release that does not match an acquisition fails the
** way it would on Windows. If a second thread is ever introduced, these become wrong
** rather than merely unimplemented, and have to be written against whatever carries it.
*/
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialowner, LPCSTR name)
{
	(void)attributes;

	/*
	** A named mutex is how the engine asks whether another copy of itself is already
	** running. One module is one process and it cannot see another's names, so the name is
	** recorded and the mutex is always the first of its name.
	*/
	std::size_t const index = Allocate_Handle();
	HandleEntryType & entry = Handle_Table()[index];

	entry.Kind = HANDLE_KIND_MUTEX;
	entry.Path = (name != nullptr) ? name : "";
	entry.Held = (initialowner != FALSE) ? 1u : 0u;

	SetLastError(NO_ERROR);
	return(Handle_From_Index(index));
}


BOOL ReleaseMutex(HANDLE mutex)
{
	HandleEntryType * const entry = Entry_From_Handle(mutex, HANDLE_KIND_MUTEX);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Held == 0) {
		SetLastError(ERROR_NOT_OWNER);
		return(FALSE);
	}

	entry->Held--;

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD WaitForSingleObject(HANDLE object, DWORD milliseconds)
{
	(void)milliseconds;

	HandleEntryType * const entry = Entry_From_Handle(object, HANDLE_KIND_MUTEX);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(WIN32_UNSUPPORTED("WaitForSingleObject on anything but a mutex", WAIT_FAILED));
	}

	entry->Held++;

	SetLastError(NO_ERROR);
	return(WAIT_OBJECT_0);
}


DWORD WaitForMultipleObjects(DWORD count, HANDLE const * objects, BOOL waitall, DWORD milliseconds)
{
	(void)milliseconds;

	if (objects == nullptr || count == 0 || count > MAXIMUM_WAIT_OBJECTS) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WAIT_FAILED);
	}

	for (DWORD index = 0; index < count; index++) {
		if (Entry_From_Handle(objects[index], HANDLE_KIND_MUTEX) != nullptr) continue;

		SetLastError(ERROR_INVALID_HANDLE);
		return(WIN32_UNSUPPORTED("WaitForMultipleObjects on anything but a mutex", WAIT_FAILED));
	}

	/*
	** Waiting for any one of them takes the first, since none of them can be held.
	*/
	DWORD const taken = (waitall != FALSE) ? count : 1;

	for (DWORD index = 0; index < taken; index++) {
		Entry_From_Handle(objects[index], HANDLE_KIND_MUTEX)->Held++;
	}

	SetLastError(NO_ERROR);
	return(WAIT_OBJECT_0);
}


HANDLE OpenMutexA(DWORD, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }


/*
** The interlocked operations are single-threaded here, so they are implemented rather
** than stubbed: the arithmetic is the whole contract once there is only one thread.
*/
LONG InterlockedIncrement(LONG volatile * addend) { LONG value = *addend + 1; *addend = value; return(value); }
LONG InterlockedDecrement(LONG volatile * addend) { LONG value = *addend - 1; *addend = value; return(value); }
LONG InterlockedExchange(LONG volatile * target, LONG value) { LONG old = *target; *target = value; return(old); }


/*
** ---------------------------------------------------------------------------------------
** The heap.
** ---------------------------------------------------------------------------------------
*/


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


HLOCAL LocalFree(HLOCAL memory) { free(memory); return(nullptr); }


/*
** ---------------------------------------------------------------------------------------
** The registry and the profiles.
** ---------------------------------------------------------------------------------------
*/


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


/*
** ---------------------------------------------------------------------------------------
** COM.
** ---------------------------------------------------------------------------------------
*/


/*
** The class object table, which is all of the COM runtime this port needs. The engine is
** its own in-process server: RegisterClasses publishes a class factory for every
** persistent game class, and every activation afterwards names one of those classes, so
** an activation is a lookup followed by a call the module already implements. Nothing
** here consults a registry, marshals, or reaches another process, and no entry outlives
** the CoRevokeClassObject that gives its reference back.
*/
struct ComClassObject
{
	CLSID Class;
	IClassFactory * Factory;
	DWORD Context;
	DWORD Flags;
	DWORD Cookie;
};

static std::vector<ComClassObject> ComClassObjects;
static DWORD ComNextCookie = 1;


/*
** Reports an activation that named a class nobody registered. The identifier is printed
** in the form the MIDL sources and the REGISTER_CLASS list write it so the missing
** registration can be found, rather than leaving the caller a null pointer to walk into.
*/
static void Com_Report_Unregistered(REFCLSID classid)
{
	fprintf(stderr, "OpenTS: no class object is registered for CLSID "
		"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}.\n",
		(unsigned long)classid.Data1, (unsigned int)classid.Data2, (unsigned int)classid.Data3,
		classid.Data4[0], classid.Data4[1], classid.Data4[2], classid.Data4[3],
		classid.Data4[4], classid.Data4[5], classid.Data4[6], classid.Data4[7]);
	fflush(stderr);
}


/*
** Fetches the factory registered for a class, newest registration first, as the service
** control manager resolves a duplicate. The reference the table holds is not handed out;
** the factory outlives the call because only CoRevokeClassObject drops an entry.
*/
static IClassFactory * Com_Find_Class_Object(REFCLSID classid, DWORD context)
{
	for (size_t index = ComClassObjects.size(); index > 0; index--) {
		ComClassObject const & entry = ComClassObjects[index - 1];
		if (entry.Class == classid && (entry.Context & context) != 0) {
			return(entry.Factory);
		}
	}
	return(nullptr);
}


HRESULT CoInitialize(LPVOID)
{
	return(S_OK);
}


void CoUninitialize(void)
{
}


HRESULT CoRegisterClassObject(REFCLSID classid, IUnknown * object, DWORD context, DWORD flags, LPDWORD registration)
{
	if (registration == nullptr) {
		return(E_INVALIDARG);
	}
	*registration = 0;

	if (object == nullptr) {
		return(E_INVALIDARG);
	}

	IClassFactory * factory = nullptr;
	HRESULT result = object->QueryInterface(IID_IClassFactory, (void **)&factory);
	if (FAILED(result)) {
		return(result);
	}

	ComClassObject entry;
	entry.Class = classid;
	entry.Factory = factory;
	entry.Context = context;
	entry.Flags = flags;
	entry.Cookie = ComNextCookie;
	ComNextCookie++;
	ComClassObjects.push_back(entry);

	*registration = entry.Cookie;
	return(S_OK);
}


HRESULT CoRevokeClassObject(DWORD registration)
{
	for (size_t index = 0; index < ComClassObjects.size(); index++) {
		if (ComClassObjects[index].Cookie == registration) {
			IClassFactory * factory = ComClassObjects[index].Factory;
			ComClassObjects.erase(ComClassObjects.begin() + index);
			factory->Release();
			return(S_OK);
		}
	}
	return(E_INVALIDARG);
}


HRESULT CoGetClassObject(REFCLSID classid, DWORD context, LPVOID, REFIID riid, LPVOID * object)
{
	if (object == nullptr) {
		return(E_INVALIDARG);
	}
	*object = nullptr;

	IClassFactory * factory = Com_Find_Class_Object(classid, context);
	if (factory == nullptr) {
		Com_Report_Unregistered(classid);
		return(REGDB_E_CLASSNOTREG);
	}

	return(factory->QueryInterface(riid, object));
}


HRESULT CoCreateInstance(REFCLSID classid, IUnknown * outer, DWORD context, REFIID riid, LPVOID * object)
{
	if (object == nullptr) {
		return(E_INVALIDARG);
	}
	*object = nullptr;

	IClassFactory * factory = Com_Find_Class_Object(classid, context);
	if (factory == nullptr) {
		Com_Report_Unregistered(classid);
		return(REGDB_E_CLASSNOTREG);
	}

	return(factory->CreateInstance(outer, riid, object));
}


LPVOID CoTaskMemAlloc(SIZE_T size) { return(malloc(size)); }
void CoTaskMemFree(LPVOID block) { free(block); }
HRESULT CreateStreamOnHGlobal(HGLOBAL, BOOL, LPSTREAM * stream) { if (stream != nullptr) *stream = nullptr; return(WIN32_STUB(E_NOTIMPL)); }


/// <summary>
/// Reads a class identifier out of its registry-form text.
/// </summary>
/// <remarks>
/// The braces are optional, as they are on Windows. Every rules and art entry naming a
/// locomotor reaches the object model through here, so a refusal is not cosmetic: the
/// caller keeps its default and the type silently receives the wrong class.
/// </remarks>
HRESULT CLSIDFromString(LPCOLESTR string, LPCLSID classid)
{
	if (classid != nullptr) {
		*classid = GUID_NULL;
	}

	if (string == nullptr || classid == nullptr) {
		return(E_INVALIDARG);
	}

	// The text is UTF-16 and every character it may legally hold is ASCII.
	char narrow[40];
	unsigned int length = 0;

	for (OLECHAR const * ptr = string; *ptr != 0; ptr++) {
		if (*ptr == '{' || *ptr == '}') {
			continue;
		}
		if (length >= sizeof(narrow) - 1 || (unsigned short)*ptr > 0x7F) {
			return(CO_E_CLASSSTRING);
		}
		narrow[length++] = (char)*ptr;
	}
	narrow[length] = '\0';

	unsigned int data1 = 0;
	unsigned int data2 = 0;
	unsigned int data3 = 0;
	unsigned int data4[8] = { 0 };

	int scanned = sscanf(narrow, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
		&data1, &data2, &data3,
		&data4[0], &data4[1], &data4[2], &data4[3],
		&data4[4], &data4[5], &data4[6], &data4[7]);

	if (scanned != 11 || length != 36) {
		return(CO_E_CLASSSTRING);
	}

	classid->Data1 = (unsigned long)data1;
	classid->Data2 = (unsigned short)data2;
	classid->Data3 = (unsigned short)data3;

	for (int index = 0; index < 8; index++) {
		classid->Data4[index] = (unsigned char)data4[index];
	}

	return(S_OK);
}


HRESULT CoFileTimeNow(FILETIME * filetime)
{
	if (filetime == nullptr) return(E_INVALIDARG);

	// Win32 counts 100 nanosecond intervals from the start of 1601; the host counts seconds
	// from the start of 1970.
	unsigned long long const epoch = 116444736000000000ULL;
	unsigned long long const now = epoch + (unsigned long long)::time(nullptr) * 10000000ULL;

	filetime->dwLowDateTime = (DWORD)(now & 0xFFFFFFFFULL);
	filetime->dwHighDateTime = (DWORD)(now >> 32);

	return(S_OK);
}


HRESULT OleInitialize(LPVOID reserved) { return(CoInitialize(reserved)); }
void OleUninitialize(void) { CoUninitialize(); }


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


/*
** ---------------------------------------------------------------------------------------
** The MSVC C runtime.
** ---------------------------------------------------------------------------------------
*/


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
** ---------------------------------------------------------------------------------------
** Resources and version information.
** ---------------------------------------------------------------------------------------
*/


int LoadStringA(HINSTANCE, UINT, LPSTR buffer, int size) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
HRSRC FindResourceA(HMODULE, LPCSTR, LPCSTR) { return(WIN32_STUB((HRSRC)nullptr)); }
HGLOBAL LoadResource(HMODULE, HRSRC) { return(WIN32_STUB((HGLOBAL)nullptr)); }
LPVOID LockResource(HGLOBAL) { return(WIN32_STUB((LPVOID)nullptr)); }
DWORD SizeofResource(HMODULE, HRSRC) { return(WIN32_STUB(0)); }


/*
** Version information is a resource in a Portable Executable's directory. The running
** module is a wasm binary, which has no such directory and so carries none: zero is what
** Windows answers for a file without a version resource, and it is the result here rather
** than a gap. A caller reading it takes the branch it already keeps for a program built
** with no version resource of its own -- Version_Name leaves its placeholder standing.
** Another file may well carry one this target does not read, so that request is reported.
*/
DWORD GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle)
{
	if (handle != nullptr) *handle = 0;

	char module[MAX_PATH];

	if (filename != nullptr && Platform_Executable_Path(module, sizeof(module))
			&& strcmp(filename, module) == 0) {
		return(0);
	}

	return(WIN32_STUB(0));
}


BOOL GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID) { return(WIN32_STUB(FALSE)); }
BOOL VerQueryValueA(LPCVOID, LPCSTR, LPVOID * buffer, PUINT length) { if (buffer != nullptr) *buffer = nullptr; if (length != nullptr) *length = 0; return(WIN32_STUB(FALSE)); }


/*
** ---------------------------------------------------------------------------------------
** The console.
** ---------------------------------------------------------------------------------------
*/


BOOL AllocConsole(void) { return(WIN32_STUB(FALSE)); }
BOOL FreeConsole(void) { return(WIN32_STUB(FALSE)); }
BOOL SetConsoleTitleA(LPCSTR) { return(WIN32_STUB(FALSE)); }
HANDLE GetStdHandle(DWORD) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL SetConsoleMode(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL GetConsoleMode(HANDLE, LPDWORD) { return(WIN32_STUB(FALSE)); }


BOOL SetStdHandle(DWORD, HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL GetConsoleScreenBufferInfo(HANDLE, PCONSOLE_SCREEN_BUFFER_INFO) { return(WIN32_STUB(FALSE)); }
HWND GetConsoleWindow(void) { return(WIN32_STUB((HWND)nullptr)); }


BOOL SetConsoleScreenBufferSize(HANDLE, COORD) { return(WIN32_STUB(FALSE)); }


BOOL WriteConsoleA(HANDLE, void const *, DWORD, LPDWORD written, LPVOID) { if (written != nullptr) *written = 0; return(WIN32_STUB(FALSE)); }


/*
** ---------------------------------------------------------------------------------------
** OLE serialization.
** ---------------------------------------------------------------------------------------
*/


BSTR SysAllocString(OLECHAR const *) { return(WIN32_STUB((BSTR)nullptr)); }
void SysFreeString(BSTR) { WIN32_STUB_VOID(); }
UINT SysStringLen(BSTR) { return(WIN32_STUB(0)); }
HRESULT StringFromCLSID(REFCLSID, LPOLESTR * string) { if (string != nullptr) *string = nullptr; return(WIN32_STUB(E_NOTIMPL)); }


/*
** An object is framed on the stream by its class identifier and nothing else: sixteen bytes
** naming the class, then whatever the object writes for itself. That framing is the save
** file's type discriminator, so it is the one thing here that may not change shape.
*/
HRESULT OleSaveToStream(IPersistStream * persist, IStream * stream)
{
	if (persist == nullptr || stream == nullptr) return(E_INVALIDARG);

	CLSID classid;
	HRESULT result = persist->GetClassID(&classid);
	if (FAILED(result)) return(result);

	result = stream->Write(&classid, sizeof(classid), nullptr);
	if (FAILED(result)) return(result);

	return(persist->Save(stream, TRUE));
}


HRESULT OleLoadFromStream(IStream * stream, REFIID riid, void ** object)
{
	if (object != nullptr) *object = nullptr;
	if (stream == nullptr || object == nullptr) return(E_INVALIDARG);

	CLSID classid;
	ULONG read = 0;
	HRESULT result = stream->Read(&classid, sizeof(classid), &read);
	if (FAILED(result)) return(result);
	if (read != sizeof(classid)) return(E_FAIL);

	if (classid == GUID_NULL) return(REGDB_E_CLASSNOTREG);

	IPersistStream * persist = nullptr;
	result = CoCreateInstance(classid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
		IID_IPersistStream, (void **)&persist);
	if (FAILED(result)) return(result);

	result = persist->Load(stream);
	if (SUCCEEDED(result)) {
		result = persist->QueryInterface(riid, object);
	}

	persist->Release();
	return(result);
}


extern "C" const IID IID_IPropertyStorage = {0x00000138, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const IID IID_IPropertySetStorage = {0x0000013A, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

void PropVariantInit(PROPVARIANT * value) { if (value != nullptr) memset(value, 0, sizeof(*value)); }
HRESULT PropVariantClear(PROPVARIANT * value) { if (value != nullptr) memset(value, 0, sizeof(*value)); return(S_OK); }


/*
** ---------------------------------------------------------------------------------------
** Locale formatting.
** ---------------------------------------------------------------------------------------
*/


/*
** The page's own locale stands in for the user locale these two are asked for, so a saved
** game is stamped the way the rest of the browser writes a date. A picture string is a
** different request and is not served; the engine passes none.
*/
int GetTimeFormatA(LCID, DWORD flags, SYSTEMTIME const * time, LPCSTR format, LPSTR text, int count)
{
	if (time == nullptr || text == nullptr || count <= 0) return(0);

	if (format != nullptr) {
		return(WIN32_UNSUPPORTED("GetTimeFormatA: a picture string of its own", 0));
	}

	bool const seconds = (flags & (TIME_NOSECONDS | TIME_NOMINUTESORSECONDS)) == 0;
	bool const minutes = (flags & TIME_NOMINUTESORSECONDS) == 0;

#if defined(__EMSCRIPTEN__)
	return(EM_ASM_INT({
		var options = {hour: "numeric"};
		if ($4) options.minute = "2-digit";
		if ($3) options.second = "2-digit";

		var text = new Date(2000, 0, 1, $0, $1, $2).toLocaleTimeString(undefined, options);
		var size = lengthBytesUTF8(text) + 1;

		if (size > $6) return 0;
		stringToUTF8(text, $5, $6);
		return size;
	}, time->wHour, time->wMinute, time->wSecond, seconds, minutes, text, count));
#else
	struct tm fields;
	memset(&fields, 0, sizeof(fields));
	fields.tm_year = 100;
	fields.tm_mday = 1;
	fields.tm_hour = time->wHour;
	fields.tm_min = time->wMinute;
	fields.tm_sec = time->wSecond;

	char const * picture = seconds ? "%H:%M:%S" : (minutes ? "%H:%M" : "%H");
	size_t const written = strftime(text, (size_t)count, picture, &fields);
	return(written != 0 ? (int)written + 1 : 0);
#endif
}


int GetDateFormatA(LCID, DWORD, SYSTEMTIME const * date, LPCSTR format, LPSTR text, int count)
{
	if (date == nullptr || text == nullptr || count <= 0) return(0);

	if (format != nullptr) {
		return(WIN32_UNSUPPORTED("GetDateFormatA: a picture string of its own", 0));
	}

#if defined(__EMSCRIPTEN__)
	return(EM_ASM_INT({
		var text = new Date($0, $1 - 1, $2).toLocaleDateString();
		var size = lengthBytesUTF8(text) + 1;

		if (size > $4) return 0;
		stringToUTF8(text, $3, $4);
		return size;
	}, date->wYear, date->wMonth, date->wDay, text, count));
#else
	struct tm fields;
	memset(&fields, 0, sizeof(fields));
	fields.tm_year = date->wYear - 1900;
	fields.tm_mon = date->wMonth - 1;
	fields.tm_mday = date->wDay;

	size_t const written = strftime(text, (size_t)count, "%x", &fields);
	return(written != 0 ? (int)written + 1 : 0);
#endif
}


/*
** ---------------------------------------------------------------------------------------
** Structured storage.
** ---------------------------------------------------------------------------------------
*/


extern "C" const FMTID FMTID_SummaryInformation = {0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}};
extern "C" const IID IID_IStorage = {0x0000000B, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

/*
** A storage is named in UTF-16 and the file layer underneath takes a narrow path. Every
** storage the engine opens is named by a path it built itself out of ASCII, so anything
** outside ASCII is refused rather than guessed at through a code page.
*/
static bool Narrow_Storage_Name(OLECHAR const * name, std::string & narrow)
{
	if (name == nullptr) return(false);

	narrow.clear();

	for (OLECHAR const * ptr = name; *ptr != 0; ptr++) {
		if ((unsigned short)*ptr > 0x7F) return(false);
		narrow.push_back((char)*ptr);
	}

	return(!narrow.empty());
}


HRESULT StgCreateDocfile(OLECHAR const * name, DWORD mode, DWORD reserved, IStorage ** storage)
{
	if (storage != nullptr) *storage = nullptr;
	if (reserved != 0) return(STG_E_INVALIDPARAMETER);

	std::string narrow;
	if (!Narrow_Storage_Name(name, narrow)) return(STG_E_INVALIDNAME);

	return(DocFile_Create(narrow.c_str(), mode, storage));
}


HRESULT StgOpenStorage(OLECHAR const * name, IStorage * priority, DWORD mode, void * exclude, DWORD reserved, IStorage ** storage)
{
	if (storage != nullptr) *storage = nullptr;
	if (priority != nullptr || exclude != nullptr || reserved != 0) return(STG_E_INVALIDPARAMETER);

	std::string narrow;
	if (!Narrow_Storage_Name(name, narrow)) return(STG_E_INVALIDNAME);

	return(DocFile_Open(narrow.c_str(), mode, storage));
}


HRESULT StgIsStorageFile(OLECHAR const * name)
{
	std::string narrow;
	if (!Narrow_Storage_Name(name, narrow)) return(STG_E_INVALIDNAME);

	return(DocFile_Is_Storage_File(narrow.c_str()));
}


/*
** ---------------------------------------------------------------------------------------
** Winsock.
** ---------------------------------------------------------------------------------------
*/


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


int WSACancelAsyncRequest(HANDLE) { return(WIN32_STUB(-1)); }


/*
** ---------------------------------------------------------------------------------------
** What is left.
** ---------------------------------------------------------------------------------------
*/


void OutputDebugStringA(LPCSTR string)
{
	if (string != nullptr) fputs(string, stderr);
}


/// <summary>
/// Translates a run of characters from the ANSI code page to the OEM one.
/// </summary>
/// <remarks>
/// The two code pages agree across ASCII, which is what the shipped game data holds, so
/// the run is copied through. A localized build wanting the high half translated needs a
/// real code page table here. Source and destination may be the same buffer, which several
/// callers rely on.
/// </remarks>
BOOL CharToOemBuffA(LPCSTR source, LPSTR destination, DWORD length)
{
	if (source == nullptr || destination == nullptr) {
		return(FALSE);
	}

	for (DWORD index = 0; index < length; index++) {
		destination[index] = source[index];
	}

	return(TRUE);
}


HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int) { return(WIN32_STUB((HINSTANCE)nullptr)); }


BOOL DeviceIoControl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD returned, LPOVERLAPPED) { if (returned != nullptr) *returned = 0; return(WIN32_STUB(FALSE)); }


DWORD FormatMessageA(DWORD, LPCVOID, DWORD, DWORD, LPSTR buffer, DWORD size, va_list *) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }


DWORD GetAdaptersInfo(PIP_ADAPTER_INFO, PULONG size) { if (size != nullptr) *size = 0; return(WIN32_STUB(ERROR_BUFFER_OVERFLOW)); }

#endif	// __EMSCRIPTEN__
