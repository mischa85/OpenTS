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

#include "always.h"

#include "ahandle.h"

#include "sounddriver.h"
#include "dbgprint.h"
#include "gametime.h"
#include "vqaplayp.h"

#include <cassert>

/// use of this is a bug..
#ifndef DSBCAPS_GETCURRENTPOSITION2
#define DSBCAPS_GETCURRENTPOSITION2	0x00010000
#endif

Ahandle _handles[Ahandle::MAX_HANDLES];


AHANDLE_CALLBACK_1 _AHandleCallbackFunc1;
AHANDLE_CALLBACK_2 _AHandleCallbackFunc2;


/// private, call handler instead

long __cdecl Open_Audio_Handler(VQAHandleP *vqap, AhandleInitParams *params, long);
long __cdecl Close_Audio_Handler(VQAHandleP *vqap);
long __cdecl Start_Audio_Handler(VQAHandleP *vqap);
long __cdecl Stop_Audio_Handler(VQAHandleP *vqap);
long __cdecl Play_Audio_Handler(VQAHandleP *vqap);
long __cdecl Pause_Audio_Handler(VQAHandleP *vqap);
long __cdecl Resume_Audio_Handler(VQAHandleP *vqap);
long __cdecl Load_Audio_Handler(VQAHandleP *vqap, void *buffer, long nbytes);
void CALLBACK AudioCallback(UINT uTimerID, UINT, DWORD dwUser, DWORD, DWORD);
_STATIC unsigned long Get_Playback_Position(VQAHandle *vqa, Ahandle *handle, VQAConfig *config);

_STATIC BOOL Move_HMI_Audio_Block_To_Ring(VQAHandleP *vqap);

unsigned long __cdecl Timer_Callback_Audio_Handler(VQAHandle *vqa)
{
	VQAHandleP    *vqap = (VQAHandleP *)vqa;
	VQAConfig     *config = &vqap->Config;
	Ahandle       *handle = &_handles[vqap->AudioHandleIndex];
	unsigned long d;
	unsigned long t;

	if (handle->Flags & AHANDLEF_IS_PAUSED) {
#ifdef _DEBUG
		DebugString("Ahandle: Paused %ld\n", handle->TickCount);
#endif
		return(handle->TickCount);
	}

	t = Simple_Timer_Callback_Audio_Handler(NULL) - handle->PauseAdjust;

	d = Get_Playback_Position(vqa, handle, config);

	if (d > 0 && d <= handle->LastPlaybackPosition) {
		if (t > handle->LastTimerTick) {
			d = (t - handle->LastTimerTick);
			handle->LastTimerTick = t;
			handle->TickCount += d;
		}
	} else {
		handle->LastPlaybackPosition = d;
		handle->LastTimerTick = t;
		handle->TickCount = (VQA_TIMETICKS * (d / (vqap->Channels * (vqap->BitsPerSample >> 3))) / vqap->SampleRate);

		if (handle->TickCount >= config->LatencyAdjustment) {
			handle->TickCount -= config->LatencyAdjustment;
		} else {
			handle->TickCount = 0;
		}
	}
	return(handle->TickCount);
}


unsigned long Get_Playback_Position(VQAHandle *vqa, Ahandle *audio, VQAConfig *config)
{
	VQAHandleP *vqap = (VQAHandleP *)vqa;

	assert(vqap != 0);
	assert(audio != 0);
	assert(config != 0);

	unsigned long s = audio->SecondaryBufferSize;
	assert(s > 0);

	unsigned long dma_diff;
	unsigned long totalbytes;
	DWORD				play_cursor;		//Position that the device is reading from
	DWORD				write_cursor;		//Position in buffer that we can write to

	EnterCriticalSection(&audio->CriticalSection);

	long r = vqap->RepeatedBuffers;
	long l = audio->LastChunkPosition;
	long m = audio->ChunksMovedToAudioBuffer;

	totalbytes = config->HMIBufSize * m;

	if (audio->SecondaryBufferPtr &&
		audio->SecondaryBufferPtr != nullptr) {
			play_cursor = (DWORD)Audio_Backend_Play_Cursor(audio->SecondaryBufferPtr);
			write_cursor = (DWORD)Audio_Backend_Write_Cursor(audio->SecondaryBufferPtr);

		if (l) {
			totalbytes += play_cursor;
		} else {
			if (play_cursor >= (s / 2)) {
				totalbytes += play_cursor - (s / 2);
			} else {
				if (totalbytes > 0) {
					totalbytes += play_cursor + (s / 2);
				}
			}
		}
	}

	LeaveCriticalSection(&audio->CriticalSection);

	dma_diff = totalbytes - config->HMIBufSize * r;
	if (dma_diff > totalbytes) {
		dma_diff = 0;
	}
	return(dma_diff);
}


