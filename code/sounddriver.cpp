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

#include "sounddriver.h"

#include "ccfile.h"
#include "data.h"
#include "dbgprint.h"
#include "globals.h" // for GameInFocus
#include "language\language.h"
#include "soscomp.h"
#include "winfix.h"

#include <algorithm>
#include <math.h> // for log10f


/*
**	If this is defined, then the streaming audio buffer will be filled
**	to maximum whenever filling is to occur. If undefined, it will fill
**	the streaming buffer in smaller chunks.
*/
#define SIMPLE_FILLING

/*
**	This is the number of times per sec that the maintenance callback gets called.
*/
#define MAINTENANCE_RATE                        40	//30 times per sec plus a safety margin

/*
**	Size of the temporary buffer in XMS/EMS that direct file
**	streaming of sounds will allocate.
*/
//#define STREAM_BUFFER_SIZE              (128L*1024L)
#define STREAM_BUFFER_SIZE              (128L*1024L)

/*
**	Define the number of "StreamBufferSize" blocks that are read in
**	at a minimum when the streaming sample load callback routine
**	is called.  We will IGNORE loads that are less that this in order
**	to avoid constant seeking on the CD.
*/
#define STREAM_CUSHION_BLOCKS   4


/*
**	This is the maximum size that a sonarc block can be.  All sonarc blocks
**	must be either a multiple of this value or a binary root of this value.
*/
#define LARGEST_SONARC_BLOCK            2048


SoundDriver Audio;

#define LOCK_GLOBAL_MUTEX() \
	if (!Lock_Global_Mutex()) { \
		DebugString("Warning: Probable deadlock occurred on GlobalAudioMutex. %s, line %d\n", __FILE__, __LINE__); \
	} \

#define UNLOCK_GLOBAL_MUTEX() Unlock_Global_Mutex()


#define LOCK_SECONDARY_MUTEX(_handle) \
	if (WaitForSingleObject(SecondaryBufferMutexes[_handle], MUTEX_TIMEOUT) == WAIT_TIMEOUT) { \
		DebugString("Warning: Probable deadlock occurred on secondary buffer mutex %d. %s, line %d\n", _handle, __FILE__, __LINE__); \
	} \

#define UNLOCK_SECONDARY_MUTEX(_handle) ReleaseMutex(SecondaryBufferMutexes[_handle]);


#define LOCK_STREAMING_SECONDARY_MUTEX(_handle) \
	if (WaitForSingleObject(SecondaryBufferMutexes[_handle], MUTEX_TIMEOUT) == WAIT_TIMEOUT) { \
		DebugString("Warning: Probable deadlock occurred on streaming secondary buffer mutex %d. %s, line %d\n", _handle, __FILE__, __LINE__); \
	} \

#define UNLOCK_STREAMING_SECONDARY_MUTEX(_handle) ReleaseMutex(SecondaryBufferMutexes[_handle]);


#define _LOCK_SECONDARY_MUTEX(_handle) \
	if (WaitForSingleObject(Audio.SecondaryBufferMutexes[_handle], SoundDriver::MUTEX_TIMEOUT) == WAIT_TIMEOUT) { \
		DebugString("Warning: Probable deadlock occurred on secondary buffer mutex %d. %s, line %d\n", _handle, __FILE__, __LINE__); \
	} \

#define _UNLOCK_SECONDARY_MUTEX(_handle) ReleaseMutex(Audio.SecondaryBufferMutexes[_handle]);


#define LOCK_ALL_MUTEX() \
	if (WaitForMultipleObjects(MUTEX_COUNT, AllAudioMutexes, true, SoundDriver::MUTEX_TIMEOUT) == WAIT_TIMEOUT) { \
		DebugString("Warning: Probable deadlock occurred on multiple audio mutexes. %s, line %d\n", __FILE__, __LINE__); \
	} \


/***********************************************************************************************
 * Convert_HMI_To_Direct_Sound_Volume -- Converts a linear volume value into an expotential    *
 *                                        value                                                *
 *                                                                                             *
 * This function converts a linear C&C volume in the range 0-255 (255 loudest) to a direct     *
 *  sound volume in the range 0 to -10000 (with 0 being the loadest)                           *
 *                                                                                             *
 * INPUT:    volume in range 0-255                                                             *
 *                                                                                             *
 * OUTPUT:   volume in range -10000 to 0                                                       *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * Note: The 27.685 value comes from 255 divided by the log of 10001                           *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    9/18/96 11:36AM ST : Created                                                             *
 *=============================================================================================*/
float Gain_From_HMI_Volume(int volume)
{
	if (volume <= 0) return(0.0f);
	if (volume >= 255) return(1.0f);

	/*
	** The curve is the one the driver has always used: a linear volume taken to decibels
	** across a hundred decibel range, which reduces to this exponent once the decibels are
	** turned back into a multiplier. The floor is that range's bottom.
	*/
	float const gain = powf((float)volume / 255.0f, 5.0f / 3.0f);
	return((gain < 1.0e-5f) ? 1.0e-5f : gain);
}


/***************************************************************************
 * ADD_LONG_TO_POINTER -- Adds an offset to a ptr casted void              *
 *                                                                         *
 * INPUT:      void * ptr - the pointer to add to                          *
 *               long size  - the size to add to it                        *
 *                                                                         *
 * OUTPUT:     void * ptr - the new location it will point to              *
 *                                                                         *
 * HISTORY:                                                                *
 *   06/23/1995 PWG : Created.                                             *
 *=========================================================================*/

static void *Audio_Add_Long_To_Pointer(void const *ptr, int size)
{
	return((void *) ( (char const *) ptr + size));
}


/// <summary>
/// Constructor for the sound driver.
/// This routine allocates the file streaming buffer and creates the mutexes that guard
/// the sample trackers. The sound hardware itself is not touched until Init is called.
/// </summary>
SoundDriver::SoundDriver(void)
{
	Audio_Focus_Loss_Function = NULL;

	DeviceOpen = false;

	StreamLowImpact = false;

	MagicNumber = 0xDEAF;

	StreamBufferSize = (SECONDARY_BUFFER_SIZE / 4) + 128;
	FileStreamBuffer = new char[StreamBufferSize * STREAM_BUFFER_COUNT];

	IsStreamBufferClaimed = false;

	SoundVolume = 255;

	SoundTimerHandle = NULL;

	TimerResolution = 0;



	TimerMutex = CreateMutex(0, 0, 0);
	GlobalAudioMutex = CreateMutex(0, 0, 0);

	memset(SampleTracker, 0, sizeof(SampleTracker));

	for (int index = 0; index < MAX_SFX; index++) {
		SecondaryBufferMutexes[index] = CreateMutex(NULL, 0, NULL);
	}

	AudioDone = false;
}


/// <summary>
/// Destructor for the sound driver.
/// This routine shuts the audio system down if it is still running, then gives back the
/// working buffers and every mutex the driver created.
/// </summary>
SoundDriver::~SoundDriver(void)
{
	if (!AudioDone) {
		End();
	}

	LOCK_ALL_MUTEX();

	if (FileStreamBuffer != NULL) {
		delete FileStreamBuffer;
		FileStreamBuffer = NULL;
	}





	for (int index = 0; index < MAX_SFX; index++) {
		ReleaseMutex(SecondaryBufferMutexes[index]);
		CloseHandle(SecondaryBufferMutexes[index]);
	}

	Unlock_Global_Mutex();
	CloseHandle(GlobalAudioMutex);
	CloseHandle(TimerMutex);
}


/***********************************************************************************************
 * Audio_Init -- Initialise the sound system                                                   *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    window - window to send callback messages to                                      *
 *           maximum bits_per_sample - 8 or 16                                                 *
 *           stereo - will stereo samples be played                                            *
 *           rate - maximum sample rate required                                               *
 *           reverse_channels                                                                  *
 *                                                                                             *
 * OUTPUT:   TRUE if correctly initialised                                                     *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   Unknown....                                                                               *
 *   08-24-95 10:01am ST : Modified for Windows 95 Direct Sound                                *
 *=============================================================================================*/

