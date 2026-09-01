/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stands in for <windows.h> and the COM headers on the WebAssembly target, which has no
// Windows SDK. Every site that includes a Win32 header selects this instead when
// __EMSCRIPTEN__ is defined; the MSVC build never sees this file and its include set is
// unchanged.
//
// Two properties matter more than completeness.
//
// Layout. wasm32 is ILP32 exactly as Win32 x86 is, and that equivalence is why the
// engine's structures survive the move. Every type below therefore carries the width,
// signedness, and alignment of its Win32 x86 original: DWORD is unsigned long, LONG is
// long, WPARAM and LPARAM are pointer sized, BOOL is int, RECT is four LONGs. A type
// that drifts here changes the shape of a saved game or a network packet silently.
//
// Honesty. Almost nothing here implements Windows. Most entry points are stubs that
// report themselves and then return the value their Win32 originals return on failure,
// so a port that reaches one fails visibly instead of proceeding on a lie. See
// WIN32_STUB below. What the host can genuinely answer is answered instead of stubbed,
// and says so: the clocks, the string helpers, and the file entry points -- CreateFileA
// through the FindFirstFileA family. Those are adapters: filesystem.h owns the file layer
// itself and decides whether a name is a host file or an entry on a mounted disc, so the
// engine gets the same answer whether it opens through RawFileClass or through here. A
// request they cannot honestly serve, such as an unhandled creation disposition, reports
// itself through WIN32_UNSUPPORTED and fails rather than approximating.

#pragma once

#if !defined(_WIN32)

#include "crtcompat.h"
#include "iso9660.hh"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>


/*
** How an unimplemented Win32 entry point announces itself.
**
** WIN32_STUB(value)  reports the call and yields `value`, which is always what the Win32
**                    function returns to say it failed.
** WIN32_STUB_VOID()  reports the call for an entry point that returns nothing.
** WIN32_STUB_ABORT() reports the call and terminates. It is used where no return value
**                    could carry the failure, so that continuing would mean the caller
**                    acting on a result that was never produced.
**
** Reporting is once per entry point rather than once per call, so a stub inside a frame
** loop names itself without burying the log.
*/
void Win32_Stub_Reached(char const * function);
[[noreturn]] void Win32_Stub_Fatal(char const * function);

#define WIN32_STUB(value)	(::Win32_Stub_Reached(__func__), (value))
#define WIN32_STUB_VOID()	(::Win32_Stub_Reached(__func__))
#define WIN32_STUB_ABORT()	(::Win32_Stub_Fatal(__func__))

/*
** How an implemented entry point announces a request it cannot honestly serve.
**
** An entry point that is real for the cases the engine uses still meets arguments it has
** no answer for -- an overlapped read, a creation disposition nobody wrote a mapping for.
** WIN32_UNSUPPORTED names the request and yields the Win32 failure value, so the gap is
** as visible as a missing entry point rather than being approximated into a wrong result.
** `description` must be a literal naming the entry point and the request; reporting is
** once per description.
*/
void Win32_Unsupported_Reached(char const * description);

#define WIN32_UNSUPPORTED(description, value)	(::Win32_Unsupported_Reached(description), (value))


/*
** The read-only half of the filesystem.
**
** A page has no host directory to run out of, so the file layer looks a name it cannot
** resolve up inside a mounted ISO9660 image instead. The images sit underneath the host,
** never over it: a name both can answer resolves to the host's copy, which is what lets a
** file the engine writes shadow the one it shipped with.
**
** A game comes on more than one disc, so several images may be mounted at once. They are
** searched in the order they were mounted, each contributing its installed data directory
** ahead of its own root, and the first that carries a name answers for it; a wildcard search
** reports what all of them hold together. Mounting appends, so the caller's order is the
** search order and the disc whose copies should win is mounted first.
**
** The images named by isohttp.h are mounted on the first file the host cannot answer for.
** These are for a caller that needs to choose them itself, such as a test harness.
*/
bool Win32_Mount_Image(char const * location);
void Win32_Unmount_Image(void);

/// <summary>Says what a run of a file on a mounted image is about to be used for.</summary>
/// <param name="filename">The name the engine would open.</param>
/// <param name="kind">Whether the run is being read now or may be wanted later.</param>
/// <param name="offset">Byte offset within the file.</param>
/// <param name="length">How many bytes it covers, or 0 for the rest of the file.</param>
/// <returns>bool; Did the name resolve to a file on a mounted image?</returns>
/// <remarks>Advisory. It exists because the image knows what an open cannot say for
/// itself: a name the engine has not opened yet still has a place on the disc, and a
/// distant server is worth telling about it while there is nothing else to fetch.</remarks>
bool Win32_Hint_File(char const * filename, ISOHintType kind, unsigned int offset, unsigned int length);


/*
** Calling conventions and linkage decoration. WebAssembly has one calling convention;
** the build already erases __cdecl, __stdcall, and __fastcall on the command line.
*/
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define STDMETHODCALLTYPE
#define STDAPICALLTYPE
#define PASCAL
#define FAR
#define NEAR
#define far
#define near
#define CONST			const
#define WINGDIAPI
#define DECLSPEC_IMPORT
#define STDAPI			HRESULT
#define STDAPI_(type)	type
#define EXTERN_C		extern "C"


/*
** The fundamental Win32 scalar types, at their Win32 x86 widths. Spelled with the
** fixed-width types so an LP64 host keeps them at those widths.
*/
typedef uint32_t		DWORD;
typedef unsigned short	WORD;
typedef unsigned char	BYTE;
typedef int				BOOL;
typedef int				INT;
typedef unsigned int	UINT;
typedef int32_t			LONG;
typedef uint32_t		ULONG;
typedef short			SHORT;
typedef unsigned short	USHORT;
typedef char			CHAR;
typedef unsigned char	UCHAR;
typedef float			FLOAT;
typedef void			VOID;
typedef unsigned char	BOOLEAN;
typedef unsigned char	boolean;
typedef wchar_t			WCHAR;
typedef WCHAR			OLECHAR;

typedef long long			LONGLONG;
typedef unsigned long long	ULONGLONG;
typedef unsigned long long	DWORDLONG;
typedef unsigned int		DWORD32;
typedef unsigned long long	DWORD64;

/*
** The pointer-sized integers, which must hold a pointer on every host.
*/
typedef intptr_t		INT_PTR;
typedef uintptr_t		UINT_PTR;
typedef intptr_t		LONG_PTR;
typedef uintptr_t		ULONG_PTR;
typedef ULONG_PTR		DWORD_PTR;
typedef ULONG_PTR		SIZE_T;
typedef LONG_PTR		SSIZE_T;

typedef void *			PVOID;
typedef void *			LPVOID;
typedef void const *	LPCVOID;
typedef char *			LPSTR;
typedef char const *	LPCSTR;
typedef char *			PSTR;
typedef char const *	PCSTR;
typedef WCHAR *			LPWSTR;
typedef WCHAR const *	LPCWSTR;
typedef OLECHAR *		LPOLESTR;
typedef OLECHAR const *	LPCOLESTR;
typedef LPSTR			LPTSTR;
typedef LPCSTR			LPCTSTR;
typedef CHAR			TCHAR;
typedef BYTE *			PBYTE;
typedef BYTE *			LPBYTE;
typedef WORD *			PWORD;
typedef DWORD *			PDWORD;
typedef LONG *			PLONG;
typedef ULONG *			PULONG;
typedef INT *			PINT;
typedef UINT *			PUINT;
typedef BOOL *			PBOOL;
typedef CHAR *			PCHAR;
typedef float *			PFLOAT;
typedef WORD *			LPWORD;
typedef DWORD *			LPDWORD;
typedef LONG *			LPLONG;
typedef INT *			LPINT;
typedef BOOL *			LPBOOL;

typedef UINT_PTR		WPARAM;
typedef LONG_PTR		LPARAM;
typedef LONG_PTR		LRESULT;
typedef int32_t			HRESULT;
typedef LONG			SCODE;
typedef DWORD			COLORREF;
typedef DWORD *			LPCOLORREF;
typedef WORD			ATOM;
typedef WORD			LANGID;
typedef DWORD			LCID;


/*
** Handles. Win32 gives each kind its own incomplete struct so the compiler keeps them
** apart; the same trick is used here, at the same four-byte width.
*/
typedef void * HANDLE;
typedef HANDLE * PHANDLE;
typedef HANDLE * LPHANDLE;

#define DECLARE_HANDLE(name)	struct name##__ { int unused; }; typedef struct name##__ * name

DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HGLOBAL__);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HRSRC);

typedef void *		HGDIOBJ;
typedef HICON		HCURSOR;
typedef HINSTANCE	HMODULE;
typedef HANDLE		HGLOBAL;
typedef HANDLE		HLOCAL;
typedef HKEY *		PHKEY;

#define HFILE			int
#define HFILE_ERROR		((HFILE)-1)