long __cdecl Stream_Audio_Handler(VQAHandle *vqa, long action, void *buffer, long nbytes)
{
	VQAHandleP *vqap = (VQAHandleP *)vqa;
	VQAConfig *config;

	long error = VQAERR_NONE;

	switch (action) {

		case VQAAUDIO_INIT:
			config = &vqap->Config;
			config->TimerCallback = Simple_Timer_Callback_Audio_Handler;
			config->RefreshRate = VQA_TIMETICKS;
			break;

		case VQAAUDIO_OPEN:
			error = Open_Audio_Handler((VQAHandleP *)vqa, (AhandleInitParams *)buffer, nbytes);
			break;

		case VQAAUDIO_CLOSE:
			error = Close_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_START:
			error = Start_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_LOAD:
			error = Load_Audio_Handler((VQAHandleP *)vqa, buffer, nbytes);
			break;

		case VQAAUDIO_PAUSE:
			error = Pause_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_PLAY:
			error = Play_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_STOP:
			error = Stop_Audio_Handler((VQAHandleP *)vqa);
			break;
	}

	return(error);
}


long __cdecl Open_Audio_Handler(VQAHandleP *vqap, AhandleInitParams *params, long b)
{

	DebugString("Opening VQ audio handler\n");
	DebugString("Current thread ID is %08x\n", GetCurrentThreadId());

	if (!Audio_Available()) {
		return(VQAERR_AUDIO);
	}

	int index = 0;
	while (index < Ahandle::MAX_HANDLES) {
		if (!_handles[index].Used) {
			break;
		}
		index++;
	}

	if (index != 1 && b == 16) {

		VQAConfig *config = &vqap->Config;

		vqap->AudioHandleIndex = index;

		Ahandle *handle = &_handles[index];
		memset(handle, 0, sizeof(*handle));
		handle->Used = TRUE;
		handle->Volume = config->Volume;

		InitializeCriticalSection(&handle->CriticalSection);

		if (config->AudioRate != -1) {
			handle->SampleRate = config->AudioRate;
		} else {
			if (config->FrameRate != vqap->FrameRate) {
				handle->SampleRate = params->SampleRate * (unsigned)config->FrameRate / vqap->FrameRate;
			} else {
				handle->SampleRate = params->SampleRate;
			}
		}

		// The device carries each stream at the format it was opened with, so the movie's
		// rate is not something the output has to be talked into.
		config->LatencyAdjustment = 0;

		handle->Channels = params->Channels;
		handle->BitsPerSample = params->BitsPerSample;
		handle->InitFlags = params->Flags;
		_AHandleCallbackFunc1 = (AHANDLE_CALLBACK_1)params->Callback1;
		_AHandleCallbackFunc2 = (AHANDLE_CALLBACK_2)params->Callback2;

		DebugString("Calling timeBeginPeriod\n");
		if (timeBeginPeriod(1000/VQA_TIMETICKS) != TIMERR_NOCANDO) {
			DebugString("Creating VQ audio timer thread\n");
			// Set orf 60hz timer
			handle->TimerHandle = timeSetEvent ( 1000/VQA_TIMETICKS , 1 , AudioCallback , (DWORD)vqap , TIME_PERIODIC);
			DebugString("VQ audio handler opened OK\n");
			if (handle->TimerHandle != 0) {
				return(VQAERR_NONE);
			}
			return(VQAERR_AUDIO);
		}
	}
	return(VQAERR_AUDIO);
}


long __cdecl Close_Audio_Handler(VQAHandleP *vqap)
{
	DebugString("Closing VQ audio handler\n");

	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	if (handle->Used == 1) {

		if (handle->SecondaryBufferPtr != NULL) {
			DebugString("Stop_Audio_Handler\n");
			Stop_Audio_Handler(vqap);
		}

		DebugString("Calling timeKillEvent\n");
		timeKillEvent(handle->TimerHandle);
		handle->TimerHandle = 0;

		DebugString("Calling timeEndPeriod\n");
		timeEndPeriod(1000/VQA_TIMETICKS);

		handle->Used = false;

		DebugString("Deleting the critical section object\n");
		DeleteCriticalSection(&handle->CriticalSection);
	}

	DebugString("VQ audio handler closed OK\n");
	return(VQAERR_NONE);
}