bool SoundDriver::Init( HWND window , int bits_per_sample, bool stereo , int rate /*, int reverse_channels*/)
{
	int index;

	if (!DeviceOpen) {

		LOCK_GLOBAL_MUTEX();

		if (!Audio_Backend_Init()) {
			DebugString("Failed to open an audio device\n");
			Print_Sound_Error(Fetch_String(TXT_DSOUND_CANT_CREATE), window);
			UNLOCK_GLOBAL_MUTEX();
			return(FALSE);
		}

		DeviceOpen = true;

		TIMECAPS tc;
		if (timeGetDevCaps(&tc, sizeof(tc)) != TIMERR_NOERROR) {
			DebugString("Error - Failed to obtain timer resolution caps\n");
			TimerResolution = TIMER_WORST_RESOLUTION;
		} else {
			TimerResolution = std::min(std::max(tc.wPeriodMin, (unsigned int)TIMER_TARGET_RESOLUTION), tc.wPeriodMax);
		}

		DebugString("Audio timer resolution is %d milliseconds\n", TimerResolution);

		timeBeginPeriod(TimerResolution);

		/*
		**	Initialise the Windows timer system to provide us with a callback
		**
		*/
		SoundTimerHandle = timeSetEvent ( 1000/MAINTENANCE_RATE , 1 , Sound_Timer_Callback , 0 , TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
		AudioDone = FALSE;

		/*
		**	One ring for each simultaneous sound effect. The device carries each of them at
		**	its own rate and width, so there is no one output format to negotiate down to.
		*/
		for (index = 0; index < MAX_SFX; index++) {
			SampleTrackerType *st = &SampleTracker[index];
			st->PlayBuffer		= Audio_Backend_Open_Stream(SECONDARY_BUFFER_SIZE, rate,
										bits_per_sample, stereo ? 2 : 1);
			st->PlaybackRate	= rate;
			st->Stereo			= (stereo) ? AUD_FLAG_STEREO : 0;
			st->BitSize 		= (bits_per_sample == 16) ? AUD_FLAG_16BIT : 0;
			st->FileHandle 	= NULL;
			st->QueueBuffer 	= NULL;
			st->FileBuffer	 = NULL;
		}

		UNLOCK_GLOBAL_MUTEX();
	}

	return(TRUE);
}


/***********************************************************************************************
 * Sound_End -- Uninitializes the sound driver.                                                *
 *                                                                                             *
 *    This routine will uninitialize the sound driver (if any was                              *
 *    installed).  This routine must be called at program termination                          *
 *    time.                                                                                    *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/23/1991 JLB : Created.                                                                 *
 *   11/02/1995 ST  : Modified for Direct Sound                                                *
 *=============================================================================================*/
void SoundDriver::End(void)
{

	int	index;

	if (WaitForSingleObject(TimerMutex, MUTEX_TIMEOUT) == WAIT_TIMEOUT) {
		DebugString("Warning: Probable deadlock occurred on TimerMutex. %s, line %d\n", __FILE__, __LINE__);
	}

	/*
	**	Remove the Windows timer event we installed for the sound callback
	*/
	if (SoundTimerHandle){
		timeKillEvent(SoundTimerHandle);
		SoundTimerHandle = 0;
		timeEndPeriod(TimerResolution);
	}

	if (DeviceOpen){
		if (WaitForMultipleObjects(MAX_SFX, SecondaryBufferMutexes, true, MUTEX_TIMEOUT) == WAIT_TIMEOUT) {
			DebugString("Warning: Probable deadlock occurred on secondary sound buffer mutexes. %s, line %d\n", __FILE__, __LINE__);
		}

		/*
		**	Stop all sounds and close the rings they were playing out of
		*/
		for (index=0 ; index < MAX_SFX; index++){
			if ( SampleTracker[index].PlayBuffer ){
				Stop_Sample (index);
				Audio_Backend_Close_Stream(SampleTracker[index].PlayBuffer);
				SampleTracker[index].PlayBuffer = nullptr;
			}
			if (SampleTracker[index].FileBuffer != NULL && SampleTracker[index].FileBuffer != FileStreamBuffer) {
				delete SampleTracker[index].FileBuffer;
			}
			SampleTracker[index].FileBuffer = NULL;

			ReleaseMutex(SecondaryBufferMutexes[index]);
		}
	}

	LOCK_GLOBAL_MUTEX();

	/*if (FileStreamBuffer){
		Free (FileStreamBuffer);
		FileStreamBuffer = NULL;
	}*/

	if (DeviceOpen){
		Audio_Backend_Shutdown();
		DeviceOpen = false;
	}

	if (FileStreamBuffer != NULL) {
		delete FileStreamBuffer;
		FileStreamBuffer = NULL;
	}

	IsStreamBufferClaimed = false;
	AudioDone = true;

	/*
	**	Since the timer has stopped, we are finished with our global critical section.
	*/
	//DeleteCriticalSection(&GlobalAudioCriticalSection);

	UNLOCK_GLOBAL_MUTEX();

	ReleaseMutex(TimerMutex);
}


/***********************************************************************************************
 * Stop_Sample -- Stops any currently playing sampled sound.                                   *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/02/1992 JLB : Created.                                                                 *
 *   11/2/95 4:09PM ST : Modified for Direct Sound                                             *
 *=============================================================================================*/
void SoundDriver::Stop_Sample(int handle)
{
	if (DeviceOpen && !AudioDone && (unsigned)handle < MAX_SFX) {

		//EnterCriticalSection (&GlobalAudioCriticalSection);
		LOCK_SECONDARY_MUTEX(handle);

		SampleTrackerType *st = &SampleTracker[handle];

		if (st->Active || st->Loading) {

			st->Active = FALSE;


			/*
			**	Stop the sample if it is playing.
			*/
			if (!st->Loading) {
				Audio_Backend_Stop(st->PlayBuffer);
			}

			/*
			**	If this is a streaming sample, then close the source file.
			*/
			if (st->FileHandle != NULL) {
				st->FileHandle->Close();
				delete st->FileHandle;
				st->FileHandle = NULL;
			}

			if (st->FileBuffer != NULL) {
				if (st->FileBuffer != FileStreamBuffer) {
					delete st->FileBuffer;
				} else {
					IsStreamBufferClaimed = false;
				}
				st->FileBuffer = NULL;
			}
			st->Loading = false;
			st->Priority = 0;
			st->FilePending = 0;
			st->FilePendingSize = 0;
			st->QueueBuffer = NULL;
			st->Callback = NULL;
		}
		//LeaveCriticalSection (&GlobalAudioCriticalSection);
		UNLOCK_SECONDARY_MUTEX(handle);
	}
}


/***********************************************************************************************
 * Sample_Status -- Queries the current playing sample status (if any).                        *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/02/1992 JLB : Created.                                                                 *
 *=============================================================================================*/
bool SoundDriver::Sample_Status(int handle)
{
	if (!DeviceOpen || AudioDone) return(FALSE);

	/*
	**	If its an invalid handle or we do not have a sound driver then
	**	the sample in question is not playing.
	*/
	if ((unsigned)handle >= MAX_SFX) return(FALSE);

	/*
	**	If the sample is loading, then for all intents and purposes the
	**	sample is playing.
	*/
	if (SampleTracker[handle].Loading) return(TRUE);

	/*
	**	If the sample is not active, then it is not playing
	*/
	if (!SampleTracker[handle].Active) return(FALSE);

	LOCK_SECONDARY_MUTEX(handle);

	/*
	**	If we made it this far, then the Sample is still playing if sos says
	**	that it is.
	*/
	bool const playing = Audio_Backend_Is_Playing(SampleTracker[handle].PlayBuffer);
	UNLOCK_SECONDARY_MUTEX(handle);

	return(playing);
}


/***********************************************************************************************
 * Is_Sample_Playing -- returns the play state of a sample                                     *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    ptr to sample data                                                                *
 *                                                                                             *
 * OUTPUT:   TRUE if sample is currently playing                                               *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/2/95 4:11PM ST : Commented                                                            *
 *=============================================================================================*/

bool SoundDriver::Is_Sample_Playing(void const * sample)
{
	int index;

//	if (AudioDone) return (FALSE);

	//EnterCriticalSection(&GlobalAudioCriticalSection);

	if (!sample) {
		//LeaveCriticalSection(&GlobalAudioCriticalSection);
		return(FALSE);
	}
	for (index = 0; index < MAX_SFX; index++) {
		if (SampleTracker[index].Original == sample && Sample_Status(index)) {
			//LeaveCriticalSection(&GlobalAudioCriticalSection);
			return(TRUE);
		}
	}
	//LeaveCriticalSection(&GlobalAudioCriticalSection);
	return(FALSE);
}


/***********************************************************************************************
 * Stop_Sample_Playing -- stops a playing sample                                               *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    ptr to sample data                                                                *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/2/95 4:13PM ST : Commented                                                            *
 *=============================================================================================*/

void SoundDriver::Stop_Sample_Playing(void const * sample)
{
	int index;

	if (sample) {
		for (index = 0; index < MAX_SFX; index++) {
			if (SampleTracker[index].Original == sample && Sample_Status(index)) {
				Stop_Sample(index);
			}
		}
	}
}


/***********************************************************************************************
 * Get_Free_Sample_Handle -- finds a free slot in which to play a new sample                   *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    priority of sample we want to play                                                *
 *                                                                                             *
 * OUTPUT:   Handle or -1 if none free                                                         *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/2/95 4:14PM ST : Added function header                                                *
 *=============================================================================================*/

int SoundDriver::Get_Free_Sample_Handle(int priority)
{
	int	id;

	/*
	**	Find a free SFX holding buffer slot.
	*/
	for (id = MAX_SFX - 1; id >= 0; id--) {
		SampleTrackerType *st = &SampleTracker[id];

		LOCK_SECONDARY_MUTEX(id);

		if (!st->Active && !st->Loading) {
			UNLOCK_SECONDARY_MUTEX(id);
			break;
		}
		UNLOCK_SECONDARY_MUTEX(id);
	}

	if (id < 0) {
		for (id = 0; id < MAX_SFX; id++) {
			SampleTrackerType *st = &SampleTracker[id];

			LOCK_SECONDARY_MUTEX(id);

			if (st->IsScore != TRUE && st->Priority < priority) {
				UNLOCK_SECONDARY_MUTEX(id);
				break;
			}
			UNLOCK_SECONDARY_MUTEX(id);
		}

		if (id == MAX_SFX) {
			return(-1);		// Cannot play!
		}
		Stop_Sample(id);		// This sample gets clobbered.
	}

	if (id == -1) {
		return(-1);
	}

	LOCK_SECONDARY_MUTEX(id);

	SampleTrackerType *st = &SampleTracker[id];

	if (st->FileHandle != NULL) {
		st->FileHandle->Close();
		delete st->FileHandle;
		st->FileHandle = NULL;
	}

	st->IsScore = FALSE;

	UNLOCK_SECONDARY_MUTEX(id);

	return(id);
}


/// <summary>
/// Plays a sample at the volume specified.
/// This routine finds a free sample tracker to play the sample on and then starts it. It is
/// the ordinary way the game plays a sound effect; use Play_Sample_Handle when the tracker
/// to play on has already been chosen.
/// </summary>
/// <param name="priority">The priority of the sample. A higher priority sample may steal the
/// channel of a lower priority one that is already playing.</param>
/// <param name="volume">The volume to play at, in the range 0 to 255.</param>
/// <returns>Returns with the handle the sample is playing on, or -1 if it could not be
/// played.</returns>
int SoundDriver::Play_Sample(void const *sample, int priority, int volume)
{
	return(Play_Sample_Handle(sample, priority, volume, Get_Free_Sample_Handle(priority)));
}


/***********************************************************************************************
 * Play_Sample_Vol -- Plays a digitized sample.                                                *
 *                                                                                             *
 *    Use this routine to play a previously loaded digitized sample.                           *
 *                                                                                             *
 * INPUT:   sample   -- Sample pointer as returned from Load_Sample.                           *
 *                                                                                             *
 *          volume   -- The volume to play (0..255 with 255=loudest).                          *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   04/17/1992 JLB : Created.                                                                 *
 *   05/24/1992 JLB : Volume support -- Soundblaster Pro                                       *
 *   04/22/1994 JLB : Multiple sample playback rates.                                          *
 *   11/02/1995 ST  : Windows Direct Sound support                                             *
 *=============================================================================================*/
int SoundDriver::Play_Sample_Handle(void const *sample, int priority, int volume, int id)
{
	AUDHeaderType                   RawHeader;
	SampleTrackerType               *st=NULL;       // Working pointer to sample tracker structure.

	void *				play_buffer_ptr;		// the sample's ring, written straight into

	if (id == -1) {
		//LeaveCriticalSection (&GlobalAudioCriticalSection);
		return(-1);
	}

	if (!DeviceOpen || AudioDone) return(-1);

	if (!sample) {
		//LeaveCriticalSection (&GlobalAudioCriticalSection);
		return(-1);
	}

	LOCK_SECONDARY_MUTEX(id);

	bool reuse_buffer = false;
	st = &SampleTracker[id];

	if (st->Original == sample && st->OneShot && st->PlayBuffer != NULL) {
		reuse_buffer = true;
	}

	/*
	**	Fetch the control bytes from the start of the sample data.
	*/
	memcpy((void *)&RawHeader, (void *)sample, sizeof(RawHeader));

	/*
	**	Fudge the sample rate to 22k
	*/
	if (RawHeader.Rate <24000 && RawHeader.Rate >20000) RawHeader.Rate = 22050;

	/*
	**	Prepare the sample tracker structure for processing of this
	**	sample.  Fill the structure with data that can be determined
	**	before the sample is started.
	*/
	st->Compression 			= (SCompressType) ((unsigned char)RawHeader.Compression);
	st->Original            = sample;
//	st->OriginalSize        = RawHeader.Size + sizeof(RawHeader);
	st->Priority            = (short)priority;
//	st->DontTouch           = TRUE;
	st->Odd						= 0;
	st->Reducer             = 0;
//	st->Restart             = FALSE;
	st->QueueBuffer 			= NULL;
	st->QueueSize           = NULL;
	st->Remainder           = RawHeader.Size;
	st->Source              = Audio_Add_Long_To_Pointer((void *)sample, sizeof(RawHeader));
	st->Service             = FALSE;

	/*
	**	If the code in question using HMI based compression then we need
	**	to set up for uncompressing it.
	*/
	if (st->Compression == SCOMP_SOS) {
		st->sosinfo.ChannelCount = (RawHeader.Flags & AUD_FLAG_STEREO) ? 2  : 1;
		st->sosinfo.BitSize      = (RawHeader.Flags & AUD_FLAG_16BIT)  ? 16 : 8;
		st->sosinfo.CompSize     = RawHeader.Size;
		st->sosinfo.UnCompSize   = RawHeader.Size * ( st->sosinfo.BitSize / 4 );
		if (st->sosinfo.BitSize == 16 && st->sosinfo.ChannelCount == 1) {
			sosCODECInitStream(&st->sosinfo);
		} else {
			General_sosCODECInitStream(&st->sosinfo);
		}
	}

	/*
	**	If the sample rate , bits per sample or stereo capabilities of the buffer do not
	**	match the sample then reopen the ring with the required capabilities
	*/
	if (!reuse_buffer) {

	if (st->PlayBuffer == NULL || ( RawHeader.Rate != st->PlaybackRate ) ||
		((RawHeader.Flags &  AUD_FLAG_16BIT) != (st->BitSize & AUD_FLAG_16BIT)) ||
		((RawHeader.Flags & AUD_FLAG_STEREO) != (st->Stereo & AUD_FLAG_STEREO))) {
		DebugString("SoundDriver [%d]: Changing sample format\n", id);

		st->Active=0;
		st->Service=0;
		st->MoreSource=0;

		/*
		**	Stop the sound buffer playing
		*/
	if (st->PlayBuffer) {
		Audio_Backend_Close_Stream(st->PlayBuffer);
		st->PlayBuffer=nullptr;
	}

		/*
		**	Open a ring in the sample's own format. The device carries each stream at the
		**	format it was opened with, so there is no output format to match here.
		*/
		st->PlayBuffer = Audio_Backend_Open_Stream(SECONDARY_BUFFER_SIZE, RawHeader.Rate,
								(RawHeader.Flags & AUD_FLAG_16BIT) ? 16 : 8,
								(RawHeader.Flags & AUD_FLAG_STEREO) ? 2 : 1);

		/*
		**	If that failed then flag the buffer as having an impossible format so it wont
		**	match any sample, which makes the next use of it try again.
		*/
		if (st->PlayBuffer == nullptr){
			DebugString("SoundDriver: Bad sample format!\n");
			st->PlaybackRate = 0;
			st->Stereo = 0;
			st->BitSize = 0;
			if (st->FileHandle != NULL) {
				st->FileHandle->Close();
				delete st->FileHandle;
				st->FileHandle = NULL;
			}
			if (st->FileBuffer != NULL) {
				if (st->FileBuffer != FileStreamBuffer) {
					delete st->FileBuffer;
				} else {
					IsStreamBufferClaimed = false;
				}
				st->FileBuffer = NULL;
			}

			st->Loading = false;
			st->Priority = 0;
			st->FilePending = 0;
			st->FilePendingSize = 0;
			st->QueueBuffer = NULL;
			st->Callback = NULL;
			UNLOCK_SECONDARY_MUTEX(id);
			return(-1);
		}

		/*
		**	Remember the format of the new buffer
		*/
		st->PlaybackRate 	= RawHeader.Rate;
		st->Stereo			= RawHeader.Flags & AUD_FLAG_STEREO;
		st->BitSize 		= RawHeader.Flags & AUD_FLAG_16BIT;
	}

	/*
	**	Fill in 3/4 of the play buffer.
	*/

	}

	//
	// Stop the sound buffer playing before we lock it
	//
	if (Audio_Backend_Is_Playing(st->PlayBuffer)){
		st->Active=0;
		st->Service=0;
		st->MoreSource=0;
		Audio_Backend_Stop(st->PlayBuffer);
	}

	Prefetch_Audio_Buffer((char *)st->Source, st->Remainder);

	if (reuse_buffer) {
		st->Remainder = false;
		st->MoreSource = false;
		st->OneShot = true;
		st->Service = true;
	} else {
	play_buffer_ptr = Audio_Backend_Ring(st->PlayBuffer);

	if (play_buffer_ptr == nullptr) {
		UNLOCK_SECONDARY_MUTEX(id);
		return(-1);
	}

	int size = SECONDARY_BUFFER_SIZE * 1 / 4;
	if (RawHeader.UncompSize < SECONDARY_BUFFER_SIZE * 3 / 4) {
		size = RawHeader.UncompSize;
	}

	//
	// Decompress the sample into the ring
	//
	st->DestPtr=(void*)Sample_Copy ( 	st,
								&st->Source,
								&st->Remainder,
								&st->QueueBuffer,
								&st->QueueSize,
								play_buffer_ptr,
								size,
								st->Compression);

	if ( (int)st->Remainder > 0 ){

		// Must be more data to copy so we dont need to zero the buffer
		st->MoreSource=TRUE;
		st->Service=TRUE;
		st->OneShot=FALSE;
	} else {

		// Whole sample is in the buffer so flag that we dont need to
		// copy more. Clear out the end of the buffer so that it
		// goes quiet if we play past the end
		st->MoreSource=FALSE;
		st->OneShot=TRUE;
		st->Service=TRUE;				//We still need to service it so that we can stop it when
											// it plays past the end of the sample data
		int left = SECONDARY_BUFFER_SIZE - size;
		if (SECONDARY_BUFFER_SIZE - size > SECONDARY_BUFFER_SIZE/4) {
			left = SECONDARY_BUFFER_SIZE/4;
		}
		memset ( (char*)( (unsigned)play_buffer_ptr + (unsigned)st->DestPtr ), 0 , left);
	}

	}

	/*
	**
	**	Set the volume of the sample.
	**
	*/
	Audio_Backend_Set_Gain(st->PlayBuffer, Gain_From_HMI_Volume( ( SoundVolume*volume)/255) );
	st->StartVolume = volume;
	st->Volume = volume << 7;

	/*
	**	Make sure the primary sound buffer is playing
	*/
	if (!Start_Primary_Sound_Buffer(FALSE)){
		//LeaveCriticalSection (&GlobalAudioCriticalSection);
		UNLOCK_SECONDARY_MUTEX(id);
		return(-1);
	}


	/*
	**	Set the buffers play pointer to the beginning of the buffer
	*/
	Audio_Backend_Seek(st->PlayBuffer, 0);


	/*
	**	Start the sample playing now.
	*/
	Audio_Backend_Start(st->PlayBuffer);

	st->Active=TRUE;
	st->Handle=(int)id;

	UNLOCK_SECONDARY_MUTEX(id);
	return(st->Handle);
}




/// <summary>
/// Takes ownership of the global audio mutex.
/// This is the mutex that guards the driver's shared state against the sound threads. The
/// wait gives up after a while rather than blocking the game forever.
/// </summary>
/// <returns>bool; Was the global audio mutex acquired?</returns>
/// <remarks>Every successful call must be paired with a call to Unlock_Global_Mutex.</remarks>
bool SoundDriver::Lock_Global_Mutex(void)
{
	return(WaitForSingleObject(GlobalAudioMutex, MUTEX_TIMEOUT) != WAIT_TIMEOUT ? true : false);
}


/// <summary>
/// Releases the global audio mutex.
/// </summary>
void SoundDriver::Unlock_Global_Mutex(void)
{
	ReleaseMutex(GlobalAudioMutex);
}


/// <summary>
/// Takes ownership of every audio mutex.
/// Use this routine to shut the sound timer and the maintenance callback out while the audio
/// system is being reconfigured or torn down. If any one of the mutexes cannot be had, the
/// ones already taken are given back, so the caller never ends up holding a partial set.
/// </summary>
/// <returns>bool; Were all of the audio mutexes acquired?</returns>
/// <remarks>Every successful call must be paired with a call to Unlock_Mutex.</remarks>
bool SoundDriver::Lock_Mutex(void)
{
	DebugString("Taking ownership of all audio mutexes\n");

	if (WaitForSingleObject(TimerMutex, MUTEX_TIMEOUT) == WAIT_TIMEOUT) {
		return(false);
	}

	int index = 0;
	while (true) {
		if (WaitForSingleObject(SecondaryBufferMutexes[index], MUTEX_TIMEOUT) == WAIT_TIMEOUT) {
			while (index >= 0) {
				UNLOCK_SECONDARY_MUTEX(index);
				index--;
			}

			ReleaseMutex(TimerMutex);

			return(false);
		}
		index++;

		if (index >= MAX_SFX) {
			break;
		}
	}

	if (!Lock_Global_Mutex()) {
		for (index = 0; index < MAX_SFX; index++) {
			UNLOCK_SECONDARY_MUTEX(index);
		}

		ReleaseMutex(TimerMutex);
		return(false);
	}

	return(true);
}


/// <summary>
/// Releases ownership of every audio mutex.
/// Use this routine to let the sound timer and the maintenance callback run again after a
/// region of code protected by Lock_Mutex.
/// </summary>
void SoundDriver::Unlock_Mutex(void)
{
	Unlock_Global_Mutex();

	for (int index = 0; index < MAX_SFX; index++) {
		UNLOCK_SECONDARY_MUTEX(index);
	}

	ReleaseMutex(TimerMutex);

	DebugString("Released ownership of all audio mutexes\n");
}


/***********************************************************************************************
 * Sound_Timer_Callback -- windows timer callback for sound maintenance                        *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/2/95 4:01PM ST : Created                                                              *
 *=============================================================================================*/

void CALLBACK SoundDriver::Sound_Timer_Callback ( UINT, UINT, DWORD, DWORD, DWORD )
{
	HANDLE mutex = Audio.TimerMutex;
	if (WaitForSingleObject(mutex, 0) == 0) {
		Audio.maintenance_callback();
		mutex = Audio.TimerMutex;
		ReleaseMutex(mutex);
	}
}


/***********************************************************************************************
 * maintenance_callback -- routine to service the direct play secondary buffers                *
 *                         and other stuff..?                                                  *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *     ....Unknown                                                                             *
 *    10/17/95 10:15PM ST : tidied up a tad for direct sound                                   *
 *=============================================================================================*/
void SoundDriver::maintenance_callback(void)
{

	int					index;              //index used in for loop
	SampleTrackerType	*st;                //ptr to SampleTracker structure
	DWORD					play_cursor;    //Position that the device is reading from
	DWORD					write_cursor;   //Position in buffer that we can write to
	int			 		bytes_copied;       //Number of bytes copied into the buffer
	BOOL					write_more;     //Flag to set if we need to write more into the buffer
	void *				play_buffer_ptr;    //Where in the ring the refill is written

	//EnterCriticalSection(&GlobalAudioCriticalSection);

	for (index = 0; index < MAX_SFX; index++) {

		if (WaitForSingleObject(SecondaryBufferMutexes[index], 0)) continue;

		st = &SampleTracker[index];

		if (st->Active) {

			/*
			**	General service routine to handle moving small blocks from the
			**	source into the rings.  If the source is
			**	compressed, then this will also uncompress it as the copy
			**	is performed.
			*/
			if (st->Service /* && !st->DontTouch*/ ) {

				//EnterCriticalSection (&st->AudioCriticalSection);

				//st->DontTouch = TRUE;

				/*
				**	Where in the ring the device is playing, and how far ahead of that it
				**	has already taken data.
				*/
				play_cursor = (DWORD)Audio_Backend_Play_Cursor(st->PlayBuffer);
				write_cursor = (DWORD)Audio_Backend_Write_Cursor(st->PlayBuffer);


				if (st->MoreSource){

					/*
					**	If the play cursor is less than a quarter
					**	of a buffer away from the end of the data then copy some
					**	more.
				 	*/
					write_more = FALSE;

					if ( play_cursor < (unsigned)st->DestPtr ){
						if ( (unsigned)st->DestPtr - (unsigned)play_cursor <= SECONDARY_BUFFER_SIZE/4 ){
							write_more=TRUE;
						}
					} else {
						/* The only time that play_cursor can be greater than DestPtr is
						**	if we wrote right to the end of the buffer last time and DestPtr
						**	looped back to the beginning of the buffer.
						**	That being the case, all we have to do is see if play_cursor is
						**	within the last 25% of the buffer
						*/
						if ( ( (unsigned)play_cursor > SECONDARY_BUFFER_SIZE*3/4) &&st->DestPtr==0 ){
							write_more=TRUE;
						}
					}

					if (write_more){

						play_buffer_ptr = Audio_Backend_Ring(st->PlayBuffer) + st->DestPtr;

						if (play_buffer_ptr != nullptr){

							bytes_copied = Sample_Copy(	st,
														&st->Source,
														&st->Remainder,
														&st->QueueBuffer,
														&st->QueueSize,
														play_buffer_ptr,
														SECONDARY_BUFFER_SIZE/4,
														st->Compression);

							if ( bytes_copied != (SECONDARY_BUFFER_SIZE/4) ){
								/*
								**	We must have reached the end of the sample
								*/
								st->MoreSource=FALSE;
								memset (((char*)play_buffer_ptr)+bytes_copied ,
										0 ,
										(SECONDARY_BUFFER_SIZE/4)-bytes_copied);

								/*
								**	Clear out an extra area in the buffer ahead of the play cursor
								**	to give us a quiet period of grace in which to stop the buffer playing
								*/
								if ( (unsigned)st->DestPtr == SECONDARY_BUFFER_SIZE*3/4 ){
									/*
									**	The grace period wraps, so it is cleared at the start
									**	of the ring rather than ahead of the refill.
									*/
									memset (Audio_Backend_Ring(st->PlayBuffer) , 0 , SECONDARY_BUFFER_SIZE/4);
								} else {
									memset ((char*)play_buffer_ptr+SECONDARY_BUFFER_SIZE/4 , 0 , SECONDARY_BUFFER_SIZE/4);
								}
							}

							/*
							**	Update our pointer into the ring
							**
							*/
							st->DestPtr = Audio_Add_Long_To_Pointer (st->DestPtr,bytes_copied);

							if ( (unsigned)st->DestPtr >= (unsigned)SECONDARY_BUFFER_SIZE ){
								st->DestPtr = Audio_Add_Long_To_Pointer (st->DestPtr,(int)-SECONDARY_BUFFER_SIZE);
							}

						}

					}				//write_more

				} else {			//!more_source

					/*
					**	no more source to write - check if the buffer play
					**	has overrun the end of the sample and stop it if it has
					*/
					if ( ( (play_cursor >= (unsigned)st->DestPtr) && ( ((unsigned)play_cursor - (unsigned)st->DestPtr) <SECONDARY_BUFFER_SIZE/4) ) ||
						(!st->OneShot &&( (play_cursor < (unsigned)st->DestPtr) && ( ((unsigned)st->DestPtr - (unsigned)play_cursor) >(SECONDARY_BUFFER_SIZE*3/4) ) ))	 ){
							//st->PlayBuffer->Stop();
							st->Service = FALSE;
							Stop_Sample( index );
					}
				}					//more_source

				//LeaveCriticalSection (&st->AudioCriticalSection);
			}
			/*
			**	For file streamed samples, fill the queue pointer if needed.
			**	This allows for delays in calling the Sound_Callback function.
			*/
			if (st->Active && !st->QueueBuffer && st->FilePending > 0) {
				st->QueueBuffer = Audio_Add_Long_To_Pointer(st->FileBuffer, (int)(st->Odd%STREAM_BUFFER_COUNT)*(int)StreamBufferSize);
				st->FilePending--;
				st->Odd++;
				if (!st->FilePending) {
					st->QueueSize = st->FilePendingSize;
				} else {
					st->QueueSize = StreamBufferSize;
				}
			}


			/*
			**	If there are any samples that require fading, then do so at this
			**	time.
			*/
			if (st->Active && st->Reducer > 0 && st->Volume > 0) {
				if (st->Reducer >= st->Volume) {
					st->Volume = 0;
				} else {
					st->Volume -= st->Reducer;
				}
				Audio_Backend_Set_Gain(st->PlayBuffer, Gain_From_HMI_Volume( ( SoundVolume*(st->Volume >>7))/255) );
			}
		}
		UNLOCK_SECONDARY_MUTEX(index);
	}

	//LeaveCriticalSection(&GlobalAudioCriticalSection);
}


/***************************************************************************
 * STREAM_SAMPLE_VOLUME -- generic streaming sample playback init          *
 *                                                                         *
 * INPUT:                                                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   07/17/1995 PWG : Created.                                             *
 *=========================================================================*/
int SoundDriver::Stream_Sample_Vol(void *buffer, int size, bool (*callback)(short id, short int *odd, void **buffer, int *size), int volume, int handle)
{
	int								playid=-1;      // Sample play ID.
/*
 * SampleTrackerType               *st;                    // Working pointer to sample control structure.
 */
	int							oldsize;                // Copy of original sound size.
	AUDHeaderType                   *header;

	if (buffer && size > 0 && DeviceOpen && !AudioDone) {

		/*
	 	**	Start the first section of the sound playing.
	 	*/
	 	header = (AUDHeaderType *)buffer;
	 	oldsize = header->Size;
	 	header->Size = size-sizeof(*header);
	 	playid = Play_Sample_Handle(buffer, 0xFF, volume, handle);
	 	header->Size = oldsize;

	 	/*
	 	**	If the sample actually started playing, then flag this
	 	**	sample as a streaming type and signal for a callback
	 	**	to occur.
	 	*/
	 	if (playid != -1) {
			LOCK_STREAMING_SECONDARY_MUTEX(playid);

	 		SampleTracker[playid].Callback = callback;
	 		SampleTracker[playid].Odd = 0;

			UNLOCK_STREAMING_SECONDARY_MUTEX(playid);
		}
		return(playid);
	}
	return(playid);
}


/***************************************************************************
 * FILE_STREAM_PRELOAD -- Handles initial proload of streaming samples     *
 *                                                                         *
 * This function is called before a sample which streams from disk is      *
 * started.  It can be called to either fill the buffer in small chunks    *
 * from the call back routine or to fill the entire buffer at once.  This  *
 * is wholely dependant on whether the Loading bit is set within the       *
 * sample tracker.                                                         *
 *                                                                         *
 * INPUT:LockedData.SampleTracker * to the header which tracks this samples*
 *                                                              processing.*
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   06/05/1995 PWG : Created.                                             *
 *=========================================================================*/

void SoundDriver::File_Stream_Preload(int handle)
{
	LOCK_STREAMING_SECONDARY_MUTEX(handle);

	SampleTrackerType	*st		= &SampleTracker[handle];
//	int					fh			= st->FileHandle;
	int					maxnum	= (STREAM_BUFFER_COUNT >> 1) + STREAM_CUSHION_BLOCKS;
	void					*buffer	= st->FileBuffer;
	int					num;

	/*
	**	Figure just how much we need to load.  If we are doing the load in progress
	**	then we will only load two blocks.
	*/
	if (st->Loading) {
		num = st->FilePending + 2;
	   num = std::min(num, maxnum);
	} else {
		num = maxnum;
	}

	//EnterCriticalSection(&GlobalAudioCriticalSection);

	/*
	**	Loop through the blocks and load up the number we need.
	*/
	int index = st->FilePending;
	if (st->FileHandle != NULL) {
		for (index = st->FilePending; index < num; index++) {
			int s = st->FileHandle->Read(Audio_Add_Long_To_Pointer(buffer, (int)index * (int)StreamBufferSize), StreamBufferSize);
			if (s > 0) {
		 		st->FilePendingSize = s;
	  			st->FilePending++;
			}
			if (s < StreamBufferSize) break;
		}
	}

//	Sound_Timer_Callback(0,0,0,0,0);	//Shouldnt block as we are calling it from the same thread



	/*
	**	If the last block was incomplete (ie. it didn't completely fill the buffer) or
	**	we have now filled up as much of the Streaming Buffer as we need to, then now is
	**	the time to kick off the sample.
	*/
	if (st->FilePendingSize < StreamBufferSize || index == maxnum) {

		/*
		**	Actually start the sample playing, and don't worry about the file callback
		**	it won't be called for a while.
		*/
		int size						= (st->FilePending == 1) ? st->FilePendingSize : StreamBufferSize;
		Stream_Sample_Vol(buffer, size, File_Callback, (int)st->Volume >> 7, handle);

		/*
		**	The Sample is finished loading (if it was loading in small pieces) so record that
		**	so that it will now use the active logic in the file call back.
		*/
		st->Loading					= FALSE;

		/*
		**	Decrement the file pending because the first block is already playing thanks
		**	to the play sample call above.
		*/
		st->FilePending--;

		/*
		**	If File pending is now a zero, then we only preloaded one block and there
		**	is nothing more to play.  So clear the sample tracing structure of the
		**	information it no longer needs.
		*/
		if (!st->FilePending) {
			st->Odd					= 0;
			st->QueueBuffer		= 0;
			st->QueueSize			= 0;
			st->FilePendingSize	= 0;
			st->Callback			= NULL;
			/*
			 */
			st->FileHandle->Close();
			delete st->FileHandle;
			st->FileHandle = NULL;
		} else {
			/*
			**	The QueueBuffer counts as an already played block so remove it from the total.
			**	Note: We didn't remove it before because there might not have been one.
			*/
			st->FilePending--;

			/*
			**	When we start loading we need to start past the first two blocks.  Why this
			**	is called Odd, I haven't got the slightest.
			*/
			st->Odd = 2;

			/*
			**	If the file pending size is less than the stream buffer, then the last block
			**	we loaded was the last block period.  So close the file and reset the handle.
			*/
			if (st->FilePendingSize != StreamBufferSize && st->FileHandle) {
				st->FileHandle->Close();
				delete st->FileHandle;
				st->FileHandle = NULL;
			}

			/*
			**	The Queue buffer needs to point at the next block to be processed.  The size
			**	of the queue is dependant on how many more blocks there are.
			*/
			st->QueueBuffer = Audio_Add_Long_To_Pointer(buffer, StreamBufferSize);
			if (!st->FilePending) {
				st->QueueSize = st->FilePendingSize;
			} else {
				st->QueueSize = StreamBufferSize;
			}
		}
	}
	//LeaveCriticalSection(&GlobalAudioCriticalSection);
	UNLOCK_STREAMING_SECONDARY_MUTEX(handle);

}


/***********************************************************************************************
 * File_Stream_Sample_Vol -- Streams a sample directly from a file.                            *
 *                                                                                             *
 *    This will take the file specified and play it directly from disk.                        *
 *    It performs this by allocating a temporary buffer in XMS/EMS and                         *
 *    then keeping this buffer filled by the Sound_Callback() routine.                         *
 *                                                                                             *
 * INPUT:   filename -- The name of the file to play.                                          *
 *                                                                                             *
 * OUTPUT:  Returns the handle to the sound -- just like Play_Sample().                        *
 *                                                                                             *
 * WARNINGS:   The temporary buffer is allocated when this routine is                          *
 *             called and then freed when the sound is finished.  Keep                         *
 *             this in mind.                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *=============================================================================================*/

int SoundDriver::File_Stream_Sample_Vol(char const *filename, int volume, bool real_time_start)
{
	SampleTrackerType       *st;
	CCFileClass *fh;
	int     handle = -1;

	if (DeviceOpen && !AudioDone && filename && CCFileClass(filename).Is_Available()) {


		/*
		**	Reserve a handle so that we can fill in the sample tracker
		**	with the needed information.  If we dont get valid handle then
		**	we might as well give up.
		*/
		handle = Get_Free_Sample_Handle(0xFF);
		if (handle == -1) {
			return(-1);
		}

		/*
		**	Lets see if we can sucessfully open up the file.  If we can't,
		**	then there is no point in going any farther.
		*/
		fh = new CCFileClass(filename);
		if (!fh->Is_Available() || !fh->Open()) {
			DebugString("SoundDriver[%d]: ***ERROR*** Unable to open file %s\n", handle, filename);
			delete fh;
			return(-1);
		}

		void *buffer;

		if (IsStreamBufferClaimed) {
			buffer = new char[StreamBufferSize * STREAM_BUFFER_COUNT];
		} else {
			buffer = FileStreamBuffer;
			IsStreamBufferClaimed = 1;
		}

		if (buffer == NULL) {
			delete fh;
			DebugString("SoundDriver[%d]: ***ERROR*** Unable to obtain streaming buffer\n", handle);
			return(-1);
		}

		LOCK_STREAMING_SECONDARY_MUTEX(handle);

		/*
		**	Now lets get a pointer to the proper sample handler and start
		**	our manipulations.
		*/
		st							= &SampleTracker[handle];
		st->IsScore				= TRUE;
		st->FilePending		= 0;
		st->FilePendingSize	= 0;
		st->Loading				= real_time_start;
		st->StartVolume			= volume;
		st->Volume				= volume << 7;
		st->FileHandle			= fh;
		st->FileBuffer			= buffer;
		UNLOCK_STREAMING_SECONDARY_MUTEX(handle);

		/*
		**	Now that we have setup our initial data properly, let load up
		**	the beginning of the sample we intend to stream.
		*/
		File_Stream_Preload(handle);
	}

	return(handle);
}


/***********************************************************************************************
 * Sound_Callback -- Audio driver callback function.                                           *
 *                                                                                             *
 *    Maintains the audio buffers.  This routine must be called at least                       *
 *    11 times per second or else audio glitches will occur.                                   *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   If this routine is not called often enough then audio                           *
 *             glitches will occur.                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/06/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
void SoundDriver::Sound_Callback(void)
{
	int					index;
	SampleTrackerType	*st;

	if (DeviceOpen && !AudioDone) {

		/*
		**	Call the timer callback now as we may block it in this function
		*/
		//Sound_Timer_Callback(0,0,0,0,0);

		/*
		**	The device pulls the rings on a thread of its own, so this pass is only what has
		**	to happen on the game's: releasing the rings of closed streams.
		*/
		Audio_Backend_Service();

		for (index = 0; index < MAX_SFX; index++) {
			st = &SampleTracker[index];

			LOCK_SECONDARY_MUTEX(index);

			if (st->Loading) {
				File_Stream_Preload(index);
			} else {
				/*
				**	General service routine to handle moving small blocks from the
				**	source into the low RAM staging buffers.
				*/

				if (st->Active) {

					/*
					**	Special check to see if the sample is a fading one AND
					**	it has faded to silence, then stop it here.
					*/
					if (st->Reducer > 0 && !st->Volume) {
						//EnterCriticalSection(&GlobalAudioCriticalSection);
						Stop_Sample(index);
						//LeaveCriticalSection(&GlobalAudioCriticalSection);
					} else {

						/*
						**	Fill the queuebuffer if it is currently empty
						**	and there is a callback function defined to fill it.
						**
						**	PWG/CDY & CO: We should be down by at least two blocks
						**	before we bother with this
						*/

						if ((st->Callback && !st->QueueBuffer) ||
							(st->FileHandle && st->FilePending < STREAM_BUFFER_COUNT-3)
							) {

							if (!st->Callback((short)index, (short int *)&st->Odd, &st->QueueBuffer, &st->QueueSize)) {
									st->Callback = NULL;
							}
			 			}
		  			}
				}
			}

			UNLOCK_SECONDARY_MUTEX(index);
			/*
			**	Advance to the next sample control structure.
			*/
			st++;
		}

	}
}