/*
** Callback signatures.
*/
typedef LRESULT (CALLBACK * WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (CALLBACK * DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void (CALLBACK * TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef BOOL (CALLBACK * WNDENUMPROC)(HWND, LPARAM);
typedef INT_PTR (WINAPI * FARPROC)();
typedef INT_PTR (WINAPI * PROC)();
typedef DWORD (WINAPI * LPTHREAD_START_ROUTINE)(LPVOID);


/*
** Structures. Each matches its Win32 x86 original field for field.
*/
typedef struct tagPOINT {
	LONG x;
	LONG y;
} POINT, * PPOINT, * LPPOINT;

typedef struct tagSIZE {
	LONG cx;
	LONG cy;
} SIZE, * PSIZE, * LPSIZE;

typedef struct tagRECT {
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
} RECT, * PRECT, * LPRECT;
typedef RECT const * LPCRECT;

typedef struct tagMSG {
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
} MSG, * PMSG, * LPMSG;

#define GUID_DEFINED
#define __IID_DEFINED__
#define CLSID_DEFINED
#define _REFGUID_DEFINED
#define _REFIID_DEFINED
#define _REFCLSID_DEFINED

typedef struct _GUID {
	uint32_t Data1;
	unsigned short Data2;
	unsigned short Data3;
	unsigned char Data4[8];
} GUID;

typedef GUID IID;
typedef GUID CLSID;
typedef GUID UUID;
typedef GUID const & REFGUID;
typedef IID const & REFIID;
typedef CLSID const & REFCLSID;
typedef GUID * LPGUID;
typedef CLSID * LPCLSID;

inline bool operator == (GUID const & left, GUID const & right)
{
	if (left.Data1 != right.Data1 || left.Data2 != right.Data2 || left.Data3 != right.Data3) return(false);
	for (int index = 0; index < 8; index++) {
		if (left.Data4[index] != right.Data4[index]) return(false);
	}
	return(true);
}


inline bool operator != (GUID const & left, GUID const & right)
{
	return(!(left == right));
}

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, * PFILETIME, * LPFILETIME;

typedef struct _SYSTEMTIME {
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
} SYSTEMTIME, * PSYSTEMTIME, * LPSYSTEMTIME;

/*
** The 64-bit integer unions. MSVC aligns __int64 to eight bytes on x86 and clang aligns
** long long the same way on wasm32, so both the anonymous field pair and the union as a
** whole keep their Win32 offsets and size.
*/
typedef union _LARGE_INTEGER {
	struct {
		DWORD LowPart;
		LONG HighPart;
	};
	struct {
		DWORD LowPart;
		LONG HighPart;
	} u;
	LONGLONG QuadPart;
} LARGE_INTEGER, * PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
	struct {
		DWORD LowPart;
		DWORD HighPart;
	};
	struct {
		DWORD LowPart;
		DWORD HighPart;
	} u;
	ULONGLONG QuadPart;
} ULARGE_INTEGER, * PULARGE_INTEGER;

typedef struct _SECURITY_ATTRIBUTES {
	DWORD nLength;
	LPVOID lpSecurityDescriptor;
	BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, * PSECURITY_ATTRIBUTES, * LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	DWORD Offset;
	DWORD OffsetHigh;
	HANDLE hEvent;
} OVERLAPPED, * LPOVERLAPPED;

/*
** RTL_CRITICAL_SECTION is twenty-four bytes on Win32 x86 and is reproduced at that size
** rather than wrapped around a host primitive, because the engine embeds it in objects
** whose layout is fixed.
*/
typedef struct _RTL_CRITICAL_SECTION {
	void * DebugInfo;
	LONG LockCount;
	LONG RecursionCount;
	HANDLE OwningThread;
	HANDLE LockSemaphore;
	ULONG_PTR SpinCount;
} CRITICAL_SECTION, RTL_CRITICAL_SECTION, * LPCRITICAL_SECTION, * PRTL_CRITICAL_SECTION;

typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	CHAR cFileName[260];
	CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, * PWIN32_FIND_DATAA, * LPWIN32_FIND_DATAA, * LPWIN32_FIND_DATA;

typedef struct _BY_HANDLE_FILE_INFORMATION {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD dwVolumeSerialNumber;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD nNumberOfLinks;
	DWORD nFileIndexHigh;
	DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, * LPBY_HANDLE_FILE_INFORMATION;

typedef struct _OSVERSIONINFOA {
	DWORD dwOSVersionInfoSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformId;
	CHAR szCSDVersion[128];
} OSVERSIONINFOA, OSVERSIONINFO, * LPOSVERSIONINFOA, * LPOSVERSIONINFO;

typedef struct _SYSTEM_INFO {
	DWORD dwOemId;
	DWORD dwPageSize;
	LPVOID lpMinimumApplicationAddress;
	LPVOID lpMaximumApplicationAddress;
	DWORD_PTR dwActiveProcessorMask;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwAllocationGranularity;
	WORD wProcessorLevel;
	WORD wProcessorRevision;
} SYSTEM_INFO, * LPSYSTEM_INFO;

typedef struct _MEMORYSTATUS {
	DWORD dwLength;
	DWORD dwMemoryLoad;
	SIZE_T dwTotalPhys;
	SIZE_T dwAvailPhys;
	SIZE_T dwTotalPageFile;
	SIZE_T dwAvailPageFile;
	SIZE_T dwTotalVirtual;
	SIZE_T dwAvailVirtual;
} MEMORYSTATUS, * LPMEMORYSTATUS;

typedef struct tagPAINTSTRUCT {
	HDC hdc;
	BOOL fErase;
	RECT rcPaint;
	BOOL fRestore;
	BOOL fIncUpdate;
	BYTE rgbReserved[32];
} PAINTSTRUCT, * LPPAINTSTRUCT;

typedef struct tagWNDCLASSA {
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
} WNDCLASSA, WNDCLASS, * LPWNDCLASSA, * LPWNDCLASS;

typedef struct tagWNDCLASSEXA {
	UINT cbSize;
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
	HICON hIconSm;
} WNDCLASSEXA, WNDCLASSEX, * LPWNDCLASSEXA, * LPWNDCLASSEX;

/*
** What WM_NCCREATE and WM_CREATE carry. The field order is Win32's own and is a
** compatibility boundary in itself: ownrdraw.cpp reads the creation parameter as
** *(HWND *)lParam, which is only the same thing as lpCreateParams while that member
** stays first.
*/
typedef struct tagCREATESTRUCTA {
	LPVOID lpCreateParams;
	HINSTANCE hInstance;
	HMENU hMenu;
	HWND hwndParent;
	int cy;
	int cx;
	int y;
	int x;
	LONG style;
	LPCSTR lpszName;
	LPCSTR lpszClass;
	DWORD dwExStyle;
} CREATESTRUCTA, CREATESTRUCT, * LPCREATESTRUCTA, * LPCREATESTRUCT;

typedef struct tagDRAWITEMSTRUCT {
	UINT CtlType;
	UINT CtlID;
	UINT itemID;
	UINT itemAction;
	UINT itemState;
	HWND hwndItem;
	HDC hDC;
	RECT rcItem;
	ULONG_PTR itemData;
} DRAWITEMSTRUCT, * LPDRAWITEMSTRUCT;

typedef struct tagMEASUREITEMSTRUCT {
	UINT CtlType;
	UINT CtlID;
	UINT itemID;
	UINT itemWidth;
	UINT itemHeight;
	ULONG_PTR itemData;
} MEASUREITEMSTRUCT, * LPMEASUREITEMSTRUCT;

/*
** WAVEFORMATEX sits inside mmsystem.h's one-byte packing region, so it is eighteen bytes
** rather than the twenty its members would otherwise round up to. The packing is
** reproduced here because the structure is written into audio buffers.
*/
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
	WORD wFormatTag;
	WORD nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD nBlockAlign;
	WORD wBitsPerSample;
	WORD cbSize;
} WAVEFORMATEX, * LPWAVEFORMATEX;
#pragma pack(pop)


/*
** Constants.
*/
#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

#define MAX_PATH				260
#define INVALID_HANDLE_VALUE	((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE		((DWORD)0xFFFFFFFF)
#define INVALID_FILE_ATTRIBUTES	((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INFINITE				0xFFFFFFFF

#define WAIT_OBJECT_0			0x00000000L
#define WAIT_ABANDONED			0x00000080L
#define WAIT_TIMEOUT			0x00000102L
#define WAIT_FAILED				0xFFFFFFFF
#define MAXIMUM_WAIT_OBJECTS	64

#define GENERIC_READ			0x80000000L
#define GENERIC_WRITE			0x40000000L
#define GENERIC_EXECUTE			0x20000000L
#define GENERIC_ALL				0x10000000L

#define FILE_SHARE_READ			0x00000001
#define FILE_SHARE_WRITE		0x00000002
#define FILE_SHARE_DELETE		0x00000004

#define CREATE_NEW				1
#define CREATE_ALWAYS			2
#define OPEN_EXISTING			3
#define OPEN_ALWAYS				4
#define TRUNCATE_EXISTING		5

#define FILE_ATTRIBUTE_READONLY		0x00000001
#define FILE_ATTRIBUTE_HIDDEN		0x00000002
#define FILE_ATTRIBUTE_SYSTEM		0x00000004
#define FILE_ATTRIBUTE_DIRECTORY	0x00000010
#define FILE_ATTRIBUTE_ARCHIVE		0x00000020
#define FILE_ATTRIBUTE_NORMAL		0x00000080
#define FILE_ATTRIBUTE_TEMPORARY	0x00000100
#define FILE_ATTRIBUTE_OFFLINE		0x00001000
#define FILE_FLAG_WRITE_THROUGH		0x80000000
#define FILE_FLAG_RANDOM_ACCESS		0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN	0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE	0x04000000
#define FILE_BEGIN				0
#define FILE_CURRENT			1
#define FILE_END				2


#define NO_ERROR					0L
#define ERROR_SUCCESS				0L
#define ERROR_FILE_NOT_FOUND		2L
#define ERROR_PATH_NOT_FOUND		3L
#define ERROR_TOO_MANY_OPEN_FILES	4L
#define ERROR_ACCESS_DENIED			5L
#define ERROR_INVALID_HANDLE		6L
#define ERROR_NOT_ENOUGH_MEMORY		8L
#define ERROR_READ_FAULT			30L
#define ERROR_GEN_FAILURE			31L
#define ERROR_SEEK					25L
#define ERROR_NEGATIVE_SEEK			131L
#define ERROR_FILE_EXISTS			80L
#define ERROR_DISK_FULL				112L
#define ERROR_DIRECTORY				267L
#define ERROR_NOT_SUPPORTED			50L
#define ERROR_INVALID_PARAMETER		87L
#define ERROR_CALL_NOT_IMPLEMENTED	120L
#define ERROR_INSUFFICIENT_BUFFER	122L
#define ERROR_ALREADY_EXISTS		183L
#define ERROR_NOT_OWNER				288L
#define ERROR_NO_MORE_FILES			18L
#define ERROR_HANDLE_EOF			38L
#define ERROR_IO_PENDING			997L

#define S_OK			((HRESULT)0L)
#define S_FALSE			((HRESULT)1L)
#define NOERROR			((HRESULT)0L)
#define E_UNEXPECTED	((HRESULT)0x8000FFFFL)
#define E_NOTIMPL		((HRESULT)0x80004001L)
#define E_OUTOFMEMORY	((HRESULT)0x8007000EL)
#define E_INVALIDARG	((HRESULT)0x80070057L)
#define E_NOINTERFACE	((HRESULT)0x80004002L)
#define E_POINTER		((HRESULT)0x80004003L)
#define E_HANDLE		((HRESULT)0x80070006L)
#define E_ABORT			((HRESULT)0x80004004L)
#define E_FAIL			((HRESULT)0x80004005L)
#define E_ACCESSDENIED	((HRESULT)0x80070005L)
#define E_PENDING		((HRESULT)0x8000000AL)

#define SUCCEEDED(hr)	(((HRESULT)(hr)) >= 0)
#define FAILED(hr)		(((HRESULT)(hr)) < 0)
#define HRESULT_CODE(hr)	((hr) & 0xFFFF)
#define MAKE_HRESULT(sev, fac, code) \
	((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))

#define CLSCTX_INPROC_SERVER	0x1
#define CLSCTX_INPROC_HANDLER	0x2
#define CLSCTX_LOCAL_SERVER		0x4
#define CLSCTX_ALL				(CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER)

#define STGTY_STORAGE	1
#define STGTY_STREAM	2
#define STGTY_LOCKBYTES	3
#define STGTY_PROPERTY	4

#define STREAM_SEEK_SET	0
#define STREAM_SEEK_CUR	1
#define STREAM_SEEK_END	2

#define MB_OK					0x00000000L
#define MB_OKCANCEL				0x00000001L
#define MB_ABORTRETRYIGNORE		0x00000002L
#define MB_YESNOCANCEL			0x00000003L
#define MB_YESNO				0x00000004L
#define MB_RETRYCANCEL			0x00000005L
#define MB_ICONHAND				0x00000010L
#define MB_ICONQUESTION			0x00000020L
#define MB_ICONEXCLAMATION		0x00000030L
#define MB_ICONASTERISK			0x00000040L
#define MB_ICONERROR			MB_ICONHAND
#define MB_ICONSTOP				MB_ICONHAND
#define MB_ICONWARNING			MB_ICONEXCLAMATION
#define MB_ICONINFORMATION		MB_ICONASTERISK
#define MB_DEFBUTTON1			0x00000000L
#define MB_DEFBUTTON2			0x00000100L
#define MB_SYSTEMMODAL			0x00001000L
#define MB_TASKMODAL			0x00002000L
#define MB_TOPMOST				0x00040000L
#define MB_SETFOREGROUND		0x00010000L

#define IDOK		1
#define IDCANCEL	2
#define IDABORT		3
#define IDRETRY		4
#define IDIGNORE	5
#define IDYES		6
#define IDNO		7

#define SW_HIDE				0
#define SW_SHOWNORMAL		1
#define SW_NORMAL			1
#define SW_SHOWMINIMIZED	2
#define SW_SHOWMAXIMIZED	3
#define SW_MAXIMIZE			3
#define SW_SHOWNOACTIVATE	4
#define SW_SHOW				5
#define SW_MINIMIZE			6
#define SW_SHOWMINNOACTIVE	7
#define SW_SHOWNA			8
#define SW_RESTORE			9
#define SW_SHOWDEFAULT		10

#define GWL_WNDPROC		(-4)
#define GWL_HINSTANCE	(-6)
#define GWL_HWNDPARENT	(-8)
#define GWL_STYLE		(-16)
#define GWL_EXSTYLE		(-20)
#define GWL_USERDATA	(-21)
#define GWL_ID			(-12)
#define GWLP_WNDPROC	(-4)
#define GWLP_USERDATA	(-21)
#define GWLP_HINSTANCE	(-6)
#define GWLP_HWNDPARENT	(-8)
#define GWLP_ID			(-12)
#define DWL_MSGRESULT	0
#define DWL_DLGPROC		4
#define DWL_USER		8
#define DWLP_MSGRESULT	0
#define DWLP_DLGPROC	((int)sizeof(LRESULT))
#define DWLP_USER		((int)(DWLP_DLGPROC + sizeof(LONG_PTR)))

#define SM_CXSCREEN			0
#define SM_CYSCREEN			1
#define SM_CXVSCROLL		2
#define SM_CYHSCROLL		3
#define SM_CYCAPTION		4
#define SM_CXBORDER			5
#define SM_CYBORDER			6
#define SM_CXFIXEDFRAME		7
#define SM_CYFIXEDFRAME		8
#define SM_CXFULLSCREEN		16
#define SM_CYFULLSCREEN		17
#define SM_CXSIZEFRAME		32
#define SM_CYSIZEFRAME		33
#define SM_CMOUSEBUTTONS	43

#define SWP_NOSIZE			0x0001
#define SWP_NOMOVE			0x0002
#define SWP_NOZORDER		0x0004
#define SWP_NOREDRAW		0x0008
#define SWP_NOACTIVATE		0x0010
#define SWP_SHOWWINDOW		0x0040
#define SWP_HIDEWINDOW		0x0080
#define SWP_FRAMECHANGED	0x0020

#define HWND_TOP		((HWND)0)
#define HWND_BOTTOM		((HWND)1)
#define HWND_TOPMOST	((HWND)-1)
#define HWND_NOTOPMOST	((HWND)-2)
#define HWND_DESKTOP	((HWND)0)
#define HWND_BROADCAST	((HWND)0xffff)

#define PM_NOREMOVE		0x0000
#define PM_REMOVE		0x0001
#define PM_NOYIELD		0x0002

#define CS_VREDRAW		0x0001
#define CS_HREDRAW		0x0002
#define CS_DBLCLKS		0x0008
#define CS_OWNDC		0x0020
#define CS_CLASSDC		0x0040
#define CS_SAVEBITS		0x0800

#define IDC_ARROW		((LPCSTR)32512)
#define IDC_IBEAM		((LPCSTR)32513)
#define IDC_WAIT		((LPCSTR)32514)
#define IDC_CROSS		((LPCSTR)32515)
#define IDI_APPLICATION	((LPCSTR)32512)
#define IDI_INFORMATION	((LPCSTR)32516)

#define MK_LBUTTON		0x0001
#define MK_RBUTTON		0x0002
#define MK_SHIFT		0x0004
#define MK_CONTROL		0x0008
#define MK_MBUTTON		0x0010

#define WM_NULL			0x0000
#define WM_CREATE		0x0001
#define WM_DESTROY		0x0002
#define WM_MOVE			0x0003
#define WM_SIZE			0x0005
#define WM_ACTIVATE		0x0006
#define WM_SETFOCUS		0x0007
#define WM_KILLFOCUS	0x0008
#define WM_ENABLE		0x000A
#define WM_SETREDRAW	0x000B
#define WM_SETTEXT		0x000C
#define WM_GETTEXT		0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT		0x000F
#define WM_CLOSE		0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT			0x0012
#define WM_ERASEBKGND	0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SHOWWINDOW	0x0018
#define WM_ACTIVATEAPP	0x001C
#define WM_SETCURSOR	0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT		0x0030
#define WM_GETFONT		0x0031
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE		0x0081
#define WM_NCDESTROY	0x0082
#define WM_NCHITTEST	0x0084
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_KEYFIRST		0x0100
#define WM_KEYDOWN		0x0100
#define WM_KEYUP		0x0101
#define WM_CHAR			0x0102
#define WM_DEADCHAR		0x0103
#define WM_SYSKEYDOWN	0x0104
#define WM_SYSKEYUP		0x0105
#define WM_SYSCHAR		0x0106
#define WM_KEYLAST		0x0108
#define WM_INITDIALOG	0x0110
#define WM_COMMAND		0x0111
#define WM_SYSCOMMAND	0x0112
#define WM_TIMER		0x0113
#define WM_HSCROLL		0x0114
#define WM_VSCROLL		0x0115
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT	0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN	0x0135
#define WM_CTLCOLORDLG	0x0136
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEFIRST	0x0200
#define WM_MOUSEMOVE	0x0200
#define WM_LBUTTONDOWN	0x0201
#define WM_LBUTTONUP	0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN	0x0204
#define WM_RBUTTONUP	0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN	0x0207
#define WM_MBUTTONUP	0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL	0x020A
#define WM_MOUSELAST	0x020A
#define WM_MOVING		0x0216
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE	0x0232
#define WM_DRAWITEM		0x002B
#define WM_MEASUREITEM	0x002C
#define WM_DELETEITEM	0x002D
#define WM_VKEYTOITEM	0x002E
#define WM_CHARTOITEM	0x002F
#define WM_DISPLAYCHANGE 0x007E
#define WM_USER			0x0400
#define WM_APP			0x8000

#define WS_OVERLAPPED	0x00000000L
#define WS_POPUP		0x80000000L
#define WS_CHILD		0x40000000L
#define WS_MINIMIZE		0x20000000L
#define WS_VISIBLE		0x10000000L
#define WS_DISABLED		0x08000000L
#define WS_CLIPSIBLINGS	0x04000000L
#define WS_CLIPCHILDREN	0x02000000L
#define WS_MAXIMIZE		0x01000000L
#define WS_CAPTION		0x00C00000L
#define WS_BORDER		0x00800000L
#define WS_DLGFRAME		0x00400000L
#define WS_VSCROLL		0x00200000L
#define WS_HSCROLL		0x00100000L
#define WS_SYSMENU		0x00080000L
#define WS_THICKFRAME	0x00040000L
#define WS_GROUP		0x00020000L
#define WS_TABSTOP		0x00010000L
#define WS_MINIMIZEBOX	0x00020000L
#define WS_MAXIMIZEBOX	0x00010000L
#define WS_OVERLAPPEDWINDOW \
	(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW	(WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST	0x00000008L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_CLIENTEDGE 0x00000200L

#define CW_USEDEFAULT	((int)0x80000000)

#define BM_GETCHECK		0x00F0
#define BM_SETCHECK		0x00F1
#define BM_GETSTATE		0x00F2
#define BM_SETSTATE		0x00F3
#define BST_UNCHECKED	0x0000
#define BST_CHECKED		0x0001

#define LB_ADDSTRING		0x0180
#define LB_INSERTSTRING		0x0181
#define LB_DELETESTRING		0x0182
#define LB_RESETCONTENT		0x0184
#define LB_SETSEL			0x0185
#define LB_SETCURSEL		0x0186
#define LB_GETSEL			0x0187
#define LB_GETCURSEL		0x0188
#define LB_GETTEXT			0x0189
#define LB_GETTEXTLEN		0x018A
#define LB_GETCOUNT			0x018B
#define LB_SELECTSTRING		0x018C
#define LB_GETTOPINDEX		0x018E
#define LB_FINDSTRING		0x018F
#define LB_SETITEMDATA		0x019A
#define LB_GETITEMDATA		0x0199
#define LB_SETTOPINDEX		0x0197
#define LB_ERR				(-1)
#define LB_ERRSPACE			(-2)

#define CB_ADDSTRING		0x0143
#define CB_RESETCONTENT		0x014B
#define CB_SETCURSEL		0x014E
#define CB_GETCURSEL		0x0147
#define CB_ERR				(-1)

#define SB_LINEUP		0
#define SB_LINEDOWN		1
#define SB_PAGEUP		2
#define SB_PAGEDOWN		3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK	5
#define SB_TOP			6
#define SB_BOTTOM		7
#define SB_ENDSCROLL	8

#define HKEY_CLASSES_ROOT		((HKEY)(ULONG_PTR)0x80000000)
#define HKEY_CURRENT_USER		((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE		((HKEY)(ULONG_PTR)0x80000002)
#define HKEY_USERS				((HKEY)(ULONG_PTR)0x80000003)

#define KEY_QUERY_VALUE		0x0001
#define KEY_SET_VALUE		0x0002
#define KEY_READ			0x20019
#define KEY_WRITE			0x20006
#define KEY_ALL_ACCESS		0xF003F

#define REG_NONE		0
#define REG_SZ			1
#define REG_EXPAND_SZ	2
#define REG_BINARY		3
#define REG_DWORD		4

#define VER_PLATFORM_WIN32s			0
#define VER_PLATFORM_WIN32_WINDOWS	1
#define VER_PLATFORM_WIN32_NT		2

#define THREAD_PRIORITY_IDLE			(-15)
#define THREAD_PRIORITY_LOWEST			(-2)
#define THREAD_PRIORITY_BELOW_NORMAL	(-1)
#define THREAD_PRIORITY_NORMAL			0
#define THREAD_PRIORITY_ABOVE_NORMAL	1
#define THREAD_PRIORITY_HIGHEST			2
#define THREAD_PRIORITY_TIME_CRITICAL	15

#define NORMAL_PRIORITY_CLASS		0x00000020
#define HIGH_PRIORITY_CLASS			0x00000080
#define IDLE_PRIORITY_CLASS			0x00000040

#define GMEM_FIXED		0x0000
#define GMEM_MOVEABLE	0x0002
#define GMEM_ZEROINIT	0x0040
#define GPTR			(GMEM_FIXED | GMEM_ZEROINIT)

#define CP_ACP			0
#define CP_UTF8			65001

#define MAKELONG(a, b)	((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | (((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16)))
#define MAKEWORD(a, b)	((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | (((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8)))
#define LOWORD(l)		((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)		((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define LOBYTE(w)		((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w)		((BYTE)(((DWORD_PTR)(w) >> 8) & 0xff))
#define GET_X_LPARAM(lp)	((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)	((int)(short)HIWORD(lp))
#define RGB(r, g, b)	((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c)	((BYTE)(c))
#define GetGValue(c)	((BYTE)(((WORD)(c)) >> 8))
#define GetBValue(c)	((BYTE)((c) >> 16))


/*
** COM. The interface shapes, plus the in-process class object table that activation
** needs. The engine is its own COM server: it publishes a class factory for every
** persistent game class through CoRegisterClassObject during startup and then creates
** those objects by class identifier, so nothing outside this module is involved. What is
** absent is the rest of OLE -- the registry, the service control manager, marshalling,
** apartments, and any server that is not this one.
*/
#define STDMETHOD(method)			virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method)	virtual type STDMETHODCALLTYPE method
#define STDMETHODIMP				HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type)			type STDMETHODCALLTYPE
#define PURE						= 0
#define THIS_
#define THIS						void
#define DECLARE_INTERFACE(iface)			struct iface
#define DECLARE_INTERFACE_(iface, base)		struct iface : public base
#define MIDL_INTERFACE(uuid)				struct
#define interface							struct
#define __RPCNDR_H_VERSION__				500

/*
** __uuidof needs -fms-extensions and __declspec(uuid), neither of which this target
** builds with, so the identity MIDL_INTERFACE attaches to a type on Windows is not
** available. Every interface the tree activates or queries for also has its identifier
** declared as IID_<interface> and defined in the matching MIDL _i.c file, so the constant
** stands in for the attribute. An interface without one fails to compile here rather than
** resolving to an identifier that matches the wrong thing.
*/
#define __uuidof(type)	(IID_##type)

#if defined(INITGUID)
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID name
#endif

EXTERN_C const IID IID_IUnknown;
EXTERN_C const IID IID_ISequentialStream;
EXTERN_C const IID IID_IPersist;
EXTERN_C const IID IID_IStream;
EXTERN_C const IID IID_IPersistStream;
EXTERN_C const IID IID_IClassFactory;
EXTERN_C const GUID GUID_NULL;
#define IID_NULL	GUID_NULL
#define CLSID_NULL	GUID_NULL

struct IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef(void) = 0;
	virtual ULONG STDMETHODCALLTYPE Release(void) = 0;
};
typedef IUnknown * LPUNKNOWN;

struct ISequentialStream : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE Read(void * pv, ULONG cb, ULONG * read) = 0;
	virtual HRESULT STDMETHODCALLTYPE Write(void const * pv, ULONG cb, ULONG * written) = 0;
};

typedef struct tagSTATSTG {
	LPOLESTR pwcsName;
	DWORD type;
	ULARGE_INTEGER cbSize;
	FILETIME mtime;
	FILETIME ctime;
	FILETIME atime;
	DWORD grfMode;
	DWORD grfLocksSupported;
	CLSID clsid;
	DWORD grfStateBits;
	DWORD reserved;
} STATSTG;

struct IStream : public ISequentialStream
{
	virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER * newposition) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER newsize) = 0;
	virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream * stream, ULARGE_INTEGER cb, ULARGE_INTEGER * read, ULARGE_INTEGER * written) = 0;
	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD commitflags) = 0;
	virtual HRESULT STDMETHODCALLTYPE Revert(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER cb, DWORD locktype) = 0;
	virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER cb, DWORD locktype) = 0;
	virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG * statstg, DWORD statflag) = 0;
	virtual HRESULT STDMETHODCALLTYPE Clone(IStream ** stream) = 0;
};
typedef IStream * LPSTREAM;