long __cdecl Start_Audio_Handler(VQAHandleP *vqap)
{
	/* Dereference commonly used data members for quicker access. */
	VQAConfig *config = &vqap->Config;
	Ahandle *audio = &_handles[vqap->AudioHandleIndex];

	if (audio->Flags & AHANDLEF_IS_PAUSED) {
		return(Play_Audio_Handler(vqap));
	}

	EnterCriticalSection(&audio->CriticalSection);

	assert(audio->SecondaryBufferPtr == NULL);

	/*
	**	If we already have a ring then get rid of it
	*/
	if (audio->SecondaryBufferPtr != NULL){
		Audio_Backend_Stop(audio->SecondaryBufferPtr);
		Audio_Backend_Close_Stream(audio->SecondaryBufferPtr);
		audio->SecondaryBufferPtr = nullptr;
	}

	/*
	**	Make it big enough for 2 blocks of HMI data
	*/
	audio->SecondaryBufferSize = config->HMIBufSize*2;

	/*
	**	Define the format for the secondary sound buffer
	*/

	/*
	**	Open the ring the movie's audio is decoded into
	*/
	Audio.Lock_Mutex();
	audio->SecondaryBufferPtr = Audio_Backend_Open_Stream(audio->SecondaryBufferSize,
									audio->SampleRate, audio->BitsPerSample, audio->Channels);
	Audio.Unlock_Mutex();

	if (audio->SecondaryBufferPtr == NULL) {
		LeaveCriticalSection(&audio->CriticalSection);
		return(VQAERR_AUDIO);
	}

	/* Start playback */
	_AHandleCallbackFunc1((VQAHandle *)vqap);
	_AHandleCallbackFunc1((VQAHandle *)vqap);
	audio->AudioBufWriteIndex = 0;
	audio->EndLastAudioChunk = 0;
	audio->ChunksMovedToAudioBuffer = 0;
	Audio_Backend_Seek(audio->SecondaryBufferPtr, 0);

	/*
	**	Set the volume
	*/
	Audio_Backend_Set_Gain(audio->SecondaryBufferPtr, Gain_From_HMI_Volume(audio->Volume & 255));

	Audio_Backend_Start(audio->SecondaryBufferPtr);
	LeaveCriticalSection(&audio->CriticalSection);
	return(VQAERR_NONE);
}


long __cdecl Load_Audio_Handler(VQAHandleP *vqap, void *buffer, long nbytes)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	if (buffer != NULL && nbytes != 0) {
		EnterCriticalSection(&handle->CriticalSection);
		if (handle->AudioBufInUse[handle->AudioBufReadIndex] == TRUE) {
			LeaveCriticalSection(&handle->CriticalSection);
			return(VQAERR_AUDIO);
		}
		int readindex = handle->AudioBufReadIndex;
		handle->AudioBuf[readindex] = buffer;
		handle->AudioBufSize[readindex] = nbytes;
		handle->AudioBufInUse[readindex] = TRUE;
		Move_HMI_Audio_Block_To_Ring(vqap);
		int index = readindex + 1;
		if (index >= Ahandle::MAX_BUFFERS) {
			index = 0;
		}
		handle->AudioBufReadIndex = index;
		LeaveCriticalSection(&handle->CriticalSection);
		return(VQAERR_NONE);
	}
	return(VQAERR_AUDIO);
}


long __cdecl Pause_Audio_Handler(VQAHandleP *vqap)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	EnterCriticalSection(&handle->CriticalSection);

	if (handle->Used == true && handle->SecondaryBufferPtr != NULL) {
		Audio_Backend_Stop(handle->SecondaryBufferPtr);
	}
	handle->Flags |= AHANDLEF_IS_PAUSED;

	LeaveCriticalSection(&handle->CriticalSection);

	return(VQAERR_NONE);
}


