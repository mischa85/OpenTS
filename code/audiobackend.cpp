/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The sound driver's output for the page, and the only translation unit that includes
// OpenAL. It has two layers. The lower one carries a looping byte ring out to Web Audio
// through OpenAL's streaming queue; the upper one dresses that ring in the DirectSound
// secondary buffer contract the sound driver and the movie player are written against, so
// neither of them changes to reach the page.

#include "always.h"

#include "audiobackend.h"

#ifndef _WIN32

#if defined(OPENTS_NO_AUDIO_BACKEND)

// A build without OpenAL keeps the servicing hooks the engine calls callable, and the
// missing sound object leaves the engine running without sound.
void Audio_Backend_Service(void) {}


LPDIRECTSOUND Audio_Create_Sound_Object(void)
{
	return(nullptr);
}

#else

#include "dbgprint.h"
#include "dsaudio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>


/// <summary>
/// The milliseconds a silent stream's cursor is advanced against.
/// </summary>
static double Audio_Clock_Ms(void)
{
#if defined(__EMSCRIPTEN__)
	return(emscripten_get_now());
#else
	return(std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}


#if !defined(__EMSCRIPTEN__)

/*
** On Windows the sound maintenance ran on a multimedia timer thread, so the device never
** starved while the game thread was busy inside a long frame. A page has no thread to
** give, but a native host does: the feeder keeps the device supplied through those
** frames, and one lock makes the backend safe against it. The lock is recursive because
** the service pass reads the backend's own public cursors.
*/
static std::recursive_mutex BackendMutex;
static std::thread Feeder;
static std::atomic<bool> FeederRunning(false);
#define AUDIO_BACKEND_GUARD() std::lock_guard<std::recursive_mutex> _guard(BackendMutex)

#else
#define AUDIO_BACKEND_GUARD() ((void)0)
#endif


/*
** Reading and resuming the page's audio clock. OpenAL has no notion of an output that a
** page refuses to start, so both go through the emitted library's own context object.
** These sit at file scope because EM_JS emits a metadata symbol whose name the compiler
** has to find intact. A native device needs no reader gesture, so there its state reads
** as running and the resume and arming calls have nothing to do.
*/
#if !defined(__EMSCRIPTEN__)

static int Audio_Web_Context_State(void) { return(1); }
static void Audio_Web_Context_Resume(void) {}
static void Audio_Web_Arm_Gesture_Resume(void) {}

#else

EM_JS(int, Audio_Web_Context_State, (void), {
	if (typeof AL === 'undefined' || !AL.currentCtx || !AL.currentCtx.audioCtx) {
		return 0;
	}
	return (AL.currentCtx.audioCtx.state === 'running') ? 1 : 2;
});


EM_JS(void, Audio_Web_Context_Resume, (void), {
	if (typeof AL === 'undefined' || !AL.currentCtx || !AL.currentCtx.audioCtx) {
		return;
	}
	var audioctx = AL.currentCtx.audioCtx;
	if (audioctx.state !== 'running' && audioctx.resume) {
		audioctx.resume().catch(function () {});
	}
});


/*
** Emscripten arms its own one-shot resume when it creates the context, which is spent on
** the first gesture whether or not the resume took. These listeners stay armed instead,
** and cover the pointer and touch events its set leaves out.
*/
EM_JS(void, Audio_Web_Arm_Gesture_Resume, (void), {
	if (typeof document === 'undefined' || typeof AL === 'undefined' || !AL.currentCtx) {
		return;
	}
	var audioctx = AL.currentCtx.audioCtx;
	var resume = function () {
		if (audioctx.state !== 'running' && audioctx.resume) {
			audioctx.resume().catch(function () {});
		}
	};
	var names = ['pointerdown', 'mousedown', 'touchstart', 'touchend', 'keydown', 'click'];
	for (var index = 0; index < names.length; index++) {
		document.addEventListener(names[index], resume, true);
	}
});

#endif	// __EMSCRIPTEN__


namespace {

/*
** Queue geometry. The chunk is the granularity the device takes the ring in, and it is a
** fraction of the ring so that the number follows a ring of any size; the queue is deep
** enough to hold a lookahead's worth of those chunks.
*/
enum {
	QUEUE_DEPTH = 16,
	CHUNK_DIVISOR = 32,
	LOOKAHEAD_DIVISOR = 4,
};

/*
** How much audio the device is kept holding beyond the play cursor.
**
** A page is not a DMA engine. It schedules the audio it has been handed onto the output
** clock from a timer of its own, reaching a fixed distance ahead, and a queue shallower
** than that distance empties between two of the timer's passes: the source stops, the gap
** is heard, and -- because the movie player reads its clock off the play cursor -- the
** movie stops with it. So the lookahead is a duration rather than a fraction of a ring
** whose size says nothing about how long it lasts.
**
** The bound on it is what the caller has actually written. Neither caller keeps less than
** a quarter of its ring written ahead of the play cursor: the sound driver refills a
** quarter whenever the cursor comes within a quarter of what it last wrote, and the movie
** player refills half a ring whenever the cursor crosses into the other half. Taking more
** than that would carry out the previous lap of the ring in place of audio not yet
** decoded, so the duration is clamped to a quarter ring and the shorter of the two wins.
** A caller whose ring is too short to hold the duration inside that quarter is asking for
** a buffer the target cannot carry, and asks for a longer one instead.
*/
int const LOOKAHEAD_MS = 90;

/*
** Playback state has to keep advancing even when the page is not making a sound, or the
** driver would never retire a sample and would run out of tracker slots. A silent cursor
** moves no further than this in one pass, so one long frame steps over a fraction of a
** sample rather than the whole of it.
*/
double const SILENT_MAX_STEP = 250.0;

ALCdevice * Device = nullptr;
ALCcontext * Context = nullptr;

/*
** Whether the page is actually producing sound. A page that has not been interacted with
** holds its audio clock stopped, so this stays false until the reader touches the page.
*/
bool Running = false;

/*
** If the audio context's state cannot be read, output is assumed to run whenever a device
** is open. The port then relies on the device alone rather than on a silent guess.
*/
bool StateReadable = false;

double LastResumeAttempt = 0.0;

}


struct AudioBackendStream
{
	AudioBackendStream * Next;

	unsigned char * Ring;
	int RingSize;

	int Rate;
	int Bits;
	int Channels;
	int BlockAlign;
	int ChunkSize;
	int Lookahead;

	bool Playing;
	float Gain;

	/*
	** The play cursor is not read back from the device. It is the ring offset playback
	** started from plus the bytes the device has finished since, which is exactly what the
	** streaming queue reports and is stable whether or not a device exists at all.
	*/
	int PlayBase;
	int SubmitOffset;
	long long PlayedBytes;
	long long SubmittedBytes;

	// The wall-clock reading the silent cursor was last advanced from.
	double SilentClock;

	bool HasSource;
	ALuint Source;
	ALuint Buffers[QUEUE_DEPTH];

	ALuint FreeList[QUEUE_DEPTH];
	int FreeCount;

	// Sizes of the chunks the device holds, oldest first.
	int PendingSize[QUEUE_DEPTH];
	int PendingHead;
	int PendingCount;
};


namespace {

AudioBackendStream * StreamList = nullptr;


int Byte_Rate(AudioBackendStream const * stream)
{
	return(stream->Rate * stream->BlockAlign);
}


ALenum Buffer_Format(int bits, int channels)
{
	if (channels >= 2) {
		return((bits == 16) ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8);
	}
	return((bits == 16) ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8);
}


/*
** Drops everything the device is holding for this stream and restarts the queue at the
** given ring offset. Used by a seek and by a change of output mode, both of which
** invalidate what the device has already taken.
*/
void Flush_Queue(AudioBackendStream * stream, int offset)
{
	if (stream->HasSource) {
		alSourceStop(stream->Source);
		alSourcei(stream->Source, AL_BUFFER, 0);
		alGetError();
	}

	stream->FreeCount = 0;
	for (int index = 0; index < QUEUE_DEPTH; index++) {
		stream->FreeList[stream->FreeCount++] = stream->Buffers[index];
	}

	stream->PendingHead = 0;
	stream->PendingCount = 0;

	stream->PlayBase = offset;
	stream->SubmitOffset = offset;
	stream->PlayedBytes = 0;
	stream->SubmittedBytes = 0;
	stream->SilentClock = Audio_Clock_Ms();
}


/*
** Moves the play cursor forward by the audio that would have been heard since the last
** pass. This is the whole of playback when the page is not making a sound.
*/
void Service_Silent(AudioBackendStream * stream)
{
	double now = Audio_Clock_Ms();
	double elapsed = now - stream->SilentClock;

	if (elapsed <= 0.0) {
		stream->SilentClock = now;
		return;
	}

	if (elapsed > SILENT_MAX_STEP) {
		stream->SilentClock = now - SILENT_MAX_STEP;
		elapsed = SILENT_MAX_STEP;
	}

	long long bytes = (long long)(elapsed * (double)Byte_Rate(stream) / 1000.0);
	bytes -= bytes % stream->BlockAlign;
	if (bytes <= 0) {
		return;
	}

	stream->PlayedBytes += bytes;
	stream->SubmittedBytes = stream->PlayedBytes;
	stream->SubmitOffset = (int)((stream->PlayBase + stream->PlayedBytes) % stream->RingSize);
	stream->SilentClock += (double)bytes * 1000.0 / (double)Byte_Rate(stream);
}


void Service_Device(AudioBackendStream * stream)
{
	ALint processed = 0;
	alGetSourcei(stream->Source, AL_BUFFERS_PROCESSED, &processed);

	while (processed > 0 && stream->PendingCount > 0) {
		ALuint name = 0;
		alSourceUnqueueBuffers(stream->Source, 1, &name);
		if (alGetError() != AL_NO_ERROR) {
			break;
		}

		stream->PlayedBytes += stream->PendingSize[stream->PendingHead];
		stream->PendingHead = (stream->PendingHead + 1) % QUEUE_DEPTH;
		stream->PendingCount--;

		stream->FreeList[stream->FreeCount++] = name;
		processed--;
	}

	ALenum format = Buffer_Format(stream->Bits, stream->Channels);

	while (stream->SubmittedBytes - stream->PlayedBytes < stream->Lookahead && stream->FreeCount > 0) {
		int chunk = std::min(stream->ChunkSize, stream->RingSize - stream->SubmitOffset);

		ALuint name = stream->FreeList[--stream->FreeCount];
		alBufferData(name, format, stream->Ring + stream->SubmitOffset, chunk, stream->Rate);
		alSourceQueueBuffers(stream->Source, 1, &name);
		if (alGetError() != AL_NO_ERROR) {
			stream->FreeList[stream->FreeCount++] = name;
			break;
		}

		stream->PendingSize[(stream->PendingHead + stream->PendingCount) % QUEUE_DEPTH] = chunk;
		stream->PendingCount++;

		stream->SubmitOffset += chunk;
		if (stream->SubmitOffset >= stream->RingSize) {
			stream->SubmitOffset -= stream->RingSize;
		}
		stream->SubmittedBytes += chunk;
	}

	/*
	** A source that ran dry stops itself. Starting it again picks the queue back up where
	** it stalled, which is audibly a gap but keeps the cursor moving. Nothing finished can
	** be replayed by this: on a stopped source every queued buffer reads as processed, so
	** the pass above has already taken back all of them, and whatever this pass queued
	** again is data the cursor has not passed.
	*/
	ALint state = 0;
	alGetSourcei(stream->Source, AL_SOURCE_STATE, &state);

	if (state != AL_PLAYING && stream->PendingCount > 0) {
		DebugString("Audio backend: stream %p underran and restarted\n", (void *)stream);
		alSourcePlay(stream->Source);
	}
}

}


bool Audio_Backend_Init(void)
{
	AUDIO_BACKEND_GUARD();

	if (Device != nullptr) {
		return(true);
	}

	Device = alcOpenDevice(nullptr);
	if (Device == nullptr) {
		DebugString("Audio backend: no OpenAL device; the engine will run without sound\n");
		return(false);
	}

	Context = alcCreateContext(Device, nullptr);
	if (Context == nullptr) {
		DebugString("Audio backend: the page provides no Web Audio; the engine will run without sound\n");
		alcCloseDevice(Device);
		Device = nullptr;
		return(false);
	}

	alcMakeContextCurrent(Context);

	StateReadable = (Audio_Web_Context_State() != 0);
	if (!StateReadable) {
		DebugString("Audio backend: the audio context state cannot be read; assuming output runs\n");
	}

	Audio_Web_Arm_Gesture_Resume();
	Audio_Web_Context_Resume();

	Running = !StateReadable || (Audio_Web_Context_State() == 1);

	// The name says where the sound actually goes.
	char const * devicename = nullptr;
#ifdef ALC_ALL_DEVICES_SPECIFIER
	if (alcIsExtensionPresent(Device, "ALC_ENUMERATE_ALL_EXT")) {
		devicename = alcGetString(Device, ALC_ALL_DEVICES_SPECIFIER);
	}
#endif
	if (devicename == nullptr || devicename[0] == '\0') {
		devicename = alcGetString(Device, ALC_DEVICE_SPECIFIER);
	}

	DebugString("Audio backend: OpenAL opened \"%s\", output is %s\n",
		devicename != nullptr ? devicename : "<unnamed>",
		Running ? "running" : "waiting for the reader to interact with the page");

#if !defined(__EMSCRIPTEN__)
	if (!FeederRunning.exchange(true)) {
		Feeder = std::thread([]() {
			while (FeederRunning.load()) {
				Audio_Backend_Service();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		});
	}
#endif

	return(true);
}


void Audio_Backend_Shutdown(void)
{
#if !defined(__EMSCRIPTEN__)
	// The feeder is joined before the lock is taken, or it would be waited on while it
	// waits on the lock.
	if (FeederRunning.exchange(false) && Feeder.joinable()) {
		Feeder.join();
	}
#endif
	AUDIO_BACKEND_GUARD();

	/*
	** Streams outlive the device. Their sources and buffers die with the context, so each
	** one is told it no longer has any, and it goes on keeping its cursor from the clock
	** until whoever owns it closes it.
	*/
	for (AudioBackendStream * stream = StreamList; stream != nullptr; stream = stream->Next) {
		stream->HasSource = false;
		stream->SilentClock = Audio_Clock_Ms();
	}

	if (Context != nullptr) {
		alcMakeContextCurrent(nullptr);
		alcDestroyContext(Context);
		Context = nullptr;
	}

	if (Device != nullptr) {
		alcCloseDevice(Device);
		Device = nullptr;
	}

	Running = false;
	StateReadable = false;
}


bool Audio_Backend_Is_Running(void)
{
	AUDIO_BACKEND_GUARD();

	return(Running);
}


void Audio_Backend_Resume(void)
{
	AUDIO_BACKEND_GUARD();

	if (Device == nullptr) {
		return;
	}

	Audio_Web_Context_Resume();
}


void Audio_Backend_Service(void)
{
	AUDIO_BACKEND_GUARD();

	/*
	** A page will not start audio until the reader has interacted with it, and there is no
	** event that says the refusal has been lifted. The state is polled instead, slowly
	** enough not to matter and often enough that sound starts within a frame or two of the
	** first click or key.
	*/
	if (Device != nullptr && StateReadable) {
		bool running = (Audio_Web_Context_State() == 1);

		if (!running) {
			double now = Audio_Clock_Ms();
			if (now - LastResumeAttempt > 250.0) {
				LastResumeAttempt = now;
				Audio_Web_Context_Resume();
			}
		}

		if (running != Running) {
			Running = running;
			DebugString("Audio backend: page output %s\n", running ? "started" : "stopped");

			/*
			** What the device holds no longer lines up with the cursor the other mode was
			** keeping, so every stream restarts from where its cursor now stands.
			*/
			for (AudioBackendStream * stream = StreamList; stream != nullptr; stream = stream->Next) {
				Flush_Queue(stream, Audio_Backend_Play_Cursor(stream));
			}
		}
	}

	for (AudioBackendStream * stream = StreamList; stream != nullptr; stream = stream->Next) {
		if (!stream->Playing) {
			continue;
		}

		if (Running && stream->HasSource) {
			Service_Device(stream);
		} else {
			Service_Silent(stream);
		}
	}
}


AudioBackendStream * Audio_Backend_Open_Stream(int ringbytes, int rate, int bits, int channels)
{
	AUDIO_BACKEND_GUARD();

	if (ringbytes <= 0 || rate <= 0) {
		return(nullptr);
	}

	if (bits != 8 && bits != 16) {
		return(nullptr);
	}

	if (channels != 1 && channels != 2) {
		return(nullptr);
	}

	AudioBackendStream * stream = new AudioBackendStream;
	std::memset(stream, 0, sizeof(*stream));

	stream->Ring = new unsigned char[ringbytes];

	/*
	** Silence is the zero point of signed sixteen bit samples and the midpoint of the
	** unsigned eight bit ones, so an untouched ring has to be filled rather than cleared.
	*/
	std::memset(stream->Ring, (bits == 8) ? 0x80 : 0x00, ringbytes);

	stream->RingSize = ringbytes;
	stream->Rate = rate;
	stream->Bits = bits;
	stream->Channels = channels;
	stream->BlockAlign = (bits / 8) * channels;
	stream->Gain = 1.0f;

	stream->ChunkSize = (ringbytes / CHUNK_DIVISOR) / stream->BlockAlign * stream->BlockAlign;
	if (stream->ChunkSize < stream->BlockAlign) {
		stream->ChunkSize = stream->BlockAlign;
	}

	stream->Lookahead = ringbytes / LOOKAHEAD_DIVISOR;

	int const wanted = Byte_Rate(stream) * LOOKAHEAD_MS / 1000;
	if (wanted < stream->Lookahead) {
		stream->Lookahead = wanted;
	}

	if (stream->Lookahead < stream->ChunkSize) {
		stream->Lookahead = stream->ChunkSize;
	}

	if (Device != nullptr) {
		alGetError();
		alGenSources(1, &stream->Source);
		alGenBuffers(QUEUE_DEPTH, stream->Buffers);
		if (alGetError() == AL_NO_ERROR) {
			stream->HasSource = true;

			/*
			** The engine mixes and places its own sound, so the source is asked for a
			** straight path to the output rather than a listener and a position. Where the
			** spatialize hint is not honoured, sitting on the listener does the same.
			*/
			alSourcei(stream->Source, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
			alSourcei(stream->Source, AL_SOURCE_RELATIVE, AL_TRUE);
			alSource3f(stream->Source, AL_POSITION, 0.0f, 0.0f, 0.0f);
			alSourcef(stream->Source, AL_GAIN, stream->Gain);
			alGetError();
		}
	}

	Flush_Queue(stream, 0);

	stream->Next = StreamList;
	StreamList = stream;

	return(stream);
}


void Audio_Backend_Close_Stream(AudioBackendStream * stream)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr) {
		return;
	}

	AudioBackendStream ** link = &StreamList;
	while (*link != nullptr && *link != stream) {
		link = &(*link)->Next;
	}
	if (*link == stream) {
		*link = stream->Next;
	}

	if (stream->HasSource) {
		alSourceStop(stream->Source);
		alSourcei(stream->Source, AL_BUFFER, 0);
		alDeleteSources(1, &stream->Source);
		alDeleteBuffers(QUEUE_DEPTH, stream->Buffers);
		alGetError();
	}

	delete[] stream->Ring;
	delete stream;
}


unsigned char * Audio_Backend_Ring(AudioBackendStream * stream)
{
	AUDIO_BACKEND_GUARD();

	return((stream != nullptr) ? stream->Ring : nullptr);
}


int Audio_Backend_Ring_Size(AudioBackendStream const * stream)
{
	AUDIO_BACKEND_GUARD();

	return((stream != nullptr) ? stream->RingSize : 0);
}


void Audio_Backend_Start(AudioBackendStream * stream)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr || stream->Playing) {
		return;
	}

	stream->Playing = true;
	stream->SilentClock = Audio_Clock_Ms();

	Audio_Backend_Resume();
}