struct IPersist : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetClassID(CLSID * classid) = 0;
};

struct IPersistStream : public IPersist
{
	virtual HRESULT STDMETHODCALLTYPE IsDirty(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE Load(IStream * stream) = 0;
	virtual HRESULT STDMETHODCALLTYPE Save(IStream * stream, BOOL clear) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER * size) = 0;
};

struct IClassFactory : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown * outer, REFIID riid, void ** object) = 0;
	virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) = 0;
};


/*
** COM runtime. Activation is answered out of the class object table CoRegisterClassObject
** fills in, and a class identifier that is not in it reports itself and fails with the
** code Windows uses for the same condition.
*/
#define REGDB_E_CLASSNOTREG	((HRESULT)0x80040154L)
#define CO_E_CLASSSTRING	((HRESULT)0x800401F3L)

HRESULT CoInitialize(LPVOID reserved);
void CoUninitialize(void);
HRESULT CoRegisterClassObject(REFCLSID classid, IUnknown * object, DWORD context, DWORD flags, LPDWORD registration);
HRESULT CoRevokeClassObject(DWORD registration);
HRESULT CoGetClassObject(REFCLSID classid, DWORD context, LPVOID reserved, REFIID riid, LPVOID * object);
HRESULT CoCreateInstance(REFCLSID classid, IUnknown * outer, DWORD context, REFIID riid, LPVOID * object);
LPVOID CoTaskMemAlloc(SIZE_T size);
void CoTaskMemFree(LPVOID block);
HRESULT CreateStreamOnHGlobal(HGLOBAL memory, BOOL deleteonrelease, LPSTREAM * stream);
HRESULT CLSIDFromString(LPCOLESTR string, LPCLSID classid);
BOOL IsEqualGUID(REFGUID first, REFGUID second);
#define IsEqualIID(a, b)	IsEqualGUID(a, b)
#define IsEqualCLSID(a, b)	IsEqualGUID(a, b)

/*
** comdef.h's smart pointer, reduced to what the tree asks of it. The real template
** carries the interface identifier as a second parameter so that construction from
** another interface can QueryInterface for the one it wants. Conversion between two
** interfaces uses dynamic_cast instead, which answers the same question about these
** interfaces: they are ordinary polymorphic C++ classes in a single module, and a cast
** across them succeeds exactly when the object implements the target. Activation cannot
** be answered that way, because the identifier has to reach the object's QueryInterface
** before there is an object to cast, so _COM_SMARTPTR_TYPEDEF records the identifier and
** CreateInstance asks for that interface exactly as Windows does. A multiply inherited
** implementation such as LocomotionClass hands back a different address for each of its
** interfaces, and that address is the one this pointer must hold.
*/
void _com_issue_error(HRESULT result);

/*
** The interface identifier a smart pointer activates for. Only _COM_SMARTPTR_TYPEDEF
** supplies one, so activating an interface that never went through it is a compile error
** rather than a request carrying the wrong identifier.
*/
template<class T> struct _com_interface_id;

template<class T> class _com_ptr_t
{
	public:
		_com_ptr_t(void) : Ptr(nullptr) {}
		_com_ptr_t(T * ptr) : Ptr(ptr)
		{
			if (Ptr != nullptr) Ptr->AddRef();
		}
		_com_ptr_t(T * ptr, bool addref) : Ptr(ptr)
		{
			if (Ptr != nullptr && addref) Ptr->AddRef();
		}
		_com_ptr_t(_com_ptr_t const & that) : Ptr(that.Ptr)
		{
			if (Ptr != nullptr) Ptr->AddRef();
		}

		/*
		** Construction from any other interface, standing in for the QueryInterface the
		** real template performs.
		*/
		template<class U> _com_ptr_t(U * ptr) : Ptr(dynamic_cast<T *>(ptr))
		{
			if (Ptr != nullptr) Ptr->AddRef();
		}
		template<class U> _com_ptr_t(_com_ptr_t<U> const & that) : Ptr(dynamic_cast<T *>(that.GetInterfacePtr()))
		{
			if (Ptr != nullptr) Ptr->AddRef();
		}

		/*
		** comdef.h's constructor has no way to return a failure, so it raises one. This
		** one does the same rather than leaving a null pointer for the caller to walk
		** into; the CreateInstance method below is the form that reports by return value.
		*/
		explicit _com_ptr_t(CLSID const & classid, IUnknown * outer = nullptr, DWORD context = CLSCTX_ALL) : Ptr(nullptr)
		{
			HRESULT result = CreateInstance(classid, outer, context);
			if (FAILED(result)) _com_issue_error(result);
		}

		~_com_ptr_t(void) { Release(); }

		_com_ptr_t & operator = (_com_ptr_t const & that)
		{
			if (this != &that) Assign(that.Ptr);
			return(*this);
		}
		_com_ptr_t & operator = (T * ptr) { Assign(ptr); return(*this); }
		template<class U> _com_ptr_t & operator = (_com_ptr_t<U> const & that)
		{
			Assign(dynamic_cast<T *>(that.GetInterfacePtr()));
			return(*this);
		}

		operator T * (void) const { return(Ptr); }
		T & operator * (void) const { return(*Ptr); }
		T * operator -> (void) const { return(Ptr); }
		T ** operator & (void) { Release(); return(&Ptr); }

		bool operator ! (void) const { return(Ptr == nullptr); }

		T * GetInterfacePtr(void) const { return(Ptr); }

		void Attach(T * ptr) { Release(); Ptr = ptr; }
		void Attach(T * ptr, bool addref)
		{
			Release();
			Ptr = ptr;
			if (Ptr != nullptr && addref) Ptr->AddRef();
		}
		T * Detach(void) { T * ptr = Ptr; Ptr = nullptr; return(ptr); }

		void Release(void)
		{
			T * old = Ptr;
			Ptr = nullptr;
			if (old != nullptr) old->Release();
		}

		HRESULT CreateInstance(CLSID const & classid, IUnknown * outer = nullptr, DWORD context = CLSCTX_ALL)
		{
			Release();
			return(CoCreateInstance(classid, outer, context, _com_interface_id<T>::Id(), (void **)&Ptr));
		}

		HRESULT QueryInterface(REFIID riid, void ** object) const
		{
			if (Ptr == nullptr) return(E_POINTER);
			return(Ptr->QueryInterface(riid, object));
		}

	private:
		void Assign(T * ptr)
		{
			T * old = Ptr;
			Ptr = ptr;
			if (Ptr != nullptr) Ptr->AddRef();
			if (old != nullptr) old->Release();
		}

		T * Ptr;
};

#define _COM_SMARTPTR_TYPEDEF(iface, iid) \
	template<> struct _com_interface_id<iface> \
	{ \
		static IID const & Id(void) { return(iid); } \
	}; \
	typedef _com_ptr_t<iface> iface##Ptr

_COM_SMARTPTR_TYPEDEF(IUnknown, IID_IUnknown);
_COM_SMARTPTR_TYPEDEF(IStream, IID_IStream);
_COM_SMARTPTR_TYPEDEF(IPersist, IID_IPersist);
_COM_SMARTPTR_TYPEDEF(IPersistStream, IID_IPersistStream);
_COM_SMARTPTR_TYPEDEF(IClassFactory, IID_IClassFactory);


/*
** DirectSound. The interfaces exist so the audio headers parse; the device is never
** created, because there is no DirectSound behind them.
*/
typedef HRESULT DSRESULT;

#define DS_OK						((HRESULT)0)
#define DSERR_ALLOCATED				MAKE_HRESULT(1, 0x878, 10)
#define DSERR_INVALIDPARAM			E_INVALIDARG
#define DSERR_OUTOFMEMORY			E_OUTOFMEMORY
#define DSERR_BUFFERLOST			MAKE_HRESULT(1, 0x878, 150)
#define DSERR_NODRIVER				MAKE_HRESULT(1, 0x878, 120)
#define DSERR_UNSUPPORTED			E_NOTIMPL
#define DSERR_GENERIC				E_FAIL

#define DSBCAPS_PRIMARYBUFFER		0x00000001
#define DSBCAPS_STATIC				0x00000002
#define DSBCAPS_LOCHARDWARE			0x00000004
#define DSBCAPS_LOCSOFTWARE			0x00000008
#define DSBCAPS_CTRLFREQUENCY		0x00000020
#define DSBCAPS_CTRLPAN				0x00000040
#define DSBCAPS_CTRLVOLUME			0x00000080
#define DSBCAPS_CTRLPOSITIONNOTIFY	0x00000100
#define DSBCAPS_STICKYFOCUS			0x00004000
#define DSBCAPS_GLOBALFOCUS			0x00008000
#define DSBCAPS_GETCURRENTPOSITION2	0x00010000

#define DSBPLAY_LOOPING				0x00000001

#define DSBSTATUS_PLAYING			0x00000001
#define DSBSTATUS_BUFFERLOST		0x00000002
#define DSBSTATUS_LOOPING			0x00000004

#define DSBLOCK_FROMWRITECURSOR		0x00000001
#define DSBLOCK_ENTIREBUFFER		0x00000002

#define DSSCL_NORMAL				0x00000001
#define DSSCL_PRIORITY				0x00000002
#define DSSCL_EXCLUSIVE				0x00000003
#define DSSCL_WRITEPRIMARY			0x00000004

#define DSBVOLUME_MIN				(-10000)
#define DSBVOLUME_MAX				0
#define DSBPAN_LEFT					(-10000)
#define DSBPAN_CENTER				0
#define DSBPAN_RIGHT				10000

#define WAVE_FORMAT_PCM				1

typedef struct _DSCAPS {
	DWORD dwSize;
	DWORD dwFlags;
	DWORD dwMinSecondarySampleRate;
	DWORD dwMaxSecondarySampleRate;
	DWORD dwPrimaryBuffers;
	DWORD dwMaxHwMixingAllBuffers;
	DWORD dwMaxHwMixingStaticBuffers;
	DWORD dwMaxHwMixingStreamingBuffers;
	DWORD dwFreeHwMixingAllBuffers;
	DWORD dwFreeHwMixingStaticBuffers;
	DWORD dwFreeHwMixingStreamingBuffers;
	DWORD dwMaxHw3DAllBuffers;
	DWORD dwMaxHw3DStaticBuffers;
	DWORD dwMaxHw3DStreamingBuffers;
	DWORD dwFreeHw3DAllBuffers;
	DWORD dwFreeHw3DStaticBuffers;
	DWORD dwFreeHw3DStreamingBuffers;
	DWORD dwTotalHwMemBytes;
	DWORD dwFreeHwMemBytes;
	DWORD dwMaxContigFreeHwMemBytes;
	DWORD dwUnlockTransferRateHwBuffers;
	DWORD dwPlayCpuOverheadSwBuffers;
	DWORD dwReserved1;
	DWORD dwReserved2;
} DSCAPS, * LPDSCAPS;

typedef struct _DSBCAPS {
	DWORD dwSize;
	DWORD dwFlags;
	DWORD dwBufferBytes;
	DWORD dwUnlockTransferRate;
	DWORD dwPlayCpuOverhead;
} DSBCAPS, * LPDSBCAPS;

typedef struct _DSBUFFERDESC {
	DWORD dwSize;
	DWORD dwFlags;
	DWORD dwBufferBytes;
	DWORD dwReserved;
	LPWAVEFORMATEX lpwfxFormat;
} DSBUFFERDESC, * LPDSBUFFERDESC;

struct IDirectSoundBuffer;

struct IDirectSound : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPDSBUFFERDESC desc, IDirectSoundBuffer ** buffer, IUnknown * outer) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps) = 0;
	virtual HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(IDirectSoundBuffer * original, IDirectSoundBuffer ** duplicate) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD level) = 0;
	virtual HRESULT STDMETHODCALLTYPE Compact(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD config) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD config) = 0;
	virtual HRESULT STDMETHODCALLTYPE Initialize(GUID * device) = 0;
};

struct IDirectSoundBuffer : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS caps) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX format, DWORD size, LPDWORD written) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVolume(LPLONG volume) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPan(LPLONG pan) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD frequency) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD status) = 0;
	virtual HRESULT STDMETHODCALLTYPE Initialize(IDirectSound * directsound, LPDSBUFFERDESC desc) = 0;
	virtual HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, LPVOID * audio1, LPDWORD size1, LPVOID * audio2, LPDWORD size2, DWORD flags) = 0;
	virtual HRESULT STDMETHODCALLTYPE Play(DWORD reserved1, DWORD priority, DWORD flags) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFormat(LPWAVEFORMATEX format) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPan(LONG pan) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) = 0;
	virtual HRESULT STDMETHODCALLTYPE Stop(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unlock(LPVOID audio1, DWORD size1, LPVOID audio2, DWORD size2) = 0;
	virtual HRESULT STDMETHODCALLTYPE Restore(void) = 0;
};

typedef IDirectSound * LPDIRECTSOUND;
typedef IDirectSoundBuffer * LPDIRECTSOUNDBUFFER;

inline HRESULT DirectSoundCreate(GUID * device, LPDIRECTSOUND * directsound, IUnknown * outer)
{
	(void)device;
	(void)outer;
	if (directsound != nullptr) *directsound = nullptr;
	return(WIN32_STUB(DSERR_NODRIVER));
}


/*
** The Win32 entry points. Each is a stub except where noted.
*/