long __cdecl Play_Audio_Handler(VQAHandleP *vqap)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	long rc;
	if (!handle->Used || handle->SecondaryBufferPtr == NULL) {
		return(VQAERR_AUDIO);
	}

	EnterCriticalSection(&handle->CriticalSection);

	Audio_Backend_Start(handle->SecondaryBufferPtr);

	handle->PauseAdjust = Simple_Timer_Callback_Audio_Handler(NULL) - handle->LastTimerTick;
	DebugString("Ahandle: PauseAdjust %ld\n", handle->PauseAdjust);
	handle->Flags &= ~AHANDLEF_IS_PAUSED;
	rc = VQAERR_NONE;
	LeaveCriticalSection(&handle->CriticalSection);

	return(rc);
}


long __cdecl Stop_Audio_Handler(VQAHandleP *vqap)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	if (handle->SecondaryBufferPtr != NULL) {

		EnterCriticalSection(&handle->CriticalSection);
		Audio_Backend_Stop(handle->SecondaryBufferPtr);
		Audio_Backend_Close_Stream(handle->SecondaryBufferPtr);
		handle->SecondaryBufferPtr = nullptr;
		handle->AudioBufReadIndex = 0;
		handle->AudioBufWriteIndex = 0;
		for (int i = 0; i < Ahandle::MAX_BUFFERS; i++) {
			handle->AudioBufInUse[i] = false;
		}

		LeaveCriticalSection(&handle->CriticalSection);
	}
	return(VQAERR_NONE);
}


/****************************************************************************
*
* NAME
*     AudioCallback - Sound system callback.
*
* SYNOPSIS
*     AudioCallback(DriverHandle, Action, SampleID)
*
*     void AudioCallback(WORD, WORD, WORD);
*
* FUNCTION
*     Our custom audio callback routine that services HMI.
*
* INPUTS
*     DriverHandle - HMI driver handle.
*     Action       - Action taken.
*     SampleID     - ID of sample.
*
* RESULT
*     NONE
*
****************************************************************************/
void CALLBACK AudioCallback ( UINT uTimerID, UINT, DWORD dwUser, DWORD, DWORD )
{
	Ahandle  	*audio;
	DWORD			play_cursor;		//Position that the device is reading from
	DWORD			write_cursor;		//Position in buffer that we can write to

	VQAHandle *vqa = (VQAHandle *)dwUser;
	VQAHandleP *vqap = (VQAHandleP *)dwUser;

	audio = &_handles[vqap->AudioHandleIndex];

	if (InterlockedIncrement(&audio->SuspendAudioCallback) != TRUE || (audio->Flags & AHANDLEF_IS_PAUSED)) {
		InterlockedDecrement(&audio->SuspendAudioCallback);
		return;
	}

	EnterCriticalSection(&audio->CriticalSection);

	if (!audio->SecondaryBufferPtr || audio->TimerHandle != uTimerID)  {
		LeaveCriticalSection(&audio->CriticalSection);
		InterlockedDecrement(&audio->SuspendAudioCallback);
		return;
	}

	if (!Audio_Backend_Is_Playing(audio->SecondaryBufferPtr)) {
		LeaveCriticalSection(&audio->CriticalSection);
		InterlockedDecrement(&audio->SuspendAudioCallback);
		return;
	}

	/*
	**	See if we are nearing the end of the meaningful data in the ring
	*/
	play_cursor = (DWORD)Audio_Backend_Play_Cursor(audio->SecondaryBufferPtr);
	write_cursor = (DWORD)Audio_Backend_Write_Cursor(audio->SecondaryBufferPtr);

	bool write_more = false;


	if (play_cursor < audio->EndLastAudioChunk){
		write_more = true;
	}else{
		if ((play_cursor >= audio->SecondaryBufferSize / 2) && audio->EndLastAudioChunk==0){
			write_more = true;
		}
	}

	/*
	**	See if we need to fill the buffer
	*/
	if (write_more == true) {
		_AHandleCallbackFunc2((VQAHandle *)vqa, audio->AudioBuf[audio->AudioBufWriteIndex]);
		audio->ChunksMovedToAudioBuffer++;
		int index = audio->AudioBufWriteIndex + 1;
		if (index >= Ahandle::MAX_BUFFERS) {
			index = 0;
		}
		audio->AudioBufInUse[audio->AudioBufWriteIndex] = FALSE;
		_AHandleCallbackFunc1((VQAHandle *)vqa);

		if (audio->AudioBufInUse[index]) {
			audio->AudioBufWriteIndex = index;
		}

	}
	LeaveCriticalSection(&audio->CriticalSection);
	InterlockedDecrement(&audio->SuspendAudioCallback);
}