void Audio_Backend_Stop(AudioBackendStream * stream)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr || !stream->Playing) {
		return;
	}

	stream->Playing = false;

	if (stream->HasSource) {
		alSourcePause(stream->Source);
		alGetError();
	}
}


bool Audio_Backend_Is_Playing(AudioBackendStream const * stream)
{
	AUDIO_BACKEND_GUARD();

	return(stream != nullptr && stream->Playing);
}


void Audio_Backend_Seek(AudioBackendStream * stream, int offset)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr) {
		return;
	}

	if (offset < 0 || offset >= stream->RingSize) {
		offset = 0;
	}
	offset -= offset % stream->BlockAlign;

	Flush_Queue(stream, offset);
}


int Audio_Backend_Play_Cursor(AudioBackendStream const * stream)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr) {
		return(0);
	}

	return((int)((stream->PlayBase + stream->PlayedBytes) % stream->RingSize));
}


int Audio_Backend_Write_Cursor(AudioBackendStream const * stream)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr) {
		return(0);
	}

	return((int)((stream->PlayBase + stream->SubmittedBytes) % stream->RingSize));
}


int Audio_Backend_Lookahead(AudioBackendStream const * stream)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr) {
		return(0);
	}

	return(stream->Lookahead);
}


void Audio_Backend_Set_Gain(AudioBackendStream * stream, float gain)
{
	AUDIO_BACKEND_GUARD();

	if (stream == nullptr) {
		return;
	}

	stream->Gain = std::clamp(gain, 0.0f, 1.0f);

	if (stream->HasSource) {
		alSourcef(stream->Source, AL_GAIN, stream->Gain);
		alGetError();
	}
}


