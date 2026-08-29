/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Stands in for wonline.cpp on the WebAssembly target, which leaves that file out: Westwood
// Online is reached through ATL and a COM chat client, and the service behind it was retired
// in 2004. Only what the rest of the engine still references is defined here; the file is
// empty everywhere else, and the MSVC build takes its definitions from wonline.cpp as before.
//
// The shared state below is genuinely zero rather than faked. A build that cannot reach the
// service is never in a Westwood Online game, so no tournament, no game id, and no pings is
// what is true, and the statistics and session code that reads it behaves accordingly.
//
// The entry points are a different matter, because each one did something. Following the
// convention win32compat.h sets, each names itself the first time it is reached and then
// returns what its original returns when nothing happened.

#include "always.h"

#include "win.h"
#include "wonline.h"

#if defined(__EMSCRIPTEN__)

#include <stdio.h>
#include <string.h>


char g_NickName[40];

char WestwoodOnline_Clan1_Players[136];
char WestwoodOnline_Clan2_Players[136];

int WestwoodOnline_StartTime = 0;
int WestwoodOnline_Tournament = 0;
int WestwoodOnline_GameID = 0;
int WestwoodOnline_GameSKU_TS = 0;
int WestwoodOnline_GameSKU_FS = 0;
int WestwoodOnline_GameSKU_WDT = 0;
char WestwoodOnline_LoginName[36];
char WestwoodOnline_UserName[16];

int g_SuspendChatPump;
int g_PingsSent;
int g_PingsReceived;

int g_MaxPlayers = 0;

char g_GameServerHost[128];
int g_GameServerPort;

// Declared where it is used, in windlg.cpp, rather than in wonline.h.
bool g_PlayingNetGame = false;


static char const * ReportedFunctions[16];
static int ReportedCount = 0;


static void Wonline_Stub(char const * function)
{
	for (int index = 0; index < ReportedCount; index++) {
		if (strcmp(ReportedFunctions[index], function) == 0) {
			return;
		}
	}

	if (ReportedCount < (int)(sizeof(ReportedFunctions) / sizeof(ReportedFunctions[0]))) {
		ReportedFunctions[ReportedCount++] = function;
	}

	fprintf(stderr, "OpenTS: %s belongs to Westwood Online, which this build does not have. "
		"It reports that nothing happened.\n", function);
	fflush(stderr);
}


#define WONLINE_STUB(value)	(::Wonline_Stub(__func__), (value))
#define WONLINE_STUB_VOID()	(::Wonline_Stub(__func__))


void DoFindPage(void)
{
	WONLINE_STUB_VOID();
}


void Encode_Game_Options(char * out)
{
	WONLINE_STUB_VOID();

	if (out != NULL) {
		out[0] = '\0';
	}
}


void SetPlayerAccepted(char * who, int status)
{
	(void)who;
	(void)status;
	WONLINE_STUB_VOID();
}


int GetPlayerAccepted(char * who)
{
	(void)who;
	return(WONLINE_STUB(0));
}


int Assign_House_And_Color(char * who, int house, int color)
{
	(void)who;
	(void)house;
	(void)color;

	// The original returns whether the house actually changed. Nothing changed here.
	return(WONLINE_STUB(0));
}


void Display_Users(void)
{
	WONLINE_STUB_VOID();
}


void Logout_WOnline(void)
{
	WONLINE_STUB_VOID();
}


WonlineResult Login_WOL(void)
{
	return(WONLINE_STUB(WONLINE_BACK));
}


int Join_WOL_Lobby(HWND win)
{
	(void)win;
	return(WONLINE_STUB(WONLINE_BACK));
}


bool Is_Channel_Owner(char * name)
{
	(void)name;
	return(WONLINE_STUB(false));
}


bool Request_WDT_Cycle(void)
{
	return(WONLINE_STUB(false));
}

#endif	// __EMSCRIPTEN__