/***************************************************************************
 * FILE_CALLBACK -- called to fill queue buffer for streaming sample       *
 *                                                                         *
 * This callback is called whenever the queue buffer playback has begun    *
 * and another buffer is needed for queuing up.  Returns TRUE if there     *
 * is more data to read from the file.                                     *
 *                                                                         *
 * INPUT:   WORD id         - the sample id number                         *
 *            WORD *odd      - which sample buffer to put info in          *
 *            VOID **buffer   - the buffer pointer to load data into       *
 *            LONG *size      - the amount to load                         *
 *                                                                         *
 * OUTPUT:  BOOL true if more data to load, FALSE if done loading          *
 *                                                                         *
 * HISTORY:                                                                *
 *   07/17/1995 PWG : Created.                                             *
 *=========================================================================*/
bool SoundDriver::File_Callback(short id, short *odd, void **buffer, int *size)
{
	SampleTrackerType       *st;            // Pointer to sample playback control struct.
	void                    *ptr;           // Pointer to working portion of file buffer.

	if (id != -1) {

		_LOCK_SECONDARY_MUTEX(id);

		st = &Audio.SampleTracker[id];
	 	ptr = st->FileBuffer;
	 	if (st->Active == true && ptr) {

	 	  	/*
	 	  	**	Move the next pending block into the primary
	 	  	**	position.  Do this only if the queue pointer is
	 	  	**	null.
	 	  	*/

			if (!*buffer && st->FilePending > 0) {
	 	  		*buffer = Audio_Add_Long_To_Pointer(ptr, (int)(*odd % STREAM_BUFFER_COUNT)*(int)Audio.StreamBufferSize);
	 	  		st->FilePending--;
	 	  		*odd = (int)(*odd + 1);
	 	  		if (!st->FilePending) {
	 	  			*size = st->FilePendingSize;
	 	  		} else {
	 	  			*size = Audio.StreamBufferSize;
	 	  		}
	 	  	}

	 	  	/*
	 	  	**	If the file handle is still valid, then read in the next
	 	  	**	block and add it to the next pending slot available.
	 	  	*/
	 	  	if (st->FilePending <
	 	  		(true/*Audio.StreamLowImpact*/ ? (STREAM_BUFFER_COUNT>>1) : ((STREAM_BUFFER_COUNT-3))) && st->FileHandle != NULL) {


	 	  		int num_empty_buffers;

#ifdef SIMPLE_FILLING
				num_empty_buffers = (STREAM_BUFFER_COUNT-2) - st->FilePending;
#else

				//
				// num_empty_buffers will be from 1 to STREAM_BUFFER_COUNT
				//
				if (StreamLowImpact) {
					num_empty_buffers = std::min((STREAM_BUFFER_COUNT >> 1)+STREAM_CUSHION_BLOCKS, (STREAM_BUFFER_COUNT - 2) - st->FilePending);
				}
				else {
					num_empty_buffers = (STREAM_BUFFER_COUNT - 2) - st->FilePending;
				}
#endif

				while (num_empty_buffers && (st->FileHandle != NULL)) {
					int     tofill;
					int    psize;

					tofill = (*odd + st->FilePending) % STREAM_BUFFER_COUNT;

					ptr = Audio_Add_Long_To_Pointer(st->FileBuffer, (int)tofill * (int)Audio.StreamBufferSize);
					psize = st->FileHandle->Read(ptr, Audio.StreamBufferSize);

				 	/*
				 	**	If less than the requested amount of data was read, this
				 	**	indicates that the source file is exhausted.  Flag the source
				 	**	file as closed so that no further reading is attempted.
				 	*/
				 	if (psize != Audio.StreamBufferSize) {
				 		st->FileHandle->Close();
				 		delete st->FileHandle;
						st->FileHandle = NULL;
				 	}

					/*
				 	**	If any real data went into the pending buffer, then flag
				 	**	that this buffer is valid.
					*/
				 	if (psize) {
				 	  	st->FilePendingSize = psize;
				 	  	st->FilePending++;
				 	}
				 	num_empty_buffers--;
				}

				/*
				**	After filling all pending buffers, check to see if the queue buffer
				**	is empty.  If so, then assign the first available pending buffer to the
				**	queue.
				*/
				if (!st->QueueBuffer && st->FilePending > 0) {
					st->QueueBuffer = Audio_Add_Long_To_Pointer(st->FileBuffer, (int)(st->Odd%STREAM_BUFFER_COUNT)*(int)Audio.StreamBufferSize);
					st->FilePending--;
					st->Odd++;
					if (!st->FilePending) {
						st->QueueSize = st->FilePendingSize;
					} else {
						st->QueueSize = Audio.StreamBufferSize;
					}
				}
			}

			/*
			**	If there are no more buffers that the callback routine
			**	can slot into the primary position, then signal that
			**	no furthur callbacks are needed.
			*/
			if (st->FilePending > 0) {
				//LeaveCriticalSection(&GlobalAudioCriticalSection);
				_UNLOCK_SECONDARY_MUTEX(id);
				return(TRUE);
			}
		}
		//LeaveCriticalSection(&GlobalAudioCriticalSection);
		_UNLOCK_SECONDARY_MUTEX(id);

	}
  return(FALSE);
}