/*
** ---------------------------------------------------------------------------------------
** The DirectSound shaped object
** ---------------------------------------------------------------------------------------
**
** The sound driver and the movie player both do their own mixing. They decode into a
** looping buffer, watch its play cursor, and top it up ahead of that cursor, using
** DirectSound only as the thing that carries the buffer to the speakers. What follows is
** that carrier, and nothing else: the buffer is a stream from above, Lock hands back a
** window into its ring, and the cursor is the one the stream keeps.
*/

namespace {

class WebSoundBufferClass : public IDirectSoundBuffer
{
	public:
		WebSoundBufferClass(bool primary, WAVEFORMATEX const * format, int bytes);
		~WebSoundBufferClass(void);

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) override;
		ULONG STDMETHODCALLTYPE AddRef(void) override;
		ULONG STDMETHODCALLTYPE Release(void) override;

		HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS caps) override;
		HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) override;
		HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX format, DWORD size, LPDWORD written) override;
		HRESULT STDMETHODCALLTYPE GetVolume(LPLONG volume) override;
		HRESULT STDMETHODCALLTYPE GetPan(LPLONG pan) override;
		HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD frequency) override;
		HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD status) override;
		HRESULT STDMETHODCALLTYPE Initialize(IDirectSound * directsound, LPDSBUFFERDESC desc) override;
		HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, LPVOID * audio1, LPDWORD size1, LPVOID * audio2, LPDWORD size2, DWORD flags) override;
		HRESULT STDMETHODCALLTYPE Play(DWORD reserved1, DWORD priority, DWORD flags) override;
		HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) override;
		HRESULT STDMETHODCALLTYPE SetFormat(LPWAVEFORMATEX format) override;
		HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) override;
		HRESULT STDMETHODCALLTYPE SetPan(LONG pan) override;
		HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) override;
		HRESULT STDMETHODCALLTYPE Stop(void) override;
		HRESULT STDMETHODCALLTYPE Unlock(LPVOID audio1, DWORD size1, LPVOID audio2, DWORD size2) override;
		HRESULT STDMETHODCALLTYPE Restore(void) override;

		bool Is_Valid(void) const { return(IsPrimary || Stream != nullptr); }

	private:
		long References;

		/*
		** The primary buffer is the mixer itself rather than a sample, so it carries no
		** ring. It keeps a format because the movie player reads one back off it.
		*/
		bool IsPrimary;
		WAVEFORMATEX Format;

		AudioBackendStream * Stream;

		LONG Volume;
		LONG Pan;
};