/*
** Clocks. These are the one group with a correct answer available off the host, so they
** compute it instead of reporting a stub.
*/
DWORD GetTickCount(void);
DWORD timeGetTime(void);
BOOL QueryPerformanceCounter(LARGE_INTEGER * count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency);
void GetSystemTime(SYSTEMTIME * time);
void GetLocalTime(SYSTEMTIME * time);

inline DWORD timeBeginPeriod(UINT period) { (void)period; return(0); }
inline DWORD timeEndPeriod(UINT period) { (void)period; return(0); }

/*
** wsprintf is sprintf with Windows' own format subset; the subset the engine uses is
** the standard one, so this forwards rather than reimplementing it.
*/
int wsprintfA(LPSTR output, LPCSTR format, ...) __attribute__((format(printf, 2, 3)));
#define wsprintf	wsprintfA

inline int lstrlenA(LPCSTR string) { return(string != nullptr ? (int)strlen(string) : 0); }
inline LPSTR lstrcpyA(LPSTR destination, LPCSTR source) { return(strcpy(destination, source)); }
inline LPSTR lstrcatA(LPSTR destination, LPCSTR source) { return(strcat(destination, source)); }
inline int lstrcmpiA(LPCSTR left, LPCSTR right) { return(stricmp(left, right)); }
#define lstrlen		lstrlenA
#define lstrcpy		lstrcpyA
#define lstrcat		lstrcatA
#define lstrcmpi	lstrcmpiA

/* Rectangles are pure arithmetic on the structure, so they are implemented. */
inline BOOL SetRect(LPRECT rect, int left, int top, int right, int bottom)
{
	if (rect == nullptr) return(FALSE);
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
	return(TRUE);
}


inline BOOL SetRectEmpty(LPRECT rect) { return(SetRect(rect, 0, 0, 0, 0)); }


inline BOOL OffsetRect(LPRECT rect, int dx, int dy)
{
	if (rect == nullptr) return(FALSE);
	rect->left += dx;
	rect->right += dx;
	rect->top += dy;
	rect->bottom += dy;
	return(TRUE);
}


inline BOOL InflateRect(LPRECT rect, int dx, int dy)
{
	if (rect == nullptr) return(FALSE);
	rect->left -= dx;
	rect->right += dx;
	rect->top -= dy;
	rect->bottom += dy;
	return(TRUE);
}


inline BOOL IsRectEmpty(LPCRECT rect)
{
	if (rect == nullptr) return(TRUE);
	return(rect->left >= rect->right || rect->top >= rect->bottom);
}


inline BOOL PtInRect(LPCRECT rect, POINT point)
{
	if (rect == nullptr) return(FALSE);
	return(point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom);
}


inline BOOL EqualRect(LPCRECT left, LPCRECT right)
{
	return(left->left == right->left && left->top == right->top
		&& left->right == right->right && left->bottom == right->bottom);
}


inline BOOL IntersectRect(LPRECT destination, LPCRECT left, LPCRECT right)
{
	destination->left = left->left > right->left ? left->left : right->left;
	destination->top = left->top > right->top ? left->top : right->top;
	destination->right = left->right < right->right ? left->right : right->right;
	destination->bottom = left->bottom < right->bottom ? left->bottom : right->bottom;

	if (IsRectEmpty(destination)) {
		SetRectEmpty(destination);
		return(FALSE);
	}
	return(TRUE);
}


inline BOOL UnionRect(LPRECT destination, LPCRECT left, LPCRECT right)
{
	if (IsRectEmpty(left)) { *destination = *right; return(!IsRectEmpty(destination)); }
	if (IsRectEmpty(right)) { *destination = *left; return(!IsRectEmpty(destination)); }

	destination->left = left->left < right->left ? left->left : right->left;
	destination->top = left->top < right->top ? left->top : right->top;
	destination->right = left->right > right->right ? left->right : right->right;
	destination->bottom = left->bottom > right->bottom ? left->bottom : right->bottom;
	return(TRUE);
}


inline BOOL CopyRect(LPRECT destination, LPCRECT source) { *destination = *source; return(TRUE); }


/* Error reporting. The stub layer keeps its own last-error slot so callers read back what they set. */
DWORD GetLastError(void);
void SetLastError(DWORD error);

/* Process, module, and thread. */
HANDLE GetCurrentProcess(void);
HANDLE GetCurrentThread(void);
DWORD GetCurrentThreadId(void);
DWORD GetCurrentProcessId(void);
void Sleep(DWORD milliseconds);
void GlobalMemoryStatus(LPMEMORYSTATUS status);
void OutputDebugStringA(LPCSTR string);
#define OutputDebugString	OutputDebugStringA

/* Synchronization. */
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manualreset, BOOL initialstate, LPCSTR name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialowner, LPCSTR name);
BOOL ReleaseMutex(HANDLE mutex);
DWORD WaitForSingleObject(HANDLE object, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, HANDLE const * objects, BOOL waitall, DWORD milliseconds);
LONG InterlockedIncrement(LONG volatile * addend);
LONG InterlockedDecrement(LONG volatile * addend);
LONG InterlockedExchange(LONG volatile * target, LONG value);
#define CreateEvent		CreateEventA
#define CreateMutex		CreateMutexA

/*
** Files and directories. Everything from CreateFileA through GetTempPathA is implemented
** over POSIX rather than stubbed, because the host has the answers. Three consequences of
** that mapping are visible to callers.
**
** Sharing modes are not enforced. POSIX has no mandatory locking, so dwShareMode is
** accepted and ignored; two opens that Windows would have refused both succeed here.
**
** Paths are resolved case-insensitively when the exact spelling does not exist, so the
** upper-case names the engine asks for reach the assets a user supplied in either case on
** a case-sensitive filesystem. Backslashes are accepted as separators.
**
** A search returns its matches in case-insensitive name order rather than in the order the
** filesystem happens to hold them, so a wildcard scan such as ECACHE*.MIX registers its
** archives in the same sequence on every host.
*/
HANDLE CreateFileA(LPCSTR filename, DWORD access, DWORD sharemode, LPSECURITY_ATTRIBUTES attributes, DWORD creation, DWORD flags, HANDLE templatefile);
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD tobread, LPDWORD read, LPOVERLAPPED overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD towrite, LPDWORD written, LPOVERLAPPED overlapped);
DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distancehigh, DWORD method);

/// <summary>Says what a run of an already open file is about to be used for.</summary>
/// <param name="file">A handle the file API handed out.</param>
/// <param name="kind">Whether the run is being read now or may be wanted later.</param>
/// <param name="offset">Byte offset within the file.</param>
/// <param name="length">How many bytes it covers, or 0 for the rest of the file.</param>
/// <returns>bool; Was the handle one an image answers for?</returns>
/// <remarks>What an open cannot say and this can: the mixfile system opens an archive and
/// then reads one embedded file out of the middle of it, so the run being read is a
/// fraction of the file the handle names and ends long before it does.</remarks>
bool Win32_Hint_Handle(HANDLE file, ISOHintType kind, unsigned int offset, unsigned int length);

DWORD GetFileSize(HANDLE file, LPDWORD sizehigh);
BOOL SetEndOfFile(HANDLE file);
BOOL FlushFileBuffers(HANDLE file);
BOOL CloseHandle(HANDLE object);
BOOL DeleteFileA(LPCSTR filename);
BOOL MoveFileA(LPCSTR existing, LPCSTR newname);
BOOL CopyFileA(LPCSTR existing, LPCSTR newname, BOOL failifexists);
DWORD GetFileAttributesA(LPCSTR filename);
BOOL SetFileAttributesA(LPCSTR filename, DWORD attributes);
BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
BOOL SetFileTime(HANDLE file, FILETIME const * creation, FILETIME const * access, FILETIME const * write);
HANDLE FindFirstFileA(LPCSTR filename, LPWIN32_FIND_DATAA data);
BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL FindClose(HANDLE find);
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attributes);
BOOL RemoveDirectoryA(LPCSTR path);
DWORD GetCurrentDirectoryA(DWORD length, LPSTR buffer);
BOOL SetCurrentDirectoryA(LPCSTR path);
DWORD GetTempPathA(DWORD length, LPSTR buffer);
BOOL FileTimeToSystemTime(FILETIME const * filetime, LPSYSTEMTIME systemtime);
BOOL SystemTimeToFileTime(SYSTEMTIME const * systemtime, LPFILETIME filetime);
BOOL FileTimeToLocalFileTime(FILETIME const * filetime, LPFILETIME local);
LONG CompareFileTime(FILETIME const * first, FILETIME const * second);
#define CreateFile			CreateFileA
#define DeleteFile			DeleteFileA
#define MoveFile			MoveFileA
#define CopyFile			CopyFileA
#define GetFileAttributes	GetFileAttributesA
#define SetFileAttributes	SetFileAttributesA
#define FindFirstFile		FindFirstFileA
#define FindNextFile		FindNextFileA
#define CreateDirectory		CreateDirectoryA
#define RemoveDirectory		RemoveDirectoryA
#define GetCurrentDirectory	GetCurrentDirectoryA
#define SetCurrentDirectory	SetCurrentDirectoryA
#define GetTempPath			GetTempPathA

/* Memory. */
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
HGLOBAL GlobalFree(HGLOBAL memory);
LPVOID GlobalLock(HGLOBAL memory);
BOOL GlobalUnlock(HGLOBAL memory);

/* Windows, messages, and input. */
LRESULT SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL PostMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL PeekMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax, UINT remove);
BOOL GetMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax);
BOOL TranslateMessage(MSG const * message);
LRESULT DispatchMessageA(MSG const * message);
void PostQuitMessage(int exitcode);
ATOM RegisterClassA(WNDCLASSA const * windowclass);
ATOM RegisterClassExA(WNDCLASSEXA const * windowclass);
BOOL UnregisterClassA(LPCSTR classname, HINSTANCE instance);
HWND CreateWindowExA(DWORD exstyle, LPCSTR classname, LPCSTR windowname, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
BOOL DestroyWindow(HWND window);
BOOL ShowWindow(HWND window, int command);
BOOL UpdateWindow(HWND window);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL SetWindowPos(HWND window, HWND insertafter, int x, int y, int cx, int cy, UINT flags);
BOOL GetWindowRect(HWND window, LPRECT rect);
BOOL GetClientRect(HWND window, LPRECT rect);
BOOL ClientToScreen(HWND window, LPPOINT point);
BOOL ScreenToClient(HWND window, LPPOINT point);
BOOL AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu);
BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exstyle);
BOOL InvalidateRect(HWND window, RECT const * rect, BOOL erase);
BOOL ValidateRect(HWND window, RECT const * rect);
HDC BeginPaint(HWND window, LPPAINTSTRUCT paint);
BOOL EndPaint(HWND window, PAINTSTRUCT const * paint);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
int FillRect(HDC dc, RECT const * rect, HBRUSH brush);
LONG GetWindowLongA(HWND window, int index);
LONG SetWindowLongA(HWND window, int index, LONG value);
LONG_PTR GetWindowLongPtrA(HWND window, int index);
LONG_PTR SetWindowLongPtrA(HWND window, int index, LONG_PTR value);
#define GetWindowLongPtr	GetWindowLongPtrA
#define SetWindowLongPtr	SetWindowLongPtrA
BOOL SetWindowTextA(HWND window, LPCSTR text);
int GetWindowTextA(HWND window, LPSTR text, int count);
BOOL EnableWindow(HWND window, BOOL enable);
BOOL IsWindow(HWND window);
BOOL IsWindowVisible(HWND window);
BOOL IsIconic(HWND window);
HWND SetFocus(HWND window);
HWND GetFocus(void);
HWND SetCapture(HWND window);
BOOL ReleaseCapture(void);
HWND GetActiveWindow(void);
HWND SetActiveWindow(HWND window);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND window);
BOOL BringWindowToTop(HWND window);
HWND GetDesktopWindow(void);
HWND FindWindowA(LPCSTR classname, LPCSTR windowname);
HWND GetParent(HWND window);
HWND GetDlgItem(HWND dialog, int id);
LRESULT SendDlgItemMessageA(HWND dialog, int id, UINT message, WPARAM wparam, LPARAM lparam);
UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC callback);
BOOL KillTimer(HWND window, UINT_PTR id);
int MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type);
int GetSystemMetrics(int index);
HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name);
HICON LoadIconA(HINSTANCE instance, LPCSTR name);
HCURSOR SetCursor(HCURSOR cursor);
int ShowCursor(BOOL show);
BOOL GetCursorPos(LPPOINT point);
BOOL SetCursorPos(int x, int y);
BOOL ClipCursor(RECT const * rect);
SHORT GetKeyState(int key);
SHORT GetAsyncKeyState(int key);
BOOL GetKeyboardState(PBYTE state);
UINT MapVirtualKeyA(UINT code, UINT maptype);
int MultiByteToWideChar(UINT codepage, DWORD flags, LPCSTR multibyte, int multibytecount, LPWSTR wide, int widecount);
int WideCharToMultiByte(UINT codepage, DWORD flags, LPCWSTR wide, int widecount, LPSTR multibyte, int multibytecount, LPCSTR defaultchar, LPBOOL useddefaultchar);
#define SendMessage			SendMessageA
#define PostMessage			PostMessageA
#define DefWindowProc		DefWindowProcA
#define PeekMessage			PeekMessageA
#define GetMessage			GetMessageA
#define DispatchMessage		DispatchMessageA
#define RegisterClass		RegisterClassA
#define RegisterClassEx		RegisterClassExA
#define UnregisterClass		UnregisterClassA
#define CreateWindowEx		CreateWindowExA
#define GetWindowLong		GetWindowLongA
#define SetWindowLong		SetWindowLongA
#define SetWindowText		SetWindowTextA
#define GetWindowText		GetWindowTextA
#define FindWindow			FindWindowA
#define SendDlgItemMessage	SendDlgItemMessageA
#define MessageBox			MessageBoxA
#define LoadCursor			LoadCursorA
#define LoadIcon			LoadIconA
#define MapVirtualKey		MapVirtualKeyA

/* Registry. */
LONG RegOpenKeyExA(HKEY key, LPCSTR subkey, DWORD options, DWORD desired, PHKEY result);
LONG RegCreateKeyExA(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR classname, DWORD options, DWORD desired, LPSECURITY_ATTRIBUTES attributes, PHKEY result, LPDWORD disposition);
LONG RegQueryValueExA(HKEY key, LPCSTR valuename, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LONG RegSetValueExA(HKEY key, LPCSTR valuename, DWORD reserved, DWORD type, BYTE const * data, DWORD size);
LONG RegDeleteValueA(HKEY key, LPCSTR valuename);
LONG RegCloseKey(HKEY key);
#define RegOpenKeyEx		RegOpenKeyExA
#define RegCreateKeyEx		RegCreateKeyExA
#define RegQueryValueEx		RegQueryValueExA
#define RegSetValueEx		RegSetValueExA
#define RegDeleteValue		RegDeleteValueA

/* Profile files. */
UINT GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT defaultvalue, LPCSTR filename);
DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR defaultvalue, LPSTR returned, DWORD size, LPCSTR filename);
BOOL WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR filename);
#define GetPrivateProfileInt	GetPrivateProfileIntA
#define GetPrivateProfileString	GetPrivateProfileStringA
#define WritePrivateProfileString WritePrivateProfileStringA

/* ShellExecute, from shellapi.h. */
HINSTANCE ShellExecuteA(HWND window, LPCSTR operation, LPCSTR file, LPCSTR parameters, LPCSTR directory, int showcommand);
#define ShellExecute	ShellExecuteA


/*
** GDI, as far as the software surface path reaches it. The engine's only real use is a
** DIB section it blits from, which does not exist here.
*/
typedef struct tagRGBQUAD {
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, * PBITMAPINFOHEADER, * LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD bmiColors[1];
} BITMAPINFO, * PBITMAPINFO, * LPBITMAPINFO;

typedef struct tagBITMAP {
	LONG bmType;
	LONG bmWidth;
	LONG bmHeight;
	LONG bmWidthBytes;
	WORD bmPlanes;
	WORD bmBitsPixel;
	LPVOID bmBits;
} BITMAP, * PBITMAP, * LPBITMAP;

typedef struct tagDIBSECTION {
	BITMAP dsBm;
	BITMAPINFOHEADER dsBmih;
	DWORD dsBitfields[3];
	HANDLE dshSection;
	DWORD dsOffset;
} DIBSECTION, * PDIBSECTION, * LPDIBSECTION;

#define BI_RGB				0L
#define BI_RLE8				1L
#define BI_RLE4				2L
#define BI_BITFIELDS		3L
#define DIB_RGB_COLORS		0
#define DIB_PAL_COLORS		1
#define BLACKONWHITE		1
#define WHITEONBLACK		2
#define COLORONCOLOR		3
#define HALFTONE			4
#define SRCCOPY				(DWORD)0x00CC0020
#define SRCPAINT			(DWORD)0x00EE0086
#define BLACKNESS			(DWORD)0x00000042

HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateDIBSection(HDC dc, BITMAPINFO const * info, UINT usage, void ** bits, HANDLE section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);
int GetObjectA(HGDIOBJ object, int count, LPVOID buffer);
BOOL GdiFlush(void);
int SetStretchBltMode(HDC dc, int mode);
BOOL StretchBlt(HDC destination, int x, int y, int width, int height, HDC source, int sx, int sy, int swidth, int sheight, DWORD rop);
BOOL BitBlt(HDC destination, int x, int y, int width, int height, HDC source, int sx, int sy, DWORD rop);
int StretchDIBits(HDC dc, int x, int y, int width, int height, int sx, int sy, int swidth, int sheight, void const * bits, BITMAPINFO const * info, UINT usage, DWORD rop);
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
HGDIOBJ GetStockObject(int object);
#define GetObject	GetObjectA

/*
** More of the window manager than the first pass needed.
*/
#define GW_HWNDFIRST	0
#define GW_HWNDLAST		1
#define GW_HWNDNEXT		2
#define GW_HWNDPREV		3
#define GW_OWNER		4
#define GW_CHILD		5

#define RDW_INVALIDATE		0x0001
#define RDW_INTERNALPAINT	0x0002
#define RDW_ERASE			0x0004
#define RDW_VALIDATE		0x0008
#define RDW_UPDATENOW		0x0100
#define RDW_ERASENOW		0x0200
#define RDW_FRAME			0x0400
#define RDW_ALLCHILDREN		0x0080

#define WM_XBUTTONDOWN		0x020B
#define WM_XBUTTONUP		0x020C
#define WM_XBUTTONDBLCLK	0x020D
#define WM_NCMOUSEMOVE		0x00A0
#define WM_CANCELMODE		0x001F
#define WM_ENTERMENULOOP	0x0211
#define WM_EXITMENULOOP		0x0212

#define SM_SWAPBUTTON		23
#define SM_CXDOUBLECLK		36
#define SM_CYDOUBLECLK		37

#define MONITOR_DEFAULTTONULL		0x00000000
#define MONITOR_DEFAULTTOPRIMARY	0x00000001
#define MONITOR_DEFAULTTONEAREST	0x00000002

typedef struct tagMONITORINFO {
	DWORD cbSize;
	RECT rcMonitor;
	RECT rcWork;
	DWORD dwFlags;
} MONITORINFO, * LPMONITORINFO;

HWND GetTopWindow(HWND window);
HWND GetWindow(HWND window, UINT command);
BOOL IsWindowEnabled(HWND window);
BOOL CloseWindow(HWND window);
int MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count);
BOOL RedrawWindow(HWND window, RECT const * update, HRGN region, UINT flags);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
BOOL GetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info);
BOOL SetDlgItemTextA(HWND dialog, int id, LPCSTR text);
UINT GetDlgItemTextA(HWND dialog, int id, LPSTR text, int count);
BOOL EndDialog(HWND dialog, INT_PTR result);
BOOL IsDialogMessageA(HWND dialog, LPMSG message);
int TranslateAcceleratorA(HWND window, HACCEL table, LPMSG message);
int ToAscii(UINT virtualkey, UINT scancode, BYTE const * keystate, LPWORD character, UINT flags);
BOOL CharToOemBuffA(LPCSTR source, LPSTR destination, DWORD length);
HLOCAL LocalFree(HLOCAL memory);
#define GetMonitorInfo		GetMonitorInfoA
#define SetDlgItemText		SetDlgItemTextA
#define GetDlgItemText		GetDlgItemTextA
#define IsDialogMessage		IsDialogMessageA
#define TranslateAccelerator TranslateAcceleratorA
#define CharToOemBuff		CharToOemBuffA

/*
** Module resources. The localized strings and the version block both live in resource
** libraries that this target does not build.
*/
#define MAKEINTRESOURCEA(id)	((LPSTR)(ULONG_PTR)(WORD)(ULONG_PTR)(id))
#define MAKEINTRESOURCE			MAKEINTRESOURCEA
#define RT_STRING				MAKEINTRESOURCEA(6)
#define RT_RCDATA				MAKEINTRESOURCEA(10)
#define TEXT(quote)				quote

int LoadStringA(HINSTANCE instance, UINT id, LPSTR buffer, int size);
HRSRC FindResourceA(HMODULE module, LPCSTR name, LPCSTR type);
HGLOBAL LoadResource(HMODULE module, HRSRC resource);
LPVOID LockResource(HGLOBAL resource);
DWORD SizeofResource(HMODULE module, HRSRC resource);
DWORD GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle);
BOOL GetFileVersionInfoA(LPCSTR filename, DWORD handle, DWORD length, LPVOID data);
BOOL VerQueryValueA(LPCVOID block, LPCSTR subblock, LPVOID * buffer, PUINT length);
#define LoadString				LoadStringA
#define FindResource			FindResourceA
#define GetFileVersionInfoSize	GetFileVersionInfoSizeA
#define GetFileVersionInfo		GetFileVersionInfoA
#define VerQueryValue			VerQueryValueA

/*
** The console and the crash path.
*/
#define STD_INPUT_HANDLE	((DWORD)-10)
#define STD_OUTPUT_HANDLE	((DWORD)-11)
#define STD_ERROR_HANDLE	((DWORD)-12)

#define EXCEPTION_NONCONTINUABLE			0x1
#define EXCEPTION_ACCESS_VIOLATION			((DWORD)0xC0000005L)
#define EXCEPTION_EXECUTE_HANDLER			1
#define EXCEPTION_CONTINUE_SEARCH			0
#define EXCEPTION_CONTINUE_EXECUTION		(-1)

BOOL AllocConsole(void);
BOOL FreeConsole(void);
BOOL SetConsoleTitleA(LPCSTR title);
HANDLE GetStdHandle(DWORD handle);
BOOL SetConsoleMode(HANDLE console, DWORD mode);
BOOL GetConsoleMode(HANDLE console, LPDWORD mode);
#define SetConsoleTitle		SetConsoleTitleA

/*
** OLE serialization and the automation string type, both reached from the save path.
*/
typedef OLECHAR * BSTR;

BSTR SysAllocString(OLECHAR const * string);
void SysFreeString(BSTR string);
UINT SysStringLen(BSTR string);
HRESULT StringFromCLSID(REFCLSID classid, LPOLESTR * string);
HRESULT OleSaveToStream(IPersistStream * persist, IStream * stream);
HRESULT OleLoadFromStream(IStream * stream, REFIID riid, void ** object);
#define MB_PRECOMPOSED	0x00000001

/*
** The multimedia timer, from mmsystem.h.
*/
typedef UINT MMRESULT;
typedef void (CALLBACK * LPTIMECALLBACK)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);

typedef struct timecaps_tag {
	UINT wPeriodMin;
	UINT wPeriodMax;
} TIMECAPS, * LPTIMECAPS;

#define TIMERR_NOERROR	0
#define TIMERR_NOCANDO	97
#define TIME_ONESHOT	0x0000
#define TIME_PERIODIC	0x0001

MMRESULT timeGetDevCaps(LPTIMECAPS caps, UINT size);
MMRESULT timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags);
MMRESULT timeKillEvent(UINT id);

#define DSCAPS_PRIMARYMONO		0x00000001
#define DSCAPS_PRIMARYSTEREO	0x00000002
#define DSCAPS_PRIMARY8BIT		0x00000004
#define DSCAPS_PRIMARY16BIT		0x00000008
#define DSCAPS_CONTINUOUSRATE	0x00000010
#define DSCAPS_EMULDRIVER		0x00000020
#define DSCAPS_CERTIFIED		0x00000040

/*
** windowsx.h's control wrappers, which are macros over SendMessage exactly as they are
** under Windows.
*/
#define BN_CLICKED			0
#define BN_DOUBLECLICKED	5
#define LBN_SELCHANGE		1
#define LBN_DBLCLK			2
#define CBN_SELCHANGE		1
#define EN_CHANGE			0x0300

#define EM_SETSEL			0x00B1
#define EM_GETSEL			0x00B0
#define EM_REPLACESEL		0x00C2
#define EM_SETLIMITTEXT		0x00C5
#define EM_LIMITTEXT		0x00C5
#define EM_SETPASSWORDCHAR	0x00CC

#define Button_GetCheck(hwnd)			((int)SendMessageA((hwnd), BM_GETCHECK, 0, 0))
#define Button_SetCheck(hwnd, check)	((void)SendMessageA((hwnd), BM_SETCHECK, (WPARAM)(int)(check), 0))
#define Button_GetState(hwnd)			((int)SendMessageA((hwnd), BM_GETSTATE, 0, 0))
#define Button_SetState(hwnd, state)	((void)SendMessageA((hwnd), BM_SETSTATE, (WPARAM)(BOOL)(state), 0))
#define Button_Enable(hwnd, enable)		EnableWindow((hwnd), (enable))
#define Button_SetText(hwnd, text)		((void)SetWindowTextA((hwnd), (text)))

#define Static_SetText(hwnd, text)		((void)SetWindowTextA((hwnd), (text)))
#define Static_GetText(hwnd, text, n)	GetWindowTextA((hwnd), (text), (n))

#define Edit_SetText(hwnd, text)		((void)SetWindowTextA((hwnd), (text)))
#define Edit_GetText(hwnd, text, n)		GetWindowTextA((hwnd), (text), (n))
#define Edit_SetSel(hwnd, start, end)	((void)SendMessageA((hwnd), EM_SETSEL, (WPARAM)(int)(start), (LPARAM)(int)(end)))
#define Edit_LimitText(hwnd, limit)		((void)SendMessageA((hwnd), EM_LIMITTEXT, (WPARAM)(limit), 0))
#define Edit_ReplaceSel(hwnd, text)		((void)SendMessageA((hwnd), EM_REPLACESEL, 0, (LPARAM)(LPCSTR)(text)))

#define ListBox_AddString(hwnd, text)			((int)SendMessageA((hwnd), LB_ADDSTRING, 0, (LPARAM)(LPCSTR)(text)))
#define ListBox_InsertString(hwnd, i, text)		((int)SendMessageA((hwnd), LB_INSERTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_DeleteString(hwnd, i)			((int)SendMessageA((hwnd), LB_DELETESTRING, (WPARAM)(int)(i), 0))
#define ListBox_ResetContent(hwnd)				((BOOL)SendMessageA((hwnd), LB_RESETCONTENT, 0, 0))
#define ListBox_GetCount(hwnd)					((int)SendMessageA((hwnd), LB_GETCOUNT, 0, 0))
#define ListBox_GetCurSel(hwnd)					((int)SendMessageA((hwnd), LB_GETCURSEL, 0, 0))
#define ListBox_SetCurSel(hwnd, i)				((int)SendMessageA((hwnd), LB_SETCURSEL, (WPARAM)(int)(i), 0))
#define ListBox_GetText(hwnd, i, text)			((int)SendMessageA((hwnd), LB_GETTEXT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_GetTextLen(hwnd, i)				((int)SendMessageA((hwnd), LB_GETTEXTLEN, (WPARAM)(int)(i), 0))
#define ListBox_GetItemData(hwnd, i)			((LRESULT)SendMessageA((hwnd), LB_GETITEMDATA, (WPARAM)(int)(i), 0))
#define ListBox_SetItemData(hwnd, i, data)		((int)SendMessageA((hwnd), LB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(data)))
#define ListBox_GetTopIndex(hwnd)				((int)SendMessageA((hwnd), LB_GETTOPINDEX, 0, 0))
#define ListBox_SetTopIndex(hwnd, i)			((int)SendMessageA((hwnd), LB_SETTOPINDEX, (WPARAM)(int)(i), 0))
#define ListBox_SetSel(hwnd, select, i)			((int)SendMessageA((hwnd), LB_SETSEL, (WPARAM)(BOOL)(select), (LPARAM)(int)(i)))
#define ListBox_GetSel(hwnd, i)					((int)SendMessageA((hwnd), LB_GETSEL, (WPARAM)(int)(i), 0))
#define ListBox_FindString(hwnd, i, text)		((int)SendMessageA((hwnd), LB_FINDSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ListBox_SelectString(hwnd, i, text)		((int)SendMessageA((hwnd), LB_SELECTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))

#define ComboBox_AddString(hwnd, text)			((int)SendMessageA((hwnd), CB_ADDSTRING, 0, (LPARAM)(LPCSTR)(text)))
#define ComboBox_ResetContent(hwnd)				((int)SendMessageA((hwnd), CB_RESETCONTENT, 0, 0))
#define ComboBox_GetCurSel(hwnd)				((int)SendMessageA((hwnd), CB_GETCURSEL, 0, 0))
#define ComboBox_SetCurSel(hwnd, i)				((int)SendMessageA((hwnd), CB_SETCURSEL, (WPARAM)(int)(i), 0))

/*
** The common controls the dialogs drive, from commctrl.h. Trackbars and progress bars
** are messages, so they are constants rather than entry points.
*/
#define TBM_GETPOS			(WM_USER)
#define TBM_GETRANGEMIN		(WM_USER + 1)
#define TBM_GETRANGEMAX		(WM_USER + 2)
#define TBM_SETPOS			(WM_USER + 5)
#define TBM_SETRANGE		(WM_USER + 6)
#define TBM_SETRANGEMIN		(WM_USER + 7)
#define TBM_SETRANGEMAX		(WM_USER + 8)
#define TBM_SETTICFREQ		(WM_USER + 20)
#define TBM_SETPAGESIZE		(WM_USER + 21)
#define TBM_SETLINESIZE		(WM_USER + 23)

#define PBM_SETRANGE		(WM_USER + 1)
#define PBM_SETPOS			(WM_USER + 2)
#define PBM_DELTAPOS		(WM_USER + 3)
#define PBM_SETSTEP			(WM_USER + 4)
#define PBM_STEPIT			(WM_USER + 5)
#define PBM_SETRANGE32		(WM_USER + 6)

BOOL InitCommonControls(void);


/*
** Device control, from winioctl.h. The engine's only device is the debug monitor card.
*/
#define METHOD_BUFFERED		0
#define METHOD_IN_DIRECT	1
#define METHOD_OUT_DIRECT	2
#define METHOD_NEITHER		3
#define FILE_ANY_ACCESS		0
#define FILE_READ_ACCESS	0x0001
#define FILE_WRITE_ACCESS	0x0002
#define FILE_READ_DATA		0x0001
#define FILE_WRITE_DATA		0x0002
#define FILE_DEVICE_UNKNOWN	0x00000022
#define CTL_CODE(devicetype, function, method, access) \
	(((devicetype) << 16) | ((access) << 14) | ((function) << 2) | (method))

BOOL DeviceIoControl(HANDLE device, DWORD code, LPVOID inbuffer, DWORD insize, LPVOID outbuffer, DWORD outsize, LPDWORD returned, LPOVERLAPPED overlapped);

/*
** The remainder of the window manager, the common controls, and GDI, as the dialog layer
** reaches them. The dialogs are Win32 dialogs end to end and none of this has a
** WebAssembly counterpart yet; the declarations exist so the layer compiles.
*/
#define SNDMSG	SendMessageA

#define IN
#define OUT
#define OPTIONAL

#define WM_NOTIFY			0x004E
#define WM_HELP				0x0053
#define WM_CONTEXTMENU		0x007B
#define WM_SETTINGCHANGE	0x001A
#define WM_SIZING			0x0214
#define WM_CAPTURECHANGED	0x0215
#define WM_NEXTDLGCTL		0x0028
#define WM_PARENTNOTIFY		0x0210

#define HTERROR				(-2)
#define HTTRANSPARENT		(-1)
#define HTNOWHERE			0
#define HTCLIENT			1
#define HTCAPTION			2

#define SIZE_RESTORED		0
#define SIZE_MINIMIZED		1
#define SIZE_MAXIMIZED		2

#define SC_SIZE				0xF000
#define SC_MOVE				0xF010
#define SC_MINIMIZE			0xF020
#define SC_MAXIMIZE			0xF030
#define SC_CLOSE			0xF060
#define SC_KEYMENU			0xF100
#define SC_SCREENSAVE		0xF140
#define SC_MONITORPOWER		0xF170

#define MF_BYCOMMAND		0x00000000L
#define MF_BYPOSITION		0x00000400L
#define MF_ENABLED			0x00000000L
#define MF_GRAYED			0x00000001L
#define MF_DISABLED			0x00000002L

#define MOD_ALT				0x0001
#define MOD_CONTROL			0x0002
#define MOD_SHIFT			0x0004
#define MOD_WIN				0x0008