/***********************************************************************************************
 * Print_Sound_Error -- show error messages from failed sound initialisation                   *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    error text                                                                        *
 *           handle to window                                                                  *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/7/96 10:17AM ST : Created                                                              *
 *=============================================================================================*/

void SoundDriver::Print_Sound_Error(char const *sound_error, HWND window)
{
	char buf[512];
	sprintf(buf, "%s\n\n%s", sound_error, Fetch_String(TXT_DSOUND_PROCEED));
	MessageBox(window, buf, Fetch_String(TXT_SHORT_TITLE), MB_ICONEXCLAMATION|MB_OK);
}






/// <summary>
/// Sets the master sound volume.
/// This routine sets the volume that every sample is mixed against and brings the samples
/// already playing into line with it. The options screen calls this routine when the player
/// drags the sound volume slider.
/// </summary>
/// <param name="volume">The master volume to use, in the range 0 to 255.</param>
void SoundDriver::Set_Volume_All(int volume)
{
	SoundVolume = std::min(volume, 255);

	for (int index = 0; index < MAX_SFX; index++) {
		if (Sample_Status(index)) {
			LOCK_SECONDARY_MUTEX(index);
			SampleTrackerType &st = SampleTracker[index];
			Audio_Backend_Set_Gain(st.PlayBuffer, Gain_From_HMI_Volume(SoundVolume * (st.Volume >> 7) / 255));
			UNLOCK_SECONDARY_MUTEX(index);
		}
	}
}