/***********************************************************************************************
 * Move_HMI_Audio_Block_To_Ring -- moves an audio block which would have been played by HMI    *
 *                                 into a ring                                                 *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   BOOL was block moved                                                              *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    12/21/95 9:51AM ST : Created                                                             *
 *=============================================================================================*/
BOOL Move_HMI_Audio_Block_To_Ring(VQAHandleP *vqap)
{
	Ahandle  	*audio;

	LPVOID		play_buffer_ptr1;   //Beginning of locked area of buffer
	LPVOID		play_buffer_ptr2;   //Length of locked area in buffer
	DWORD			lock_length1;   //Beginning of second locked area in buffer
	DWORD			lock_length2;   //Length of second locked area in buffer
	unsigned		next_fill_pos;

	audio = &_handles[vqap->AudioHandleIndex];
	VQAConfig *config = &vqap->Config;


	if (audio->SecondaryBufferPtr == NULL) {
		return(0);
	}

	/*************************************************************************
	**
	**	Copy the data from the HMI play position into the ring
	**
	*/
	next_fill_pos = audio->EndLastAudioChunk;

	/*
	**	Lock the buffer to get a pointer to it
	*/
	unsigned char * ring = Audio_Backend_Ring(audio->SecondaryBufferPtr);
	if (ring == nullptr) return(FALSE);

	DWORD const ring_size = (DWORD)Audio_Backend_Ring_Size(audio->SecondaryBufferPtr);
	DWORD const want = (DWORD)config->HMIBufSize;

	lock_length1 = (want < ring_size - next_fill_pos) ? want : ring_size - next_fill_pos;
	lock_length2 = want - lock_length1;
	play_buffer_ptr1 = ring + next_fill_pos;
	play_buffer_ptr2 = ring;

	int index = audio->AudioBufReadIndex;

	/*
	**	Copy the HMI audio buffer to the ring
	*/
	if (audio->AudioBufSize[index] > lock_length1) {

		memcpy((char*)play_buffer_ptr1, audio->AudioBuf[index], lock_length1);

		if (audio->AudioBufSize[index] - lock_length1 > lock_length2) {
			memcpy(play_buffer_ptr2, audio->AudioBuf[index], lock_length2);
		} else {
			memcpy(play_buffer_ptr2, audio->AudioBuf[index], audio->AudioBufSize[index] - lock_length1);
		}

	} else {
		memcpy(play_buffer_ptr1, audio->AudioBuf[index], audio->AudioBufSize[index]);
	}


	/*
	**	Update our audio data pointers
	*/
	audio->LastChunkPosition = next_fill_pos;
	audio->EndLastAudioChunk = next_fill_pos + audio->AudioBufSize[index];
	if (audio->EndLastAudioChunk >= audio->SecondaryBufferSize) {
		audio->EndLastAudioChunk = 0;
	}

	return(TRUE);
}


unsigned long __cdecl Simple_Timer_Callback_Audio_Handler(VQAHandle *vqa)
{
	return(Get_Game_Time_50());
}


void Pause_All_Audio_Handler(void)
{
	for (int i = 0; i < Ahandle::MAX_HANDLES; i++) {
		Ahandle *handle = &_handles[i];
		if (handle->Used == true && handle->SecondaryBufferPtr != NULL) {

			EnterCriticalSection(&handle->CriticalSection);

			Audio_Backend_Stop(handle->SecondaryBufferPtr);
			handle->Flags |= AHANDLEF_IS_PAUSED;

			LeaveCriticalSection(&handle->CriticalSection);

		}
	}
}


void Resume_All_Audio_Handler(void)
{
	for (int i = 0; i < Ahandle::MAX_HANDLES; i++) {
		Ahandle *handle = &_handles[i];
		if (handle->Used == true && handle->SecondaryBufferPtr != NULL) {

			EnterCriticalSection(&handle->CriticalSection);

			Audio_Backend_Start(handle->SecondaryBufferPtr);
			handle->Flags &= ~AHANDLEF_IS_PAUSED;

			LeaveCriticalSection(&handle->CriticalSection);

		}
	}
}


long __cdecl Lock_Audio_Handler(void)
{
	/// nothing in win32
	return(1);
}


long __cdecl Unlock_Audio_Handler(void)
{
	/// nothing in win32
	return(1);
}
