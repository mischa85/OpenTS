/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stands in for <winsock.h> on the WebAssembly target. Winsock 1.1 is BSD sockets with
// Windows spellings over the top, and Emscripten provides BSD sockets, so this maps onto
// the host headers rather than inventing a stack: SOCKET is a descriptor, the address
// structures are the POSIX ones, and the byte-order helpers are the POSIX ones.
//
// What does not map is the parts of Winsock that are Windows rather than sockets --
// startup and shutdown, the asynchronous window-message notifications, ioctlsocket -- and
// those are stubs. Emscripten's sockets are also WebSocket-backed and neither raw IPX nor
// UDP survives that; the engine's network play does not work here, and this header does
// not pretend otherwise.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "win32compat.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


typedef int SOCKET;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr * PSOCKADDR;
typedef struct sockaddr * LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in * PSOCKADDR_IN;
typedef struct sockaddr_in * LPSOCKADDR_IN;
typedef struct in_addr IN_ADDR;
typedef struct in_addr * LPIN_ADDR;
typedef struct hostent HOSTENT;
typedef struct hostent * LPHOSTENT;
typedef struct servent SERVENT;
typedef struct servent * LPSERVENT;
typedef struct protoent PROTOENT;
typedef struct protoent * LPPROTOENT;
typedef fd_set FD_SET;
typedef fd_set * LPFD_SET;
typedef struct timeval TIMEVAL;
typedef struct timeval * LPTIMEVAL;

#define INVALID_SOCKET	((SOCKET)(-1))
#define SOCKET_ERROR	(-1)

#ifndef WSADESCRIPTION_LEN
#define WSADESCRIPTION_LEN	256
#define WSASYS_STATUS_LEN	128
#endif

typedef struct WSAData {
	WORD wVersion;
	WORD wHighVersion;
	char szDescription[WSADESCRIPTION_LEN + 1];
	char szSystemStatus[WSASYS_STATUS_LEN + 1];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char * lpVendorInfo;
} WSADATA, * LPWSADATA;

#define MAKEWORD_WSA(low, high)	MAKEWORD(low, high)

/*
** Winsock's error numbers are the BSD ones offset by 10000. The mapping only matters
** where the engine compares against a name, so the names are what is provided.
*/
#define WSABASEERR			10000
#define WSAEINTR			(WSABASEERR + 4)
#define WSAEBADF			(WSABASEERR + 9)
#define WSAEACCES			(WSABASEERR + 13)
#define WSAEFAULT			(WSABASEERR + 14)
#define WSAEINVAL			(WSABASEERR + 22)
#define WSAEMFILE			(WSABASEERR + 24)
#define WSAEWOULDBLOCK		(WSABASEERR + 35)
#define WSAEINPROGRESS		(WSABASEERR + 36)
#define WSAEALREADY			(WSABASEERR + 37)
#define WSAENOTSOCK			(WSABASEERR + 38)
#define WSAEDESTADDRREQ		(WSABASEERR + 39)
#define WSAEMSGSIZE			(WSABASEERR + 40)
#define WSAEADDRINUSE		(WSABASEERR + 48)
#define WSAEADDRNOTAVAIL	(WSABASEERR + 49)
#define WSAENETDOWN			(WSABASEERR + 50)
#define WSAENETUNREACH		(WSABASEERR + 51)
#define WSAECONNABORTED		(WSABASEERR + 53)
#define WSAECONNRESET		(WSABASEERR + 54)
#define WSAENOBUFS			(WSABASEERR + 55)
#define WSAEISCONN			(WSABASEERR + 56)
#define WSAENOTCONN			(WSABASEERR + 57)
#define WSAETIMEDOUT		(WSABASEERR + 60)
#define WSAECONNREFUSED		(WSABASEERR + 61)
#define WSAEHOSTUNREACH		(WSABASEERR + 65)
#define WSASYSNOTREADY		(WSABASEERR + 91)
#define WSAVERNOTSUPPORTED	(WSABASEERR + 92)
#define WSANOTINITIALISED	(WSABASEERR + 93)
#define WSAHOST_NOT_FOUND	(WSABASEERR + 1001)

#define FD_READ			0x01
#define FD_WRITE		0x02
#define FD_OOB			0x04
#define FD_ACCEPT		0x08
#define FD_CONNECT		0x10
#define FD_CLOSE		0x20

#define closesocket(s)	::close(s)

typedef struct linger LINGER;

#define WSAGETSELECTEVENT(l)	LOWORD(l)
#define WSAGETSELECTERROR(l)	HIWORD(l)

/*
** Winsock measures option and address lengths in int where POSIX uses socklen_t. Both are
** four bytes, but they are distinct types, so these overloads let the calls the tree
** already writes resolve. They forward to the host's sockets and are not stubs.
*/
inline int getsockopt(SOCKET socket, int level, int name, void * value, int * length)
{
	socklen_t size = (socklen_t)*length;
	int result = ::getsockopt(socket, level, name, value, &size);

	*length = (int)size;
	return(result);
}


inline int recvfrom(SOCKET socket, void * buffer, int length, int flags, struct sockaddr * from, int * fromlength)
{
	socklen_t size = (socklen_t)*fromlength;
	int result = (int)::recvfrom(socket, buffer, (size_t)length, flags, from, &size);

	*fromlength = (int)size;
	return(result);
}


inline int accept(SOCKET socket, struct sockaddr * address, int * length)
{
	socklen_t size = (socklen_t)*length;
	int result = ::accept(socket, address, &size);

	*length = (int)size;
	return(result);
}


inline int getsockname(SOCKET socket, struct sockaddr * address, int * length)
{
	socklen_t size = (socklen_t)*length;
	int result = ::getsockname(socket, address, &size);

	*length = (int)size;
	return(result);
}

/*
** The Windows-only half of Winsock.
*/
int WSAStartup(WORD version, LPWSADATA data);
int WSACleanup(void);
int WSAGetLastError(void);
void WSASetLastError(int error);
int WSAAsyncSelect(SOCKET socket, HWND window, unsigned int message, long events);
int ioctlsocket(SOCKET socket, long command, unsigned long * argument);

#endif	// __EMSCRIPTEN__