/// <summary>
/// Adjusts the master sound volume by a percentage.
/// This routine scales the master volume and brings every sample currently playing into line
/// with the new setting. It is used to duck the sound effects while a movie or a briefing is
/// speaking.
/// </summary>
/// <param name="percent">The percentage of the current master volume to scale to.</param>
/// <returns>Returns with the master volume that was in effect before the adjustment.</returns>
int SoundDriver::Adjust_Volume_All(int percent)
{
	int volume = SoundVolume;
	SoundVolume = std::min((percent * volume) / 100, 255);

	for (int index = 0; index < MAX_SFX; index++) {
		if (Sample_Status(index)) {
			LOCK_SECONDARY_MUTEX(index);
			SampleTrackerType &st = SampleTracker[index];
			Audio_Backend_Set_Gain(st.PlayBuffer, Gain_From_HMI_Volume(SoundVolume * (st.Volume >> 7) / 255));
			UNLOCK_SECONDARY_MUTEX(index);
		}
	}
	return(volume);
}


/// <summary>
/// Sets the volume of the sample playing on the handle specified.
/// The volume given is remembered as the sample's own volume, so it survives any later
/// change to the master sound volume.
/// </summary>
/// <param name="volume">The volume to play at, in the range 0 to 255.</param>
void SoundDriver::Set_Handle_Volume(int handle, int volume)
{
	if (Sample_Status(handle)) {

		LOCK_SECONDARY_MUTEX(handle);

		volume = std::min(volume, 255);
		SampleTrackerType &st = SampleTracker[handle];

		Audio_Backend_Set_Gain(st.PlayBuffer, Gain_From_HMI_Volume((SoundVolume * volume) / 255));
		st.Volume = volume << 7;

		UNLOCK_SECONDARY_MUTEX(handle);
	}
}