#define CB_GETEDITSEL				0x0140
#define CB_LIMITTEXT				0x0141
#define CB_SETEDITSEL				0x0142
#define CB_DELETESTRING				0x0144
#define CB_GETCOUNT					0x0146
#define CB_GETLBTEXT				0x0148
#define CB_GETLBTEXTLEN				0x0149
#define CB_INSERTSTRING				0x014A
#define CB_FINDSTRING				0x014C
#define CB_SELECTSTRING				0x014D
#define CB_SHOWDROPDOWN				0x014F
#define CB_GETITEMDATA				0x0150
#define CB_SETITEMDATA				0x0151
#define CB_GETDROPPEDCONTROLRECT	0x0152
#define CB_SETITEMHEIGHT			0x0153
#define CB_GETITEMHEIGHT			0x0154
#define CB_GETDROPPEDSTATE			0x0157
#define CB_GETTOPINDEX				0x015B
#define CB_SETTOPINDEX				0x015C
#define CB_SETDROPPEDWIDTH			0x0160

#define EN_MAXTEXT			0x0501
#define EN_UPDATE			0x0400
#define EN_SETFOCUS			0x0100
#define EN_KILLFOCUS		0x0200

#define HKM_SETHOTKEY		(WM_USER + 1)
#define HKM_GETHOTKEY		(WM_USER + 2)
#define HKM_SETRULES		(WM_USER + 3)

#define SBM_SETPOS			0x00E0
#define SBM_GETPOS			0x00E1
#define SBM_SETRANGE		0x00E2
#define SBM_GETRANGE		0x00E3
#define SBM_SETSCROLLINFO	0x00E9
#define SBM_GETSCROLLINFO	0x00EA

#define SB_HORZ				0
#define SB_VERT				1
#define SB_CTL				2
#define SB_BOTH				3

#define SIF_RANGE			0x0001
#define SIF_PAGE			0x0002
#define SIF_POS				0x0004
#define SIF_DISABLENOSCROLL	0x0008
#define SIF_TRACKPOS		0x0010
#define SIF_ALL				(SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)

#define FW_DONTCARE			0
#define FW_NORMAL			400
#define FW_BOLD				700
#define ANSI_CHARSET		0
#define DEFAULT_CHARSET		1
#define OUT_DEFAULT_PRECIS	0
#define CLIP_DEFAULT_PRECIS	0
#define DEFAULT_QUALITY		0
#define DEFAULT_PITCH		0
#define TRANSPARENT			1
#define OPAQUE				2

#define HORZRES				8
#define VERTRES				10
#define BITSPIXEL			12
#define PLANES				14
#define VREFRESH			116
#define LOGPIXELSX			88
#define LOGPIXELSY			90

#define GM_COMPATIBLE		1
#define GM_ADVANCED			2

#define IDC_SIZEALL			((LPCSTR)32646)
#define IDC_NO				((LPCSTR)32648)
#define IDC_HAND			((LPCSTR)32649)
#define IDC_APPSTARTING		((LPCSTR)32650)

#define LOCALE_USER_DEFAULT		0x0400
#define LANG_USER_DEFAULT		0x0400
#define TIME_NOMINUTESORSECONDS	0x00000001
#define TIME_NOSECONDS			0x00000002
#define TIME_NOTIMEMARKER		0x00000004
#define TIME_FORCE24HOURFORMAT	0x00000008
#define DATE_SHORTDATE			0x00000001
#define DATE_LONGDATE			0x00000002

#define TRACKBAR_CLASS		"msctls_trackbar32"
#define PROGRESS_CLASS		"msctls_progress32"
#define WC_TREEVIEWA		"SysTreeView32"
#define WC_TREEVIEW			WC_TREEVIEWA

#define MAKEWPARAM(l, h)	((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h)	((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h)	((LRESULT)(DWORD)MAKELONG(l, h))

#define ZeroMemory(d, n)	memset((d), 0, (n))
#define CopyMemory(d, s, n)	memcpy((d), (s), (n))
#define MoveMemory(d, s, n)	memmove((d), (s), (n))
#define FillMemory(d, n, f)	memset((d), (f), (n))

typedef short VARIANT_BOOL;
#define VARIANT_TRUE	((VARIANT_BOOL)-1)
#define VARIANT_FALSE	((VARIANT_BOOL)0)

typedef struct _COORD {
	SHORT X;
	SHORT Y;
} COORD, * PCOORD;

typedef struct _SMALL_RECT {
	SHORT Left;
	SHORT Top;
	SHORT Right;
	SHORT Bottom;
} SMALL_RECT, * PSMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
	COORD dwSize;
	COORD dwCursorPosition;
	WORD wAttributes;
	SMALL_RECT srWindow;
	COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, * PCONSOLE_SCREEN_BUFFER_INFO;

typedef struct _RTL_OSVERSIONINFOW {
	ULONG dwOSVersionInfoSize;
	ULONG dwMajorVersion;
	ULONG dwMinorVersion;
	ULONG dwBuildNumber;
	ULONG dwPlatformId;
	WCHAR szCSDVersion[128];
} RTL_OSVERSIONINFOW, * PRTL_OSVERSIONINFOW;

typedef struct tagSCROLLINFO {
	UINT cbSize;
	UINT fMask;
	int nMin;
	int nMax;
	UINT nPage;
	int nPos;
	int nTrackPos;
} SCROLLINFO, * LPSCROLLINFO;
typedef SCROLLINFO const * LPCSCROLLINFO;

typedef struct tagLOGFONTA {
	LONG lfHeight;
	LONG lfWidth;
	LONG lfEscapement;
	LONG lfOrientation;
	LONG lfWeight;
	BYTE lfItalic;
	BYTE lfUnderline;
	BYTE lfStrikeOut;
	BYTE lfCharSet;
	BYTE lfOutPrecision;
	BYTE lfClipPrecision;
	BYTE lfQuality;
	BYTE lfPitchAndFamily;
	CHAR lfFaceName[32];
} LOGFONTA, LOGFONT, * PLOGFONTA, * LPLOGFONTA;

typedef struct tagTEXTMETRICA {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	CHAR tmFirstChar;
	CHAR tmLastChar;
	CHAR tmDefaultChar;
	CHAR tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
} TEXTMETRICA, TEXTMETRIC, * PTEXTMETRICA, * LPTEXTMETRICA;

typedef struct tagNMHDR {
	HWND hwndFrom;
	UINT_PTR idFrom;
	UINT code;
} NMHDR, * LPNMHDR;

DECLARE_HANDLE(HTREEITEM);
DECLARE_HANDLE(HIMAGELIST);

typedef struct tagTVITEMA {
	UINT mask;
	HTREEITEM hItem;
	UINT state;
	UINT stateMask;
	LPSTR pszText;
	int cchTextMax;
	int iImage;
	int iSelectedImage;
	int cChildren;
	LPARAM lParam;
} TVITEMA, TV_ITEM, TVITEM, * LPTVITEMA;

typedef struct tagNMTREEVIEWA {
	NMHDR hdr;
	UINT action;
	TVITEMA itemOld;
	TVITEMA itemNew;
	POINT ptDrag;
} NMTREEVIEWA, NM_TREEVIEW, NMTREEVIEW, * LPNMTREEVIEWA, * LPNMTREEVIEW;

typedef struct tagTVHITTESTINFO {
	POINT pt;
	UINT flags;
	HTREEITEM hItem;
} TVHITTESTINFO, TV_HITTESTINFO, * LPTVHITTESTINFO;

/*
** A dialog template and the item templates that follow it, as a resource compiler writes
** them. Both are two-byte packed and both are followed by variable length name, class,
** title and font fields, so neither is the whole record: they are the fixed head of one.
** The extended form a resource may use instead -- recognized by a first DWORD of
** 0xFFFF0001 -- carries different fields at different offsets and is read as bytes rather
** than declared here.
*/
#pragma pack(push, 2)
typedef struct {
	DWORD style;
	DWORD dwExtendedStyle;
	WORD cdit;
	short x;
	short y;
	short cx;
	short cy;
} DLGTEMPLATE, * LPDLGTEMPLATE;

typedef struct {
	DWORD style;
	DWORD dwExtendedStyle;
	short x;
	short y;
	short cx;
	short cy;
	WORD id;
} DLGITEMTEMPLATE, * LPDLGITEMTEMPLATE;
#pragma pack(pop)

typedef DLGTEMPLATE const * LPCDLGTEMPLATE;
typedef DLGITEMTEMPLATE const * LPCDLGITEMTEMPLATE;

#define DS_SETFONT			0x0040L

#define DLGWINDOWEXTRA		30

#define TVGN_ROOT			0x0000
#define TVGN_NEXT			0x0001
#define TVGN_PREVIOUS		0x0002
#define TVGN_PARENT			0x0003
#define TVGN_CHILD			0x0004
#define TVGN_FIRSTVISIBLE	0x0005
#define TVGN_NEXTVISIBLE	0x0006
#define TVGN_CARET			0x0009
#define TVGN_DROPHILITE		0x0008
#define TVM_SELECTITEM		(0x1100 + 11)
#define TVM_GETNEXTITEM		(0x1100 + 10)
#define TVM_GETINDENT		(0x1100 + 6)
#define TVM_HITTEST			(0x1100 + 17)
#define TVM_GETITEMA		(0x1100 + 12)
#define TVM_SETITEMA		(0x1100 + 13)
#define TVM_EXPAND			(0x1100 + 2)
#define TVN_SELCHANGEDA		(0u - 402u)
#define TVN_BEGINDRAGA		(0u - 407u)

#define TreeView_SelectItem(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SELECTITEM, TVGN_CARET, (LPARAM)(HTREEITEM)(item)))
#define TreeView_SelectDropTarget(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SELECTITEM, TVGN_DROPHILITE, (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetNextItem(hwnd, item, code) \
	((HTREEITEM)SendMessageA((hwnd), TVM_GETNEXTITEM, (WPARAM)(code), (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetFirstVisible(hwnd)	TreeView_GetNextItem((hwnd), NULL, TVGN_FIRSTVISIBLE)
#define TreeView_GetSelection(hwnd)		TreeView_GetNextItem((hwnd), NULL, TVGN_CARET)
#define TreeView_GetParent(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_PARENT)
#define TreeView_GetChild(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_CHILD)
#define TreeView_GetNextSibling(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_NEXT)
#define TreeView_GetIndent(hwnd)		((UINT)SendMessageA((hwnd), TVM_GETINDENT, 0, 0))
#define TreeView_HitTest(hwnd, info) \
	((HTREEITEM)SendMessageA((hwnd), TVM_HITTEST, 0, (LPARAM)(LPTVHITTESTINFO)(info)))
#define TreeView_GetItem(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_GETITEMA, 0, (LPARAM)(TVITEMA *)(item)))
#define TreeView_SetItem(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SETITEMA, 0, (LPARAM)(TVITEMA const *)(item)))
#define TreeView_Expand(hwnd, item, code) \
	((BOOL)SendMessageA((hwnd), TVM_EXPAND, (WPARAM)(code), (LPARAM)(HTREEITEM)(item)))

#define ComboBox_GetItemData(hwnd, i)			((LRESULT)SendMessageA((hwnd), CB_GETITEMDATA, (WPARAM)(int)(i), 0))
#define ComboBox_SetItemData(hwnd, i, data)		((int)SendMessageA((hwnd), CB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(data)))
#define ComboBox_GetCount(hwnd)					((int)SendMessageA((hwnd), CB_GETCOUNT, 0, 0))
#define ComboBox_DeleteString(hwnd, i)			((int)SendMessageA((hwnd), CB_DELETESTRING, (WPARAM)(int)(i), 0))
#define ComboBox_InsertString(hwnd, i, text)	((int)SendMessageA((hwnd), CB_INSERTSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ComboBox_FindString(hwnd, i, text)		((int)SendMessageA((hwnd), CB_FINDSTRING, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ComboBox_GetLBText(hwnd, i, text)		((int)SendMessageA((hwnd), CB_GETLBTEXT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))
#define ComboBox_GetLBTextLen(hwnd, i)			((int)SendMessageA((hwnd), CB_GETLBTEXTLEN, (WPARAM)(int)(i), 0))
#define ComboBox_GetDroppedState(hwnd)			((BOOL)SendMessageA((hwnd), CB_GETDROPPEDSTATE, 0, 0))
#define ComboBox_GetDroppedControlRect(hwnd, r)	((void)SendMessageA((hwnd), CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)(RECT *)(r)))
#define ComboBox_ShowDropdown(hwnd, show)		((BOOL)SendMessageA((hwnd), CB_SHOWDROPDOWN, (WPARAM)(BOOL)(show), 0))
#define ComboBox_SetItemHeight(hwnd, i, cy)		((int)SendMessageA((hwnd), CB_SETITEMHEIGHT, (WPARAM)(int)(i), (LPARAM)(int)(cy)))
#define ComboBox_GetItemHeight(hwnd, i)			((int)SendMessageA((hwnd), CB_GETITEMHEIGHT, (WPARAM)(int)(i), 0))

HWND CreateDialogParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc, LPARAM initparam);
HWND CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATE dialogtemplate, HWND parent, DLGPROC dialogproc, LPARAM initparam);
INT_PTR DialogBoxParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc, LPARAM initparam);
DWORD GetDialogBaseUnits(void);
BOOL EnumChildWindows(HWND parent, WNDENUMPROC callback, LPARAM parameter);
int GetClassNameA(HWND window, LPSTR classname, int count);
HWND ChildWindowFromPoint(HWND parent, POINT point);
HWND GetCapture(void);
BOOL IsChild(HWND parent, HWND window);
HMENU GetMenu(HWND window);
HMENU GetSystemMenu(HWND window, BOOL revert);
BOOL DeleteMenu(HMENU menu, UINT position, UINT flags);
BOOL EnableMenuItem(HMENU menu, UINT item, UINT enable);
BOOL DestroyCursor(HCURSOR cursor);
HBRUSH CreateSolidBrush(COLORREF color);
int SaveDC(HDC dc);
BOOL RestoreDC(HDC dc, int saved);
int SetGraphicsMode(HDC dc, int mode);
int GetDeviceCaps(HDC dc, int index);
HFONT CreateFontIndirectA(LOGFONTA const * font);
BOOL GetTextMetricsA(HDC dc, LPTEXTMETRICA metrics);
BOOL TextOutA(HDC dc, int x, int y, LPCSTR string, int count);
BOOL GetScrollInfo(HWND window, int bar, LPSCROLLINFO info);
int SetScrollInfo(HWND window, int bar, LPCSCROLLINFO info, BOOL redraw);
BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION information);
BOOL FileTimeToDosDateTime(FILETIME const * filetime, LPWORD dosdate, LPWORD dostime);
BOOL DosDateTimeToFileTime(WORD dosdate, WORD dostime, LPFILETIME filetime);
int GetTimeFormatA(LCID locale, DWORD flags, SYSTEMTIME const * time, LPCSTR format, LPSTR text, int count);
int GetDateFormatA(LCID locale, DWORD flags, SYSTEMTIME const * date, LPCSTR format, LPSTR text, int count);
BOOL SetStdHandle(DWORD handle, HANDLE value);
BOOL GetConsoleScreenBufferInfo(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFO info);
HWND GetConsoleWindow(void);
BOOL WriteConsoleA(HANDLE console, void const * buffer, DWORD towrite, LPDWORD written, LPVOID reserved);
HIMAGELIST ImageList_Create(int cx, int cy, UINT flags, int initial, int grow);
BOOL ImageList_Destroy(HIMAGELIST list);
BOOL ImageList_BeginDrag(HIMAGELIST list, int track, int hotspotx, int hotspoty);
void ImageList_EndDrag(void);
BOOL ImageList_DragEnter(HWND lock, int x, int y);
BOOL ImageList_DragLeave(HWND lock);
BOOL ImageList_DragMove(int x, int y);
BOOL ImageList_DragShowNolock(BOOL show);
#define CreateDialogParam		CreateDialogParamA
#define CreateDialogIndirectParam CreateDialogIndirectParamA
#define DialogBoxParam			DialogBoxParamA
#define GetClassName			GetClassNameA
#define CreateFontIndirect		CreateFontIndirectA
#define GetTextMetrics			GetTextMetricsA
#define TextOut					TextOutA
#define GetTimeFormat			GetTimeFormatA
#define GetDateFormat			GetDateFormatA
#define WriteConsole			WriteConsoleA

/*
** Structured storage's property sets, which the save-game header is written through.
*/
#define STGM_READ				0x00000000L
#define STGM_WRITE				0x00000001L
#define STGM_READWRITE			0x00000002L
#define STGM_SHARE_DENY_NONE	0x00000040L
#define STGM_SHARE_EXCLUSIVE	0x00000010L
#define STGM_CREATE				0x00001000L
#define STGM_DIRECT				0x00000000L

#define PRSPEC_LPWSTR	0
#define PRSPEC_PROPID	1