/*
** DirectSound expresses attenuation in hundredths of a decibel below the source level,
** which is where the volume the game computes ends up. This is the way back out.
*/
float Gain_From_Millibels(LONG millibels)
{
	if (millibels >= DSBVOLUME_MAX) {
		return(1.0f);
	}
	if (millibels <= DSBVOLUME_MIN) {
		return(0.0f);
	}

	return(std::pow(10.0f, (float)millibels / 2000.0f));
}


WebSoundBufferClass::WebSoundBufferClass(bool primary, WAVEFORMATEX const * format, int bytes) :
	References(1),
	IsPrimary(primary),
	Stream(nullptr),
	Volume(DSBVOLUME_MAX),
	Pan(DSBPAN_CENTER)
{
	std::memset(&Format, 0, sizeof(Format));

	if (format != nullptr) {
		Format = *format;
	} else {
		Format.wFormatTag = WAVE_FORMAT_PCM;
		Format.nChannels = 1;
		Format.nSamplesPerSec = 22050;
		Format.wBitsPerSample = 16;
		Format.nBlockAlign = 2;
		Format.nAvgBytesPerSec = 44100;
	}

	if (!primary) {
		Stream = Audio_Backend_Open_Stream(bytes, (int)Format.nSamplesPerSec, (int)Format.wBitsPerSample, (int)Format.nChannels);
	}
}


