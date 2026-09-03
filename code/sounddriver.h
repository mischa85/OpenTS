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

#include "audio.h"
#include "audiobackend.h"
#include "soundint.h"


#define INVALID_SAMPLE_HANDLE -1

class SoundDriver
{
	public:
		SoundDriver(void);
		~SoundDriver(void);

		int File_Stream_Sample(char const *filename, bool real_time_start = false);
		int File_Stream_Sample_Vol(char const *filename, int volume, bool real_time_start = false);
		void Sound_Callback(void);
		void maintenance_callback(void);
		//void *Load_Sample(char const *filename);
		//long Load_Sample_Into_Buffer(char const *filename, void *buffer, long size);
		//long Sample_Read(int fh, void *buffer, long size);
		//void Free_Sample(void const *sample);
		bool Init( HWND window , int bits_per_sample, bool stereo , int rate /*, int reverse_channels*/);
		void End(void);
		void Stop_Sample(int handle);
		bool Sample_Status(int handle);
		bool Is_Sample_Playing(void const * sample);
		void Stop_Sample_Playing(void const * sample);
		int Play_Sample(void const *sample, int priority=0xFF, int volume=0xFF/*, signed short panloc = 0x0*/);
		int Play_Sample_Handle(void const *sample, int priority, int volume/*, signed short panloc*/, int id);
		//int Set_Sound_Vol(int volume);
		//int Set_Score_Vol(int volume);
		void Fade_Sample(int handle, int ticks);
		int Get_Free_Sample_Handle(int priority);
		//int Get_Digi_Handle(void);
		//long Sample_Length(void const *sample);
		bool Start_Primary_Sound_Buffer (bool forced);
		void Stop_Primary_Sound_Buffer (void);

		int Get_Playing_Sample_Handle(void const *sample);
		void Set_Volume_All(int volume);
		int Adjust_Volume_All(int percent);
		void Set_Handle_Volume(int handle, int volume);
		void Set_Sample_Volume(void const *sample, int volume);
		void Adjust_Sample_Handle_Volume(int handle, int percent);
		void Restore_Sample_Handle_Volume(int handle);

		enum {
			AUD_CHUNK_MAGIC_ID = 0xDEAF,

			PRIORITY_MIN = 0,
			PRIORITY_MAX = 255,

			MUTEX_COUNT = 1 + MAX_SFX,

			MUTEX_TIMEOUT = 10000, /// 10 seconds

			STREAM_BUFFER_COUNT = 16,

			TIMER_WORST_RESOLUTION = 25, /// 25-millisecond target resolution
			TIMER_TARGET_RESOLUTION = 10, /// 10-millisecond target resolution
		};

		bool Lock_Mutex(void);
		void Unlock_Mutex(void);

	private:
		bool Lock_Global_Mutex(void);
		void Unlock_Global_Mutex(void);

		static void CALLBACK Sound_Timer_Callback(UINT, UINT, DWORD, DWORD, DWORD);
		int Stream_Sample_Vol(void *buffer, int size, bool (*callback)(short, short int *, void **, int *), int volume, int handle);
		void File_Stream_Preload(int handle);
		static bool File_Callback(short int id, short int *odd, void **buffer, int *size);

		void Print_Sound_Error(char const *sound_error, HWND window);

		int Simple_Copy(void ** source, int * ssize, void ** alternate, int * altsize, void **dest, int size);
		int Sample_Copy(SampleTrackerType *st, void **source, int *ssize, void **alternate, int *altsize, void *dest, int size, SCompressType scomp);

	public:
		/*
		**	Function to call if we detect focus loss
		*/
		void (*Audio_Focus_Loss_Function)(void);

		/*
		 * If this flag is true, a stream refill tops up only half of its buffers at a time,
		 * trading playback headroom for a lighter load on the drive.
		 */
		bool StreamLowImpact;

	private:
		/*
		 * This is the magic number that heads every compressed audio chunk. A chunk whose
		 * header does not carry it is taken as corrupt and the sample stops decoding there.
		 */
		int MagicNumber;

		/*
		**	Buffer for streaming audio from CD
		*/
		void *FileStreamBuffer;

		/*
		 * This is the size, expressed in bytes, of one block of the file streaming buffer.
		 * The buffer holds STREAM_BUFFER_COUNT of them and is filled one block at a time.
		 */
		int StreamBufferSize;

		/*
		 * This is the master sound volume (0 - 255) that every sample's volume is scaled by.
		 */
		unsigned int SoundVolume;

		/*
		 * These are the sample playback slots, one for each simultaneously playable sound.
		 * A sample handle is nothing more than an index into this array.
		 */
		SampleTrackerType SampleTracker[MAX_SFX];

		/*
		 * This is the mutex that shuts the sound timer callback out while the audio system
		 * is being reconfigured or torn down.
		 */
		HANDLE TimerMutex;
		union {
			struct {
				/*
				 * This is the mutex that guards the audio state not tied to any one sample.
				 */
				HANDLE GlobalAudioMutex;

				/*
				 * These are the per sample mutexes, one for each secondary sound buffer, so
				 * that one sample can be worked on without stalling any of the others.
				 */
				HANDLE SecondaryBufferMutexes[MAX_SFX];
			};

			/*
			 * This is an alias over the global mutex and the secondary buffer mutexes, so
			 * that the whole set can be taken or waited on with a single call.
			 */
			HANDLE AllAudioMutexes[MUTEX_COUNT];
		};

		/*
		 * If the preallocated FileStreamBuffer has already been claimed, then this flag
		 * will be true and the next stream to start must allocate a buffer of its own.
		 */
		bool IsStreamBufferClaimed;

		// True once the output device has been opened, which is what makes a second Init
		// a no-op rather than a second device.
		bool DeviceOpen;

		/*
		**	Windows Handle for sound timer
		*/
		UINT SoundTimerHandle;

		/*
		 * This is the multimedia timer period, expressed in milliseconds, that the audio
		 * system asks Windows for. It is clamped to what the timer device can provide.
		 */
		unsigned int TimerResolution;

		/*
		 * If the audio system has been shut down, then this flag will be true. Every entry
		 * point checks it so that a sound request made afterward is quietly ignored.
		 */
		bool AudioDone;

		friend bool Audio_Available(void);
};

extern SoundDriver Audio;

/***********************************************************************************************
 * File_Stream_Sample -- Streams a sample directly from a file.                                *
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
 *   01/06/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
inline int SoundDriver::File_Stream_Sample(char const *filename, bool real_time_start)
{
	return(File_Stream_Sample_Vol(filename, 0xFF, real_time_start));
}

inline bool Audio_Available(void)
{
	return(Audio.DeviceOpen && !Audio.AudioDone);
}

float Gain_From_HMI_Volume(int volume);