#define VT_EMPTY	0
#define VT_NULL		1
#define VT_I2		2
#define VT_I4		3
#define VT_BSTR		8
#define VT_BOOL		11
#define VT_LPSTR	30

typedef ULONG PROPID;

typedef struct tagPROPSPEC {
	ULONG ulKind;
	union {
		PROPID propid;
		LPOLESTR lpwstr;
	};
} PROPSPEC;

typedef struct tagPROPVARIANT {
	WORD vt;
	WORD wReserved1;
	WORD wReserved2;
	WORD wReserved3;
	union {
		LONG lVal;
		SHORT iVal;
		VARIANT_BOOL boolVal;
		LPSTR pszVal;
		LPWSTR pwszVal;
		FILETIME filetime;
		BSTR bstrVal;
		LONGLONG hVal;
	};
} PROPVARIANT, * LPPROPVARIANT;

struct IPropertyStorage : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE ReadMultiple(ULONG count, PROPSPEC const * specs, PROPVARIANT * values) = 0;
	virtual HRESULT STDMETHODCALLTYPE WriteMultiple(ULONG count, PROPSPEC const * specs, PROPVARIANT const * values, PROPID first) = 0;
	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD flags) = 0;
};

struct IPropertySetStorage : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE Create(REFGUID fmtid, CLSID const * classid, DWORD flags, DWORD mode, IPropertyStorage ** storage) = 0;
	virtual HRESULT STDMETHODCALLTYPE Open(REFGUID fmtid, DWORD mode, IPropertyStorage ** storage) = 0;
	virtual HRESULT STDMETHODCALLTYPE Delete(REFGUID fmtid) = 0;
};

EXTERN_C const IID IID_IPropertyStorage;
EXTERN_C const IID IID_IPropertySetStorage;
_COM_SMARTPTR_TYPEDEF(IPropertyStorage, IID_IPropertyStorage);
_COM_SMARTPTR_TYPEDEF(IPropertySetStorage, IID_IPropertySetStorage);

void PropVariantInit(PROPVARIANT * value);
HRESULT PropVariantClear(PROPVARIANT * value);

/*
** Winsock's Windows-only asynchronous request cancellation.
*/
int WSACancelAsyncRequest(HANDLE request);


/*
** What the crash handler, the console, the dialogs, and structured storage still need.
*/
#include <arpa/inet.h>

#define FORMAT_MESSAGE_ALLOCATE_BUFFER	0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS	0x00000200
#define FORMAT_MESSAGE_FROM_STRING		0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE		0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM		0x00001000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK	0x000000FF
#define LANG_NEUTRAL		0x00
#define SUBLANG_DEFAULT		0x01
#define MAKELANGID(primary, sub)	((WORD)((((WORD)(sub)) << 10) | (WORD)(primary)))

DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD messageid, DWORD languageid, LPSTR buffer, DWORD size, va_list * arguments);
#define FormatMessage	FormatMessageA

BOOL SetConsoleScreenBufferSize(HANDLE console, COORD size);

/*
** Structured exception handling. WebAssembly has no equivalent, and the __try blocks in
** except.cpp do not compile here at all; these declarations only let the report writer's
** own code parse.
*/
#define EXCEPTION_MAXIMUM_PARAMETERS			15
#define EXCEPTION_DATATYPE_MISALIGNMENT			((DWORD)0x80000002L)
#define EXCEPTION_BREAKPOINT					((DWORD)0x80000003L)
#define EXCEPTION_SINGLE_STEP					((DWORD)0x80000004L)
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED			((DWORD)0xC000008CL)
#define EXCEPTION_FLT_DENORMAL_OPERAND			((DWORD)0xC000008DL)
#define EXCEPTION_FLT_DIVIDE_BY_ZERO			((DWORD)0xC000008EL)
#define EXCEPTION_FLT_INEXACT_RESULT			((DWORD)0xC000008FL)
#define EXCEPTION_FLT_INVALID_OPERATION			((DWORD)0xC0000090L)
#define EXCEPTION_FLT_OVERFLOW					((DWORD)0xC0000091L)
#define EXCEPTION_FLT_STACK_CHECK				((DWORD)0xC0000092L)
#define EXCEPTION_FLT_UNDERFLOW					((DWORD)0xC0000093L)
#define EXCEPTION_INT_DIVIDE_BY_ZERO			((DWORD)0xC0000094L)
#define EXCEPTION_INT_OVERFLOW					((DWORD)0xC0000095L)
#define EXCEPTION_PRIV_INSTRUCTION				((DWORD)0xC0000096L)
#define EXCEPTION_IN_PAGE_ERROR					((DWORD)0xC0000006L)
#define EXCEPTION_ILLEGAL_INSTRUCTION			((DWORD)0xC000001DL)
#define EXCEPTION_NONCONTINUABLE_EXCEPTION		((DWORD)0xC0000025L)
#define EXCEPTION_STACK_OVERFLOW				((DWORD)0xC00000FDL)
#define EXCEPTION_INVALID_DISPOSITION			((DWORD)0xC0000026L)
#define EXCEPTION_GUARD_PAGE					((DWORD)0x80000001L)
#define EXCEPTION_INVALID_HANDLE				((DWORD)0xC0000008L)
#define STATUS_NO_MEMORY						((DWORD)0xC0000017L)