WebSoundBufferClass::~WebSoundBufferClass(void)
{
	Audio_Backend_Close_Stream(Stream);
	Stream = nullptr;
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::QueryInterface(REFIID, void ** object)
{
	if (object != nullptr) {
		*object = nullptr;
	}
	return(E_NOINTERFACE);
}


ULONG STDMETHODCALLTYPE WebSoundBufferClass::AddRef(void)
{
	References++;
	return((ULONG)References);
}


ULONG STDMETHODCALLTYPE WebSoundBufferClass::Release(void)
{
	References--;
	if (References <= 0) {
		delete this;
		return(0);
	}
	return((ULONG)References);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetCaps(LPDSBCAPS caps)
{
	if (caps == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	caps->dwFlags = IsPrimary ? DSBCAPS_PRIMARYBUFFER : DSBCAPS_CTRLVOLUME;
	caps->dwBufferBytes = (DWORD)Audio_Backend_Ring_Size(Stream);
	caps->dwUnlockTransferRate = 0;
	caps->dwPlayCpuOverhead = 0;

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetCurrentPosition(LPDWORD play, LPDWORD write)
{
	if (Stream == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	if (play != nullptr) {
		*play = (DWORD)Audio_Backend_Play_Cursor(Stream);
	}
	if (write != nullptr) {
		*write = (DWORD)Audio_Backend_Write_Cursor(Stream);
	}

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetFormat(LPWAVEFORMATEX format, DWORD size, LPDWORD written)
{
	if (format == nullptr || size < sizeof(WAVEFORMATEX)) {
		return(DSERR_INVALIDPARAM);
	}

	*format = Format;
	if (written != nullptr) {
		*written = sizeof(WAVEFORMATEX);
	}

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetVolume(LPLONG volume)
{
	if (volume == nullptr) {
		return(DSERR_INVALIDPARAM);
	}
	*volume = Volume;
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetPan(LPLONG pan)
{
	if (pan == nullptr) {
		return(DSERR_INVALIDPARAM);
	}
	*pan = Pan;
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetFrequency(LPDWORD frequency)
{
	if (frequency == nullptr) {
		return(DSERR_INVALIDPARAM);
	}
	*frequency = Format.nSamplesPerSec;
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::GetStatus(LPDWORD status)
{
	if (status == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	/*
	** Every buffer the engine starts is started looping, so the two bits move together and
	** the callers that test either one see the same answer.
	*/
	*status = Audio_Backend_Is_Playing(Stream) ? (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING) : 0;

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::Initialize(IDirectSound *, LPDSBUFFERDESC)
{
	return(DSERR_UNSUPPORTED);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::Lock(DWORD offset, DWORD bytes, LPVOID * audio1, LPDWORD size1, LPVOID * audio2, LPDWORD size2, DWORD flags)
{
	if (Stream == nullptr || audio1 == nullptr || size1 == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	unsigned char * ring = Audio_Backend_Ring(Stream);
	DWORD size = (DWORD)Audio_Backend_Ring_Size(Stream);

	if ((flags & DSBLOCK_ENTIREBUFFER) != 0) {
		offset = 0;
		bytes = size;
	} else if ((flags & DSBLOCK_FROMWRITECURSOR) != 0) {
		offset = (DWORD)Audio_Backend_Write_Cursor(Stream);
	}

	if (offset >= size || bytes > size) {
		return(DSERR_INVALIDPARAM);
	}

	DWORD first = std::min(bytes, size - offset);

	*audio1 = ring + offset;
	*size1 = first;

	if (audio2 != nullptr) {
		*audio2 = (bytes > first) ? ring : nullptr;
	}
	if (size2 != nullptr) {
		*size2 = bytes - first;
	}

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::Play(DWORD, DWORD, DWORD)
{
	if (IsPrimary) {
		Audio_Backend_Resume();
		return(DS_OK);
	}

	Audio_Backend_Start(Stream);

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::SetCurrentPosition(DWORD position)
{
	if (Stream == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	Audio_Backend_Seek(Stream, (int)position);

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::SetFormat(LPWAVEFORMATEX format)
{
	/*
	** Only the primary buffer takes a format after creation, and on a page it is the
	** browser that decides the output format, so the request is recorded and no more. The
	** movie player reads it back to find out whether it has to be restored later.
	*/
	if (!IsPrimary) {
		return(DSERR_INVALIDPARAM);
	}

	if (format == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	Format = *format;

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::SetVolume(LONG volume)
{
	Volume = std::clamp(volume, (LONG)DSBVOLUME_MIN, (LONG)DSBVOLUME_MAX);

	Audio_Backend_Set_Gain(Stream, Gain_From_Millibels(Volume));

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::SetPan(LONG pan)
{
	Pan = std::clamp(pan, (LONG)DSBPAN_LEFT, (LONG)DSBPAN_RIGHT);
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::SetFrequency(DWORD)
{
	return(DSERR_UNSUPPORTED);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::Stop(void)
{
	Audio_Backend_Stop(Stream);
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::Unlock(LPVOID, DWORD, LPVOID, DWORD)
{
	/*
	** The ring is the buffer, so a write is already in place by the time it is unlocked,
	** exactly as it is for a DirectSound buffer the hardware reads directly.
	*/
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundBufferClass::Restore(void)
{
	// Nothing takes the page's audio away, so a buffer is never lost and never needs one.
	return(DS_OK);
}


class WebSoundObjectClass : public IDirectSound
{
	public:
		WebSoundObjectClass(void) : References(1) {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) override;
		ULONG STDMETHODCALLTYPE AddRef(void) override;
		ULONG STDMETHODCALLTYPE Release(void) override;

		HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPDSBUFFERDESC desc, IDirectSoundBuffer ** buffer, IUnknown * outer) override;
		HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps) override;
		HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(IDirectSoundBuffer * original, IDirectSoundBuffer ** duplicate) override;
		HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD level) override;
		HRESULT STDMETHODCALLTYPE Compact(void) override;
		HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD config) override;
		HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD config) override;
		HRESULT STDMETHODCALLTYPE Initialize(GUID * device) override;

	private:
		long References;
};


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::QueryInterface(REFIID, void ** object)
{
	if (object != nullptr) {
		*object = nullptr;
	}
	return(E_NOINTERFACE);
}


ULONG STDMETHODCALLTYPE WebSoundObjectClass::AddRef(void)
{
	References++;
	return((ULONG)References);
}


ULONG STDMETHODCALLTYPE WebSoundObjectClass::Release(void)
{
	References--;
	if (References <= 0) {
		Audio_Backend_Shutdown();
		delete this;
		return(0);
	}
	return((ULONG)References);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::CreateSoundBuffer(LPDSBUFFERDESC desc, IDirectSoundBuffer ** buffer, IUnknown * outer)
{
	if (buffer == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	*buffer = nullptr;

	if (outer != nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	/*
	** DirectSound rejects a description whose size field does not match the structure it
	** knows. Keeping that check means a caller that hands over an uninitialized
	** description is refused here just as it is on Windows, rather than being given a
	** buffer built from whatever was on the stack.
	*/
	if (desc == nullptr || desc->dwSize != sizeof(DSBUFFERDESC)) {
		return(DSERR_INVALIDPARAM);
	}

	bool primary = (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0;

	if (!primary) {
		if (desc->lpwfxFormat == nullptr || desc->dwBufferBytes == 0) {
			return(DSERR_INVALIDPARAM);
		}

		WAVEFORMATEX const * format = desc->lpwfxFormat;
		if (format->wFormatTag != WAVE_FORMAT_PCM) {
			return(DSERR_INVALIDPARAM);
		}
		if (format->nChannels < 1 || format->nChannels > 2) {
			return(DSERR_INVALIDPARAM);
		}
		if (format->wBitsPerSample != 8 && format->wBitsPerSample != 16) {
			return(DSERR_INVALIDPARAM);
		}
		if (format->nSamplesPerSec < 1000 || format->nSamplesPerSec > 192000) {
			return(DSERR_INVALIDPARAM);
		}
	}

	WebSoundBufferClass * created = new WebSoundBufferClass(primary, primary ? nullptr : desc->lpwfxFormat, (int)desc->dwBufferBytes);
	if (!created->Is_Valid()) {
		created->Release();
		return(DSERR_OUTOFMEMORY);
	}

	*buffer = created;

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::GetCaps(LPDSCAPS caps)
{
	if (caps == nullptr) {
		return(DSERR_INVALIDPARAM);
	}

	DWORD size = caps->dwSize;
	std::memset(caps, 0, sizeof(*caps));
	caps->dwSize = size;

	caps->dwMinSecondarySampleRate = 4000;
	caps->dwMaxSecondarySampleRate = 48000;
	caps->dwPrimaryBuffers = 1;

	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::DuplicateSoundBuffer(IDirectSoundBuffer *, IDirectSoundBuffer ** duplicate)
{
	if (duplicate != nullptr) {
		*duplicate = nullptr;
	}
	return(DSERR_UNSUPPORTED);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::SetCooperativeLevel(HWND, DWORD)
{
	// A page shares its audio with the rest of the browser and cannot claim any of it.
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::Compact(void)
{
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::GetSpeakerConfig(LPDWORD config)
{
	if (config == nullptr) {
		return(DSERR_INVALIDPARAM);
	}
	*config = 0;
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::SetSpeakerConfig(DWORD)
{
	return(DS_OK);
}


HRESULT STDMETHODCALLTYPE WebSoundObjectClass::Initialize(GUID *)
{
	return(DSERR_UNSUPPORTED);
}

}


LPDIRECTSOUND Audio_Create_Sound_Object(void)
{
	/*
	** A page that will not give out audio is not a failure to start the sound driver. The
	** object is handed back either way and its streams keep their cursors moving, so the
	** engine retires its samples on time and starts making a sound the moment the page
	** allows one.
	*/
	Audio_Backend_Init();

	return(new WebSoundObjectClass);
}

#endif	// OPENTS_NO_AUDIO_BACKEND

#endif