/// <summary>
/// Sets the volume of a sample that is playing.
/// This routine is a convenience for callers that hold the sample data rather than the
/// handle it was started on. A sample that is not playing is quietly ignored.
/// </summary>
/// <param name="volume">The volume to play at, in the range 0 to 255.</param>
void SoundDriver::Set_Sample_Volume(void const *sample, int volume)
{
	int handle = Get_Playing_Sample_Handle(sample);
	if (handle != -1) {
		Set_Handle_Volume(handle, volume);
	}
}


/// <summary>
/// Adjusts the volume of a playing sample.
/// This routine scales the volume the sample is currently playing at rather than setting it
/// outright, so successive adjustments compound. Restore_Sample_Handle_Volume undoes them.
/// </summary>
/// <param name="percent">The percentage of the current volume to scale to.</param>
void SoundDriver::Adjust_Sample_Handle_Volume(int handle, int percent)
{
	if (Sample_Status(handle)) {
		LOCK_SECONDARY_MUTEX(handle);
		SampleTrackerType &st = SampleTracker[handle];
		int vol = percent * (st.Volume >> 7) / 100;
		Audio_Backend_Set_Gain(st.PlayBuffer, Gain_From_HMI_Volume(vol));
		st.Volume = vol << 7;
		UNLOCK_SECONDARY_MUTEX(handle);
	}
}