typedef struct _EXCEPTION_RECORD {
	DWORD ExceptionCode;
	DWORD ExceptionFlags;
	struct _EXCEPTION_RECORD * ExceptionRecord;
	PVOID ExceptionAddress;
	DWORD NumberParameters;
	ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, * PEXCEPTION_RECORD;

/*
** The x86 thread context. It is reproduced at its Win32 x86 shape because the crash
** report reads named registers out of it; wasm has no such registers and nothing ever
** fills one in.
*/
typedef struct _FLOATING_SAVE_AREA {
	DWORD ControlWord;
	DWORD StatusWord;
	DWORD TagWord;
	DWORD ErrorOffset;
	DWORD ErrorSelector;
	DWORD DataOffset;
	DWORD DataSelector;
	BYTE RegisterArea[80];
	DWORD Cr0NpxState;
} FLOATING_SAVE_AREA;

typedef struct _CONTEXT {
	DWORD ContextFlags;
	DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
	FLOATING_SAVE_AREA FloatSave;
	DWORD SegGs, SegFs, SegEs, SegDs;
	DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
	DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
	BYTE ExtendedRegisters[512];
} CONTEXT, * PCONTEXT, * LPCONTEXT;

typedef struct _EXCEPTION_POINTERS {
	PEXCEPTION_RECORD ExceptionRecord;
	PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, * PEXCEPTION_POINTERS, * LPEXCEPTION_POINTERS;

/*
** The rest of the dialog and control surface.
*/
#define LB_SELITEMRANGE		0x019B
#define LB_SELITEMRANGEEX	0x0183
#define LB_SETITEMHEIGHT	0x01A0
#define LB_GETITEMHEIGHT	0x01A1
#define LB_GETSELCOUNT		0x0190
#define LB_GETSELITEMS		0x0191

#define WC_TABCONTROL		"SysTabControl32"
#define HOTKEY_CLASS		"msctls_hotkey32"
#define UPDOWN_CLASS		"msctls_updown32"

#define BS_PUSHBUTTON		0x00000000L
#define BS_DEFPUSHBUTTON	0x00000001L
#define BS_CHECKBOX			0x00000002L
#define BS_AUTOCHECKBOX		0x00000003L
#define BS_RADIOBUTTON		0x00000004L
#define BS_3STATE			0x00000005L
#define BS_GROUPBOX			0x00000007L
#define BS_AUTORADIOBUTTON	0x00000009L
#define BS_OWNERDRAW		0x0000000BL

#define ES_LEFT				0x0000L
#define ES_CENTER			0x0001L
#define ES_RIGHT			0x0002L
#define ES_MULTILINE		0x0004L
#define ES_PASSWORD			0x0020L
#define ES_AUTOHSCROLL		0x0080L
#define ES_READONLY			0x0800L
#define ES_NUMBER			0x2000L

#define SYSTEM_FONT			13
#define ANSI_VAR_FONT		12
#define DEFAULT_GUI_FONT	17
#define NULL_BRUSH			5
#define WHITE_BRUSH			0
#define BLACK_BRUSH			4

#define OUT_RASTER_PRECIS	6
#define OUT_TT_PRECIS		4
#define PROOF_QUALITY		2
#define DRAFT_QUALITY		1
#define FF_DONTCARE			(0 << 4)
#define FF_ROMAN			(1 << 4)
#define FF_SWISS			(2 << 4)
#define FF_MODERN			(3 << 4)
#define VARIABLE_PITCH		2
#define FIXED_PITCH			1

#define TA_LEFT				0
#define TA_RIGHT			2
#define TA_CENTER			6
#define TA_TOP				0
#define TA_BOTTOM			8
#define TA_BASELINE			24

#define MWT_IDENTITY		1
#define MWT_LEFTMULTIPLY	2
#define MWT_RIGHTMULTIPLY	3

#define SWP_NOOWNERZORDER	0x0200
#define SWP_NOSENDCHANGING	0x0400
#define SWP_DRAWFRAME		SWP_FRAMECHANGED

#define SM_CXDRAG			68
#define SM_CYDRAG			69

#define RT_CURSOR			MAKEINTRESOURCEA(1)
#define RT_BITMAP			MAKEINTRESOURCEA(2)
#define RT_ICON				MAKEINTRESOURCEA(3)
#define RT_MENU				MAKEINTRESOURCEA(4)
#define RT_DIALOG			MAKEINTRESOURCEA(5)
#define RT_VERSION			MAKEINTRESOURCEA(16)

#define HELP_CONTEXT		0x0001L
#define HELP_CONTEXTPOPUP	0x0008L

typedef struct tagPOINTS {
	SHORT x;
	SHORT y;
} POINTS, * LPPOINTS;
#define MAKEPOINTS(l)	(*((POINTS *)&(l)))

typedef struct tagWINDOWPOS {
	HWND hwnd;
	HWND hwndInsertAfter;
	int x;
	int y;
	int cx;
	int cy;
	UINT flags;
} WINDOWPOS, * LPWINDOWPOS, * PWINDOWPOS;

typedef struct tagHELPINFO {
	UINT cbSize;
	int iContextType;
	int iCtrlId;
	HANDLE hItemHandle;
	DWORD_PTR dwContextId;
	POINT MousePos;
} HELPINFO, * LPHELPINFO;

typedef struct tagICONINFO {
	BOOL fIcon;
	DWORD xHotspot;
	DWORD yHotspot;
	HBITMAP hbmMask;
	HBITMAP hbmColor;
} ICONINFO, * PICONINFO;

/* BITMAPFILEHEADER is fourteen bytes: wingdi.h packs it to two. */
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
	WORD bfType;
	DWORD bfSize;
	WORD bfReserved1;
	WORD bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, * LPBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct _devicemodeA {
	BYTE dmDeviceName[32];
	WORD dmSpecVersion;
	WORD dmDriverVersion;
	WORD dmSize;
	WORD dmDriverExtra;
	DWORD dmFields;
	short dmOrientation;
	short dmPaperSize;
	short dmPaperLength;
	short dmPaperWidth;
	short dmScale;
	short dmCopies;
	short dmDefaultSource;
	short dmPrintQuality;
	short dmColor;
	short dmDuplex;
	short dmYResolution;
	short dmTTOption;
	short dmCollate;
	BYTE dmFormName[32];
	WORD dmLogPixels;
	DWORD dmBitsPerPel;
	DWORD dmPelsWidth;
	DWORD dmPelsHeight;
	DWORD dmDisplayFlags;
	DWORD dmDisplayFrequency;
} DEVMODEA, DEVMODE, * LPDEVMODEA, * LPDEVMODE;

#define ENUM_CURRENT_SETTINGS	((DWORD)-1)

BOOL CheckDlgButton(HWND dialog, int id, UINT check);
UINT IsDlgButtonChecked(HWND dialog, int id);
int GetDlgCtrlID(HWND window);
BOOL GetUpdateRect(HWND window, LPRECT rect, BOOL erase);
int GetBkMode(HDC dc);
COLORREF GetBkColor(HDC dc);
COLORREF GetTextColor(HDC dc);
UINT SetTextAlign(HDC dc, UINT align);
LRESULT CallWindowProcA(WNDPROC previous, HWND window, UINT message, WPARAM wparam, LPARAM lparam);
HWND WindowFromPoint(POINT point);
BOOL RegisterHotKey(HWND window, int id, UINT modifiers, UINT key);
BOOL UnregisterHotKey(HWND window, int id);
DWORD GetWindowContextHelpId(HWND window);
BOOL WinHelpA(HWND window, LPCSTR help, UINT command, ULONG_PTR data);
BOOL SetViewportOrgEx(HDC dc, int x, int y, LPPOINT previous);
BOOL SetWindowOrgEx(HDC dc, int x, int y, LPPOINT previous);
BOOL DPtoLP(HDC dc, LPPOINT points, int count);
BOOL LPtoDP(HDC dc, LPPOINT points, int count);
HBITMAP CreateBitmap(int width, int height, UINT planes, UINT bitsperpixel, void const * bits);
HICON CreateIconIndirect(PICONINFO info);
BOOL EnumDisplaySettingsA(LPCSTR devicename, DWORD mode, LPDEVMODEA devmode);
#define CallWindowProc		CallWindowProcA
#define WinHelp				WinHelpA
#define EnumDisplaySettings	EnumDisplaySettingsA

#define TVIF_TEXT			0x0001
#define TVIF_IMAGE			0x0002
#define TVIF_PARAM			0x0004
#define TVIF_STATE			0x0008
#define TVIF_HANDLE			0x0010
#define TVIF_SELECTEDIMAGE	0x0020
#define TVIF_CHILDREN		0x0040
#define TVE_COLLAPSE		0x0001
#define TVE_EXPAND			0x0002
#define TVE_TOGGLE			0x0003
#define TVM_CREATEDRAGIMAGE	(0x1100 + 18)
#define TVM_GETITEMRECT		(0x1100 + 4)
#define TVM_GETEDITCONTROL	(0x1100 + 15)
#define TVM_SELECTITEM_FIRSTVISIBLE	TVGN_FIRSTVISIBLE

typedef struct tagTVDISPINFOA {
	NMHDR hdr;
	TVITEMA item;
} NMTVDISPINFOA, NMTVDISPINFO, TV_DISPINFO, * LPNMTVDISPINFOA;

#define TreeView_CreateDragImage(hwnd, item) \
	((HIMAGELIST)SendMessageA((hwnd), TVM_CREATEDRAGIMAGE, 0, (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetItemRect(hwnd, item, rect, code) \
	(*(HTREEITEM *)(rect) = (item), (BOOL)SendMessageA((hwnd), TVM_GETITEMRECT, (WPARAM)(BOOL)(code), (LPARAM)(RECT *)(rect)))
#define TreeView_GetPrevVisible(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_PREVIOUS)
#define TreeView_GetNextVisible(hwnd, item)	TreeView_GetNextItem((hwnd), (item), TVGN_NEXTVISIBLE)
#define TreeView_SelectSetFirstVisible(hwnd, item) \
	((BOOL)SendMessageA((hwnd), TVM_SELECTITEM, TVGN_FIRSTVISIBLE, (LPARAM)(HTREEITEM)(item)))
#define TreeView_GetEditControl(hwnd) \
	((HWND)SendMessageA((hwnd), TVM_GETEDITCONTROL, 0, 0))

/*
** Structured storage. The save game is a compound file on both targets: OLE writes it under
** Windows and docfile.cpp writes the same container here.
*/
#define STGM_SHARE_DENY_READ	0x00000030L
#define STGM_SHARE_DENY_WRITE	0x00000020L
#define STGM_TRANSACTED			0x00010000L
#define STGM_DELETEONRELEASE	0x04000000L
#define PROPSETFLAG_DEFAULT		0
#define PROPSETFLAG_NONSIMPLE	1
#define PROPSETFLAG_ANSI		2
#define PID_DICTIONARY			0
#define PID_CODEPAGE			1
#define PID_FIRST_USABLE		2
#define VT_LPWSTR				31
#define CLSCTX_INPROC			(CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER)
#define CLASS_E_NOAGGREGATION	((HRESULT)0x80040110L)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#define REGCLS_SINGLEUSE		0
#define REGCLS_MULTIPLEUSE		1
#define REGCLS_MULTI_SEPARATE	2
#define STGC_DEFAULT			0
#define STATFLAG_DEFAULT		0
#define STATFLAG_NONAME			1

#define STG_E_INVALIDFUNCTION		((HRESULT)0x80030001L)
#define STG_E_FILENOTFOUND			((HRESULT)0x80030002L)
#define STG_E_PATHNOTFOUND			((HRESULT)0x80030003L)
#define STG_E_ACCESSDENIED			((HRESULT)0x80030005L)
#define STG_E_INSUFFICIENTMEMORY	((HRESULT)0x80030008L)
#define STG_E_WRITEFAULT			((HRESULT)0x8003001DL)
#define STG_E_READFAULT				((HRESULT)0x8003001EL)
#define STG_E_SHAREVIOLATION		((HRESULT)0x80030020L)
#define STG_E_FILEALREADYEXISTS		((HRESULT)0x80030050L)
#define STG_E_INVALIDPARAMETER		((HRESULT)0x80030057L)
#define STG_E_MEDIUMFULL			((HRESULT)0x80030070L)
#define STG_E_INVALIDHEADER			((HRESULT)0x800300FBL)
#define STG_E_INVALIDNAME			((HRESULT)0x800300FCL)
#define STG_E_UNKNOWN				((HRESULT)0x800300FDL)
#define STG_E_UNIMPLEMENTEDFUNCTION	((HRESULT)0x800300FEL)
#define STG_E_INVALIDFLAG			((HRESULT)0x800300FFL)

typedef GUID FMTID;
typedef OLECHAR ** SNB;
EXTERN_C const FMTID FMTID_SummaryInformation;
EXTERN_C const IID IID_IStorage;

struct IEnumSTATSTG;

struct IStorage : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE CreateStream(OLECHAR const * name, DWORD mode, DWORD reserved1, DWORD reserved2, IStream ** stream) = 0;
	virtual HRESULT STDMETHODCALLTYPE OpenStream(OLECHAR const * name, void * reserved1, DWORD mode, DWORD reserved2, IStream ** stream) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateStorage(OLECHAR const * name, DWORD mode, DWORD reserved1, DWORD reserved2, IStorage ** storage) = 0;
	virtual HRESULT STDMETHODCALLTYPE OpenStorage(OLECHAR const * name, IStorage * priority, DWORD mode, SNB exclude, DWORD reserved, IStorage ** storage) = 0;
	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD flags) = 0;
	virtual HRESULT STDMETHODCALLTYPE Revert(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE DestroyElement(OLECHAR const * name) = 0;
	virtual HRESULT STDMETHODCALLTYPE RenameElement(OLECHAR const * from, OLECHAR const * to) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetElementTimes(OLECHAR const * name, FILETIME const * creation, FILETIME const * access, FILETIME const * modify) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetClass(REFCLSID classid) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetStateBits(DWORD statebits, DWORD mask) = 0;
	virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG * statstg, DWORD statflag) = 0;
};
_COM_SMARTPTR_TYPEDEF(IStorage, IID_IStorage);

typedef IPersistStream * LPPERSISTSTREAM;
typedef IStorage * LPSTORAGE;

HRESULT StgCreateDocfile(OLECHAR const * name, DWORD mode, DWORD reserved, IStorage ** storage);
HRESULT StgOpenStorage(OLECHAR const * name, IStorage * priority, DWORD mode, void * exclude, DWORD reserved, IStorage ** storage);
HRESULT StgIsStorageFile(OLECHAR const * name);
HRESULT CoFileTimeNow(FILETIME * filetime);


/*
** The IP helper library, from iphlpapi.h. wspudp.cpp enumerates the host's adapters to
** pick a bind address.
*/
#define ERROR_BUFFER_OVERFLOW	111L
#define MAX_ADAPTER_NAME_LENGTH			256
#define MAX_ADAPTER_DESCRIPTION_LENGTH	128
#define MAX_ADAPTER_ADDRESS_LENGTH		8

typedef struct {
	char String[4 * 4];
} IP_ADDRESS_STRING, IP_MASK_STRING;

typedef struct _IP_ADDR_STRING {
	struct _IP_ADDR_STRING * Next;
	IP_ADDRESS_STRING IpAddress;
	IP_MASK_STRING IpMask;
	DWORD Context;
} IP_ADDR_STRING, * PIP_ADDR_STRING;

typedef struct _IP_ADAPTER_INFO {
	struct _IP_ADAPTER_INFO * Next;
	DWORD ComboIndex;
	char AdapterName[MAX_ADAPTER_NAME_LENGTH + 4];
	char Description[MAX_ADAPTER_DESCRIPTION_LENGTH + 4];
	UINT AddressLength;
	BYTE Address[MAX_ADAPTER_ADDRESS_LENGTH];
	DWORD Index;
	UINT Type;
	UINT DhcpEnabled;
	PIP_ADDR_STRING CurrentIpAddress;
	IP_ADDR_STRING IpAddressList;
	IP_ADDR_STRING GatewayList;
	IP_ADDR_STRING DhcpServer;
	BOOL HaveWins;
	IP_ADDR_STRING PrimaryWinsServer;
	IP_ADDR_STRING SecondaryWinsServer;
	int32_t LeaseObtained;
	int32_t LeaseExpires;
} IP_ADAPTER_INFO, * PIP_ADAPTER_INFO;

DWORD GetAdaptersInfo(PIP_ADAPTER_INFO adapters, PULONG size);


/*
** The MIDL-generated wolapi headers decorate their prototypes with these.
*/
#define __RPC_FAR
#define __RPC_USER
#define __RPC_STUB
#define __RPC_API
#define RPC_ENTRY

#define CONTROL_C_EXIT		((DWORD)0xC000013AL)
#define MUTEX_ALL_ACCESS	0x1F0001
#define WM_NCPAINT			0x0085
#define WM_NCCALCSIZE		0x0083
#define WM_NCACTIVATE		0x0086
#define HELP_CONTEXTMENU	0x000AL
#define VT_FILETIME			64

HANDLE OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
#define OpenMutex	OpenMutexA

HRESULT OleInitialize(LPVOID reserved);
void OleUninitialize(void);

BOOL GetTextExtentPoint32A(HDC dc, LPCSTR string, int count, LPSIZE size);
HFONT CreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strikeout, DWORD charset, DWORD outprecision, DWORD clipprecision, DWORD quality, DWORD pitchandfamily, LPCSTR face);
BOOL ModifyWorldTransform(HDC dc, void const * transform, DWORD mode);
#define GetTextExtentPoint32	GetTextExtentPoint32A
#define CreateFont				CreateFontA


/*
** The RPC proxy and stub interfaces the MIDL-generated wolapi header declares. Nothing
** marshals anything here; the declarations exist so the generated prototypes parse.
*/
struct IRpcChannelBuffer;
struct IRpcStubBuffer;
struct IRpcProxyBuffer;
typedef struct _RPC_MESSAGE RPC_MESSAGE;
typedef struct _RPC_MESSAGE * PRPC_MESSAGE;
typedef int32_t RPC_STATUS;

#define TCM_FIRST			0x1300
#define TCM_SETITEMSIZE		(TCM_FIRST + 41)
#define TCM_GETITEMCOUNT	(TCM_FIRST + 4)
#define TCM_GETCURSEL		(TCM_FIRST + 11)
#define TCM_SETCURSEL		(TCM_FIRST + 12)
#define TCM_ADJUSTRECT		(TCM_FIRST + 40)
#define TCM_SETPADDING		(TCM_FIRST + 43)

#define TreeView_GetRoot(hwnd)	TreeView_GetNextItem((hwnd), NULL, TVGN_ROOT)

/*
** MSVC's unreachability hint. Clang spells it differently, and it carries no meaning
** beyond optimization either way.
*/
#define __assume(condition)	__builtin_assume(condition)


#define WC_LISTVIEWA		"SysListView32"
#define WC_LISTVIEW			WC_LISTVIEWA
#define WC_TABCONTROLA		"SysTabControl32"

#define TabCtrl_GetItemCount(hwnd)		((int)SendMessageA((hwnd), TCM_GETITEMCOUNT, 0, 0))
#define TabCtrl_GetCurSel(hwnd)			((int)SendMessageA((hwnd), TCM_GETCURSEL, 0, 0))
#define TabCtrl_SetCurSel(hwnd, i)		((int)SendMessageA((hwnd), TCM_SETCURSEL, (WPARAM)(int)(i), 0))
#define TabCtrl_SetItemSize(hwnd, x, y)	((DWORD)SendMessageA((hwnd), TCM_SETITEMSIZE, 0, MAKELPARAM((x), (y))))
#define TabCtrl_AdjustRect(hwnd, larger, r) \
	((void)SendMessageA((hwnd), TCM_ADJUSTRECT, (WPARAM)(BOOL)(larger), (LPARAM)(RECT *)(r)))
#define TabCtrl_SetPadding(hwnd, cx, cy) \
	((void)SendMessageA((hwnd), TCM_SETPADDING, 0, MAKELPARAM((cx), (cy))))


#define DECLSPEC_UUID(uuid)
#define DECLSPEC_NOVTABLE
#define DECLSPEC_SELECTANY

#define TCM_GETITEMRECT		(TCM_FIRST + 10)
#define TabCtrl_GetItemRect(hwnd, i, rect) \
	((BOOL)SendMessageA((hwnd), TCM_GETITEMRECT, (WPARAM)(int)(i), (LPARAM)(RECT *)(rect)))

#define LVM_FIRST				0x1000
#define LVM_GETCOLUMNWIDTH		(LVM_FIRST + 29)
#define LVM_SETCOLUMNWIDTH		(LVM_FIRST + 30)
#define LVM_GETITEMCOUNT		(LVM_FIRST + 4)
#define LVM_DELETEALLITEMS		(LVM_FIRST + 9)
#define LVM_GETNEXTITEM			(LVM_FIRST + 12)
#define LVSCW_AUTOSIZE			(-1)
#define LVSCW_AUTOSIZE_USEHEADER (-2)

#define ListView_GetColumnWidth(hwnd, i) \
	((int)SendMessageA((hwnd), LVM_GETCOLUMNWIDTH, (WPARAM)(int)(i), 0))
#define ListView_SetColumnWidth(hwnd, i, cx) \
	((BOOL)SendMessageA((hwnd), LVM_SETCOLUMNWIDTH, (WPARAM)(int)(i), MAKELPARAM((cx), 0)))
#define ListView_GetItemCount(hwnd) \
	((int)SendMessageA((hwnd), LVM_GETITEMCOUNT, 0, 0))
#define ListView_DeleteAllItems(hwnd) \
	((BOOL)SendMessageA((hwnd), LVM_DELETEALLITEMS, 0, 0))


#define WM_CTLCOLORSCROLLBAR	0x0137
#define LBS_NOTIFY				0x0001L
#define LBS_SORT				0x0002L
#define LBS_MULTIPLESEL			0x0008L
#define LBS_OWNERDRAWFIXED		0x0010L
#define LBS_HASSTRINGS			0x0040L
#define LBS_EXTENDEDSEL			0x0800L

#define CBS_SIMPLE				0x0001L
#define CBS_DROPDOWN			0x0002L
#define CBS_DROPDOWNLIST		0x0003L

#define TVIS_SELECTED		0x0002
#define TVIS_CUT			0x0004
#define TVIS_DROPHILITED	0x0008
#define TVIS_BOLD			0x0010
#define TVIS_EXPANDED		0x0020
#define TVIS_EXPANDEDONCE	0x0040
#define TVIS_STATEIMAGEMASK	0xF000

#define TCIF_TEXT			0x0001
#define TCIF_IMAGE			0x0002
#define TCIF_PARAM			0x0008

typedef struct tagTCITEMA {
	UINT mask;
	DWORD dwState;
	DWORD dwStateMask;
	LPSTR pszText;
	int cchTextMax;
	int iImage;
	LPARAM lParam;
} TCITEMA, TC_ITEM, TCITEM, * LPTCITEMA;

#define TCM_GETITEMA		(TCM_FIRST + 5)
#define TCM_SETITEMA		(TCM_FIRST + 6)
#define TCM_INSERTITEMA		(TCM_FIRST + 7)
#define TabCtrl_GetItem(hwnd, i, item) \
	((BOOL)SendMessageA((hwnd), TCM_GETITEMA, (WPARAM)(int)(i), (LPARAM)(TC_ITEM *)(item)))
#define TabCtrl_SetItem(hwnd, i, item) \
	((BOOL)SendMessageA((hwnd), TCM_SETITEMA, (WPARAM)(int)(i), (LPARAM)(TC_ITEM *)(item)))
#define TabCtrl_InsertItem(hwnd, i, item) \
	((int)SendMessageA((hwnd), TCM_INSERTITEMA, (WPARAM)(int)(i), (LPARAM)(TC_ITEM const *)(item)))

typedef struct tagMSGBOXPARAMSA {
	UINT cbSize;
	HWND hwndOwner;
	HINSTANCE hInstance;
	LPCSTR lpszText;
	LPCSTR lpszCaption;
	DWORD dwStyle;
	LPCSTR lpszIcon;
	DWORD_PTR dwContextHelpId;
	void * lpfnMsgBoxCallback;
	DWORD dwLanguageId;
} MSGBOXPARAMSA, MSGBOXPARAMS, * LPMSGBOXPARAMSA;

#define MB_USERICON		0x00000080L
#define MB_HELP			0x00004000L

int MessageBoxIndirectA(MSGBOXPARAMSA const * parameters);
#define MessageBoxIndirect	MessageBoxIndirectA


#define LBS_NOSEL			0x4000L
#define LBS_NOINTEGRALHEIGHT 0x0100L
#define LB_FINDSTRINGEXACT	0x01A2
#define ListBox_FindStringExact(hwnd, i, text) \
	((int)SendMessageA((hwnd), LB_FINDSTRINGEXACT, (WPARAM)(int)(i), (LPARAM)(LPCSTR)(text)))

#define EM_POSFROMCHAR		0x00D6
#define EM_CHARFROMPOS		0x00D7
#define EM_LINEFROMCHAR		0x00C9
#define EM_LINEINDEX		0x00BB
#define WM_SYSDEADCHAR		0x0107
#define WM_UNICHAR			0x0109

HWND GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous);
HWND GetNextDlgGroupItem(HWND dialog, HWND control, BOOL previous);


#define SS_LEFT				0x00000000L
#define SS_CENTER			0x00000001L
#define SS_RIGHT			0x00000002L
#define SS_ICON				0x00000003L
#define SS_BLACKRECT		0x00000004L
#define SS_OWNERDRAW		0x0000000DL
#define SS_BITMAP			0x0000000EL
#define SS_NOPREFIX			0x00000080L
#define SS_NOTIFY			0x00000100L
#define SS_CENTERIMAGE		0x00000200L
#define SS_TYPEMASK			0x0000001FL


#define LB_GETITEMRECT		0x0198
#define ListBox_GetItemRect(hwnd, i, rect) \
	((int)SendMessageA((hwnd), LB_GETITEMRECT, (WPARAM)(int)(i), (LPARAM)(RECT *)(rect)))


#define WM_GETDLGCODE		0x0087
#define DLGC_WANTARROWS		0x0001
#define DLGC_WANTTAB		0x0002
#define DLGC_WANTALLKEYS	0x0004
#define DLGC_WANTMESSAGE	0x0004
#define DLGC_HASSETSEL		0x0008
#define DLGC_DEFPUSHBUTTON	0x0010
#define DLGC_BUTTON			0x2000
#define DLGC_WANTCHARS		0x0080

#define TB_LINEUP			0
#define TB_LINEDOWN			1
#define TB_PAGEUP			2
#define TB_PAGEDOWN			3
#define TB_THUMBPOSITION	4
#define TB_THUMBTRACK		5
#define TB_TOP				6
#define TB_BOTTOM			7
#define TB_ENDTRACK			8

#define DT_LEFT				0x00000000
#define DT_CENTER			0x00000001
#define DT_RIGHT			0x00000002
#define DT_TOP				0x00000000
#define DT_VCENTER			0x00000004
#define DT_BOTTOM			0x00000008
#define DT_WORDBREAK		0x00000010
#define DT_SINGLELINE		0x00000020
#define DT_CALCRECT			0x00000400
#define DT_NOPREFIX			0x00000800
#define DT_END_ELLIPSIS		0x00008000

int GetKeyNameTextA(LONG param, LPSTR buffer, int size);
int DrawTextA(HDC dc, LPCSTR text, int count, LPRECT rect, UINT format);
#define GetKeyNameText	GetKeyNameTextA
#define DrawText		DrawTextA


#define ODT_MENU		1
#define ODT_LISTBOX		2
#define ODT_COMBOBOX	3
#define ODT_BUTTON		4
#define ODT_STATIC		5
#define ODT_TAB			101
#define ODT_LISTVIEW	102

#define ODA_DRAWENTIRE	0x0001
#define ODA_SELECT		0x0002
#define ODA_FOCUS		0x0004

#define ODS_SELECTED	0x0001
#define ODS_GRAYED		0x0002
#define ODS_DISABLED	0x0004
#define ODS_CHECKED		0x0008
#define ODS_FOCUS		0x0010
#define ODS_DEFAULT		0x0020

#endif	// __EMSCRIPTEN__
