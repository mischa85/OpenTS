/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#pragma once

#include "vqaplay.h"

#ifdef _WIN32
#include <dsound.h>
#else
#include "win32compat.h"
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include "win32compat.h"
#endif

struct AhandleInitParams
{
	unsigned short SampleRate;
	unsigned char Channels;
	unsigned char BitsPerSample;
	unsigned int Flags;
	void *Callback1;
	void *Callback2;
};


#define AHANDLEF_IS_PAUSED (1 << 0)

struct Ahandle {

	enum {
		MAX_HANDLES = 1,
		MAX_BUFFERS = 2,
	};

	bool Used;
	unsigned int Volume;
	unsigned short SampleRate;
	unsigned char Channels;
	unsigned char BitsPerSample;
	char Reserved1[8];
	unsigned int LastPlaybackPosition;
	unsigned int LastTimerTick;
	unsigned int PauseAdjust;
	unsigned int TickCount;
	unsigned int InitFlags;
	unsigned int Flags;
	char Reserved2[20];
	int AudioBufReadIndex;
	int AudioBufWriteIndex;
	BOOL AudioBufInUse[MAX_BUFFERS];
	void * AudioBuf[MAX_BUFFERS];
	unsigned int AudioBufSize[MAX_BUFFERS];
	UINT TimerHandle;
	DSBUFFERDESC BufferDesc;
	WAVEFORMATEX DsBuffFormat;
	LPDIRECTSOUNDBUFFER SecondaryBufferPtr;
	unsigned int SecondaryBufferSize;
	unsigned int ChunksMovedToAudioBuffer;
	unsigned int LastChunkPosition;
	unsigned int EndLastAudioChunk;
	CRITICAL_SECTION CriticalSection;
	LONG SuspendAudioCallback;
};

unsigned int __cdecl Simple_Timer_Callback_Audio_Handler(VQAHandle *vqa);
unsigned int __cdecl Timer_Callback_Audio_Handler(VQAHandle *vqa);

long __cdecl Lock_Audio_Handler(void);
long __cdecl Unlock_Audio_Handler(void);
intptr_t __cdecl Stream_Audio_Handler(VQAHandle *vqa, long action, void *buffer, long nbytes);


typedef long (__cdecl *AHANDLE_CALLBACK_1)(VQAHandle *vqa);
typedef long (__cdecl *AHANDLE_CALLBACK_2)(VQAHandle *vqa, void *buffer);