/// <summary>
/// Restores a playing sample to its original volume.
/// Use this routine to undo the effect of an earlier volume adjustment. The volume the
/// sample was started with is put back into effect.
/// </summary>
void SoundDriver::Restore_Sample_Handle_Volume(int handle)
{
	if (Sample_Status(handle)) {
		LOCK_SECONDARY_MUTEX(handle);
		SampleTrackerType &st = SampleTracker[handle];
		int vol = st.StartVolume;
		st.Volume = vol << 7;
		Audio_Backend_Set_Gain(st.PlayBuffer, Gain_From_HMI_Volume((SoundVolume * vol) / 255));
		UNLOCK_SECONDARY_MUTEX(handle);
	}
}


/***********************************************************************************************
 * Fade_Sample -- Start a sample fading                                                        *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Sample handle                                                                     *
 *           fade rate                                                                         *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/2/95 4:21PM ST : Added function header                                                *
 *=============================================================================================*/

void SoundDriver::Fade_Sample(int handle, int ticks)
{
	_LOCK_SECONDARY_MUTEX(handle);

	if (Sample_Status(handle)==true) {
		if (!ticks || SampleTracker[handle].Loading) {
			Stop_Sample(handle);
		} else {
			SampleTrackerType * st;
			st = &SampleTracker[handle];
			st->Reducer = (int) ((st->Volume / ticks)+1);
		}
	}
	UNLOCK_SECONDARY_MUTEX(handle);
}


