/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stands in for the MSVC C runtime headers the tree includes -- <io.h>, <direct.h>,
// <conio.h>, <new.h>, <sal.h> -- and for the MSVC spellings of the string and path
// helpers. always.h includes this, so every translation unit has it.
//
// Everything here is a real implementation, mapped onto POSIX or written out, not a
// stub: these are library routines with defined behavior rather than operating system
// services. The Win32 API stubs live in win32compat.h.
//
// The whole file is inert outside the WebAssembly target; MSVC keeps its own headers.

#pragma once

#if !defined(_WIN32)

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


/*
** MSVC's sized integer keywords. wasm32 is ILP32 like Win32 x86, so each of these has
** the width the inherited declarations were written against.
*/
#define __int8		char
#define __int16		short
#define __int32		int
#define __int64		long long

#define __forceinline	inline __attribute__((always_inline))

/*
** The single-underscore calling-convention spellings. The build erases the double
** underscore forms on the command line; these are the ones the older files use.
*/
#define _cdecl
#define _stdcall
#define _fastcall

/*
** The one SAL annotation the tree uses. SAL is an MSVC static-analysis vocabulary with
** no run-time meaning, so dropping it changes nothing that is compiled.
*/
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif

/*
** <io.h> and <fcntl.h> spellings. Windows distinguishes text and binary streams and
** POSIX does not, so the mode bits that pick between them are the identity here.
*/
#ifndef O_BINARY
#define O_BINARY	0
#endif
#ifndef O_TEXT
#define O_TEXT		0
#endif
#ifndef O_RAW
#define O_RAW		O_BINARY
#endif

#ifndef S_IREAD
#define S_IREAD		S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE	S_IWUSR
#endif

/*
** The MSVC guard the MIDL-generated headers test before declaring their own time_t.
*/
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#endif

#ifndef _MAX_PATH
#define _MAX_PATH	260
#define _MAX_DRIVE	3
#define _MAX_DIR	256
#define _MAX_FNAME	256
#define _MAX_EXT	256
#endif

/*
** MSVC's <ctype.h> character-class masks, which the INI parser compares against.
*/
#ifndef _CONTROL
#define _UPPER		0x0001
#define _LOWER		0x0002
#define _DIGIT		0x0004
#define _SPACE		0x0008
#define _PUNCT		0x0010
#define _CONTROL	0x0020
#define _BLANK		0x0040
#define _HEX		0x0080
#endif

#ifndef _A_NORMAL
#define _A_NORMAL	0x00
#define _A_RDONLY	0x01
#define _A_HIDDEN	0x02
#define _A_SYSTEM	0x04
#define _A_SUBDIR	0x10
#define _A_ARCH		0x20
#endif


/*
** Case-insensitive comparison. MSVC spells these without the underscore that POSIX
** uses; the semantics are the same.
*/
inline int stricmp(char const * left, char const * right)
{
	return(strcasecmp(left, right));
}


inline int strnicmp(char const * left, char const * right, size_t count)
{
	return(strncasecmp(left, right, count));
}


inline int memicmp(void const * left, void const * right, size_t count)
{
	unsigned char const * l = (unsigned char const *)left;
	unsigned char const * r = (unsigned char const *)right;

	for (size_t index = 0; index < count; index++) {
		int diff = tolower(l[index]) - tolower(r[index]);
		if (diff != 0) return(diff);
	}
	return(0);
}


inline char * strupr(char * string)
{
	for (char * ptr = string; *ptr != '\0'; ptr++) {
		*ptr = (char)toupper((unsigned char)*ptr);
	}
	return(string);
}


inline char * strlwr(char * string)
{
	for (char * ptr = string; *ptr != '\0'; ptr++) {
		*ptr = (char)tolower((unsigned char)*ptr);
	}
	return(string);
}


/*
** Reverse in place. No standard or POSIX equivalent exists, so this is written out.
*/
inline char * strrev(char * string)
{
	char * front = string;
	char * back = string + strlen(string);

	while (back > front) {
		back--;
		char swap = *front;
		*front = *back;
		*back = swap;
		front++;
	}
	return(string);
}


char * itoa(int value, char * buffer, int radix);
char * ltoa(long value, char * buffer, int radix);
char * ultoa(unsigned long value, char * buffer, int radix);

/*
** MSVC path decomposition, over DOS path syntax: a drive letter followed by a colon, a
** directory ending in its final separator, a base name, and an extension including its
** dot. Both separators are recognized, as they are under Windows. Any output pointer
** may be null, in which case that component is discarded.
*/
void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext);
void _makepath(char * path, char const * drive, char const * dir, char const * fname, char const * ext);

/*
** MSVC's <sys/timeb.h> underscore spellings, over the host's struct.
*/
struct _timeb {
	long time;
	unsigned short millitm;
	short timezone;
	short dstflag;
};


inline void _ftime(struct _timeb * result)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	result->time = (long)now.tv_sec;
	result->millitm = (unsigned short)(now.tv_nsec / 1000000);
	result->timezone = 0;
	result->dstflag = 0;
}


inline long filelength(int handle)
{
	struct stat info;

	if (fstat(handle, &info) != 0) return(-1L);
	return((long)info.st_size);
}


inline int freopen_s(FILE ** stream, char const * filename, char const * mode, FILE * old)
{
	FILE * result = freopen(filename, mode, old);

	if (stream != nullptr) *stream = result;
	return(result != nullptr ? 0 : errno);
}


/*
** <conio.h>. There is no console to read a key from.
*/
int _getch(void);


inline size_t _msize(void * block)
{
#if defined(__APPLE__)
	return(malloc_size(block));
#else
	return(malloc_usable_size(block));
#endif
}


#endif	// __EMSCRIPTEN__