/// <summary>
/// Fetches the handle that a sample is playing on.
/// This routine is used by the volume routines to find the sample tracker that a piece of
/// raw sample data is being played through.
/// </summary>
/// <param name="sample">Pointer to the sample data to search for.</param>
/// <returns>Returns with the handle the sample is playing on, or -1 if it is not playing.</returns>
int SoundDriver::Get_Playing_Sample_Handle(void const *sample)
{
	if (sample == NULL) {
		return(-1);
	}

	for (int index = 0; index < MAX_SFX; index++) {
		if (SampleTracker[index].Original == sample && Sample_Status(index) ) {
			return(index);
		}
	}

	return(-1);
}


/***********************************************************************************************
 * Start_Primary_Sound_Buffer -- start the primary sound buffer playing                        *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/1/96 12:28PM ST : Created                                                              *
 *=============================================================================================*/
bool SoundDriver::Start_Primary_Sound_Buffer (bool forced)
{
	DWORD status;

	if (DeviceOpen && !AudioDone && GameInFocus){

		LOCK_GLOBAL_MUTEX();
		Audio_Backend_Resume();
		UNLOCK_GLOBAL_MUTEX();

		return(TRUE);
	}
	return(FALSE);
}


/***********************************************************************************************
 * Stop_Primary_Sound_Buffer -- stops the primary sound buffer from playing.                   *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: This stops all sound playback                                                     *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/1/96 12:28PM ST : Created                                                              *
 *=============================================================================================*/

void SoundDriver::Stop_Primary_Sound_Buffer (void)
{
	LOCK_GLOBAL_MUTEX();

	for ( int index = 0; index < MAX_SFX; index++) {
		Stop_Sample(index);
	}

	UNLOCK_GLOBAL_MUTEX();
}


/***************************************************************************
 * SIMPLE_COPY -- Copyies 1 or 2 source chuncks to a dest                  *
 *                                                                         *
 * INPUT:                                                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *                                                                         *
 * WARNINGS:                                                               *
 *                                                                         *
 * HISTORY:                                                                *
 *   06/23/1995 PWG : Created.                                             *
 *=========================================================================*/
int SoundDriver::Simple_Copy(void ** source, int * ssize, void ** alternate, int * altsize, void **dest, int size)
{


	int	out = 0;		// Number of bytes copied to the destination.

	/*
	**	It could happen that entering this routine, the source buffer
	**	has been exhausted, but the alternate buffer is still valid.
	**	Move the alternate into the primary position before proceeding.
	*/
	if (!(*ssize)) {
		*source = *alternate;
		*ssize = *altsize;
		*alternate = NULL;
		*altsize = 0;
	}

	if (*source && *ssize > 0) {
		int	s;				// Scratch length var.

		/*
		**	Copy as much as possible from the primary source, but no
		**	more than the primary source has to offer.
		*/
		s = size;
		if (*ssize < s) s = *ssize;
		memmove(*dest, *source, s);//Mem_Copy(*source, *dest, s);
		*source = Audio_Add_Long_To_Pointer(*source, s);
		*ssize -= s;
		*dest = Audio_Add_Long_To_Pointer(*dest, s);
		size -= s;
		out += s;

		/*
		**	If the primary source was insufficient to fill the request, then
		**	move the alternate into the primary position and try again.
		*/
		if (size > 0) {
			*source = *alternate;
			*ssize = *altsize;
			*alternate = 0;
			*altsize = 0;
			out += Simple_Copy(source, ssize, alternate, altsize, dest, size);
		}
	}

	return(out);
}


/***********************************************************************************************
 * Sample_Copy -- Copies sound data from source format to raw format.                          *
 *                                                                                             *
 *    This routine is used to copy the sound data (possibly compressed) to the destination     *
 *    buffer in raw format.                                                                    *
 *                                                                                             *
 * INPUT:   source   -- Pointer to the source data (possibly compressed).                      *
 *                                                                                             *
 *          dest     -- Pointer to the destination buffer.                                     *
 *                                                                                             *
 *          size     -- The size of the destination buffer.                                    *
 *                                                                                             *
 * OUTPUT:  Returns with the number of bytes placed into the output buffer.  This is usually   *
 *          the number of bytes requested except in the case when the source is exhausted.     *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/03/1994 JLB : Created.                                                                 *
 *   09/04/1994 JLB : Revamped entirely.                                                       *
 *=============================================================================================*/
int SoundDriver::Sample_Copy(SampleTrackerType *st, void ** source, int * ssize, void ** alternate, int * altsize, void * dest, int size, SCompressType scomp)
{

	int	s;
	int	datasize = 0;		// Output bytes.
	char	staging[LARGEST_SONARC_BLOCK + 50];

	if (scomp == SCOMP_NONE || scomp != SCOMP_SOS) {
		datasize = Simple_Copy(source, ssize, alternate, altsize, &dest, size);
	} else

	switch (scomp) {

//		default:

//		case SCOMP_NONE:
//			datasize = Simple_Copy(source, ssize, alternate, altsize, &dest, size);
//			break;

//		case SCOMP_WESTWOOD:
		case SCOMP_SOS:
			while (size > 0) {

				/*
				**	The block spans two buffers.  It must be copied down to
				**	a staging area before it can be decompressed.
				*/
				{
					int magic;
					unsigned short fsize;
					unsigned short dsize;
					void *fptr;
					void *dptr;
					void *mptr;

					fptr = &fsize;
					dptr = &dsize;
					mptr = &magic;

					s = Simple_Copy(source, ssize, alternate, altsize, &fptr, sizeof(fsize));
					if (s < sizeof(fsize)) {
						return(datasize);
					}
					s = Simple_Copy(source, ssize, alternate, altsize, &dptr, sizeof(dsize));
					if (s < sizeof(dsize) || size < dsize) {
						return(datasize);
					}

					s = Simple_Copy(source, ssize, alternate, altsize, &mptr, sizeof(magic));
					if (s < sizeof(magic) || magic != MagicNumber) {
						return(datasize);
					}

					/*
					**	If the frame and uncompressed data size are identical, then this
					**	indicates that the frame is not compressed.  Just copy it directly
					**	to the destination buffer in this case.
					*/
					if (fsize == dsize) {
						s = Simple_Copy(source, ssize, alternate, altsize, &dest, fsize);
						if (s < dsize) {
							return(datasize);
						}
					} else {

						/*
						**	The frame was compressed, so copy it to the staging buffer, and then
						**	uncompress it into the final destination buffer.
						*/
						// The decoder reads a quarter of dsize for 16 bit and half of it for 8 bit.
						if (fsize > sizeof(staging) || dsize > sizeof(staging) * (st->sosinfo.BitSize / 4)) {
							return(datasize);
						}
						fptr = staging;
						s = Simple_Copy(source, ssize, alternate, altsize, &fptr, fsize);
						if (s < fsize) {
							return(datasize);
						}
						st->sosinfo.Source = staging;
						st->sosinfo.Dest   = (char *)dest;
						if (st->sosinfo.BitSize==16 && st->sosinfo.ChannelCount==1){
							sosCODECDecompressData(&st->sosinfo, dsize);
						} else {
							General_sosCODECDecompressData(&st->sosinfo, dsize);
						}
						dest = Audio_Add_Long_To_Pointer(dest, dsize);
					}
					datasize += dsize;
					size -= dsize;
				}
			}

			break;
	}
	return(datasize);
}
