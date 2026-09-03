/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The sound driver's output, and the only translation unit that includes miniaudio. It has
// two layers. The lower one carries a looping byte ring out to the host's audio device;
// the upper one dresses that ring in the DirectSound secondary buffer contract the sound
// driver and the movie player are written against, so neither of them changes to reach it.

#include "always.h"

#include "audiobackend.h"


#include "dbgprint.h"

/*
** Only the device and the format conversion are wanted here. The decoders, the synthesis
** and the higher level engine would compile a great deal of code this file never calls.
*/
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>


namespace {

/*
** Milliseconds from a clock that only ever moves forward. Only the silent cursor reads it,
** and it cares about differences rather than where the count starts.
*/
uint32_t Now_Milliseconds(void)
{
	return((uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
}

enum {
	/*
	** Five sample slots and the movie stream. The slots are fixed rather than allocated so
	** that the device thread can walk them without taking a lock against the game thread.
	*/
	MAX_STREAMS = 8,

	// Frames converted in one pass, which bounds the scratch buffers on the device stack.
	SCRATCH_FRAMES = 512,

	// A converter that reports neither input taken nor output produced this many times has
	// nothing further to give, and the pass stops rather than spinning.
	MAX_CONVERT_PASSES = 64,
};

enum {
	SLOT_FREE = 0,
	SLOT_LIVE,
	SLOT_RETIRING,
};

/*
** How far beyond the play cursor the device is kept supplied, which is how far the write
** cursor can run ahead.
**
** The bound on it is what the caller has actually written. Neither caller keeps less than
** a quarter of its ring written ahead of the play cursor: the sound driver refills a
** quarter whenever the cursor comes within a quarter of what it last wrote, and the movie
** player refills half a ring whenever the cursor crosses into the other half. Taking more
** than that would carry out the previous lap of the ring in place of audio not yet
** decoded, so the duration is clamped to a quarter ring and the shorter of the two wins.
*/
int const LOOKAHEAD_MS = 90;

/*
** Playback state has to keep advancing even when no device opened, or the driver would
** never retire a sample and would run out of tracker slots. A silent cursor moves no
** further than this in one pass, so one long frame steps over a fraction of a sample
** rather than the whole of it.
*/
double const SILENT_MAX_STEP = 250.0;

}


struct AudioBackendStream
{
	std::atomic<int> State;

	unsigned char * Ring;
	int RingSize;

	int Rate;
	int Bits;
	int Channels;
	int BlockAlign;
	int Lookahead;

	std::atomic<bool> Playing;
	std::atomic<float> Gain;

	/*
	** The play cursor is the ring offset playback started from plus the bytes the device
	** has taken since. The device thread is the only writer of the byte count, so a reader
	** on the game thread sees a cursor that only ever moves forward.
	*/
	std::atomic<int> PlayBase;
	std::atomic<long long> TakenBytes;

	/*
	** A seek is applied by the device thread rather than the caller's, so that the
	** converter is only ever touched from one thread. Negative means none is pending.
	*/
	std::atomic<int> SeekRequest;

	/*
	** Bumped whenever the device thread leaves this slot. A closed slot is freed only after
	** the count has moved, which is what proves the thread is no longer inside it.
	*/
	std::atomic<unsigned> Passes;

	// The pass count at the moment the slot was closed, which retirement waits to pass.
	unsigned RetiredAt;

	ma_data_converter Converter;
	bool HasConverter;

	// The reading the silent cursor was last advanced from, in host milliseconds.
	uint32_t SilentClock;
};


namespace {

AudioBackendStream Slots[MAX_STREAMS];

ma_device Device;
bool DeviceReady = false;
std::atomic<bool> Running{false};


int Byte_Rate(AudioBackendStream const * stream)
{
	return(stream->Rate * stream->BlockAlign);
}


ma_format Source_Format(int bits)
{
	return((bits == 16) ? ma_format_s16 : ma_format_u8);
}


/*
** The bytes the device is holding for a stream but has not yet sounded. The play cursor
** trails the bytes taken by this much, which is what keeps the region between the play and
** write cursors the region the caller must leave alone.
*/
int Latency_Bytes(AudioBackendStream const * stream)
{
	if (!DeviceReady || Device.sampleRate == 0) {
		return(0);
	}

	long long frames = (long long)Device.playback.internalPeriodSizeInFrames;
	long long bytes = frames * Byte_Rate(stream) / (long long)Device.sampleRate;
	return((int)std::min<long long>(bytes, stream->RingSize / 4));
}


/*
** Reads one span of a stream's ring into a contiguous buffer, wrapping at the end. The
** ring is never short: a caller that has not decoded far enough gets its previous lap
** carried out, which is what a starved DirectSound buffer does.
*/
void Read_Ring(AudioBackendStream const * stream, long long offset, unsigned char * dest, int bytes)
{
	int start = (int)(offset % stream->RingSize);
	int first = std::min(bytes, stream->RingSize - start);

	std::memcpy(dest, stream->Ring + start, (size_t)first);

	if (bytes > first) {
		std::memcpy(dest + first, stream->Ring, (size_t)(bytes - first));
	}
}


// Carries one stream into the mix for the length of a device pass.
void Mix_Stream(AudioBackendStream * stream, float * output, ma_uint32 frames)
{
	int pending = stream->SeekRequest.exchange(-1, std::memory_order_acq_rel);
	if (pending >= 0) {
		stream->PlayBase.store(pending, std::memory_order_relaxed);
		stream->TakenBytes.store(0, std::memory_order_release);
		ma_data_converter_reset(&stream->Converter);
	}

	if (!stream->Playing.load(std::memory_order_acquire)) {
		return;
	}

	float const gain = stream->Gain.load(std::memory_order_relaxed);
	long long taken = stream->TakenBytes.load(std::memory_order_relaxed);
	long long const base = stream->PlayBase.load(std::memory_order_relaxed);

	unsigned char source[SCRATCH_FRAMES * 4];
	float converted[SCRATCH_FRAMES * 2];

	ma_uint32 remaining = frames;
	int idle = 0;

	while (remaining > 0 && idle < MAX_CONVERT_PASSES) {
		ma_uint64 want = std::min<ma_uint64>(remaining, SCRATCH_FRAMES);

		ma_uint64 need = 0;
		if (ma_data_converter_get_required_input_frame_count(&stream->Converter, want, &need) != MA_SUCCESS) {
			break;
		}
		need = std::clamp<ma_uint64>(need, 1, SCRATCH_FRAMES);

		Read_Ring(stream, base + taken, source, (int)need * stream->BlockAlign);

		ma_uint64 in = need;
		ma_uint64 out = want;
		if (ma_data_converter_process_pcm_frames(&stream->Converter, source, &in, converted, &out) != MA_SUCCESS) {
			break;
		}

		if (in == 0 && out == 0) {
			idle++;
			continue;
		}
		idle = 0;

		for (ma_uint64 index = 0; index < out * 2; index++) {
			output[index] += converted[index] * gain;
		}

		taken += (long long)in * stream->BlockAlign;
		output += out * 2;
		remaining -= (ma_uint32)out;
	}

	stream->TakenBytes.store(taken, std::memory_order_release);
}


void Device_Callback(ma_device * device, void * output, void const * input, ma_uint32 frames)
{
	(void)device;
	(void)input;

	float * mix = (float *)output;
	std::memset(mix, 0, (size_t)frames * 2 * sizeof(float));

	for (AudioBackendStream & slot : Slots) {
		if (slot.State.load(std::memory_order_acquire) == SLOT_LIVE) {
			Mix_Stream(&slot, mix, frames);
		}
		slot.Passes.fetch_add(1, std::memory_order_release);
	}
}


/*
** Advances a stream that has no device behind it, so that the driver still retires the
** sample. The step is taken from the host clock and capped, so one long frame does not
** carry the cursor over a whole sample.
*/
void Advance_Silent(AudioBackendStream * stream)
{
	uint32_t const now = Now_Milliseconds();
	double elapsed = (double)(now - stream->SilentClock);

	stream->SilentClock = now;

	if (elapsed <= 0.0) {
		return;
	}
	elapsed = std::min(elapsed, SILENT_MAX_STEP);

	long long const step = (long long)(elapsed * Byte_Rate(stream) / 1000.0);
	long long taken = stream->TakenBytes.load(std::memory_order_relaxed);

	taken += step - (step % stream->BlockAlign);
	stream->TakenBytes.store(taken, std::memory_order_relaxed);
}


// Releases a closed slot once the device thread has been seen to leave it.
void Retire_Slot(AudioBackendStream * stream)
{
	if (DeviceReady && Running.load(std::memory_order_acquire)) {
		unsigned const seen = stream->Passes.load(std::memory_order_acquire);
		if (seen == stream->RetiredAt) {
			return;
		}
	}

	if (stream->HasConverter) {
		ma_data_converter_uninit(&stream->Converter, nullptr);
		stream->HasConverter = false;
	}

	delete[] stream->Ring;
	stream->Ring = nullptr;

	stream->State.store(SLOT_FREE, std::memory_order_release);
}

}


bool Audio_Backend_Init(void)
{
	if (DeviceReady) {
		return(true);
	}

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = 0;
	config.dataCallback = Device_Callback;

	if (ma_device_init(nullptr, &config, &Device) != MA_SUCCESS) {
		DebugString("Audio: no output device opened; the engine runs silent\n");
		return(false);
	}

	DeviceReady = true;

	if (ma_device_start(&Device) == MA_SUCCESS) {
		Running.store(true, std::memory_order_release);
	}

	DebugString("Audio: %s at %d Hz\n", ma_get_backend_name(Device.pContext->backend), (int)Device.sampleRate);
	return(true);
}


void Audio_Backend_Shutdown(void)
{
	if (DeviceReady) {
		ma_device_uninit(&Device);
		DeviceReady = false;
		Running.store(false, std::memory_order_release);
	}

	for (AudioBackendStream & slot : Slots) {
		if (slot.State.load(std::memory_order_acquire) != SLOT_FREE) {
			slot.Playing.store(false, std::memory_order_release);
			slot.State.store(SLOT_RETIRING, std::memory_order_release);
			slot.RetiredAt = slot.Passes.load(std::memory_order_acquire) - 1;
			Retire_Slot(&slot);
		}
	}
}


bool Audio_Backend_Is_Running(void)
{
	return(Running.load(std::memory_order_acquire));
}


void Audio_Backend_Resume(void)
{
	if (!DeviceReady || Running.load(std::memory_order_acquire)) {
		return;
	}

	if (ma_device_start(&Device) == MA_SUCCESS) {
		Running.store(true, std::memory_order_release);
	}
}


void Audio_Backend_Service(void)
{
	Audio_Backend_Resume();

	bool const silent = !DeviceReady || !Running.load(std::memory_order_acquire);

	for (AudioBackendStream & slot : Slots) {
		int const state = slot.State.load(std::memory_order_acquire);

		if (state == SLOT_RETIRING) {
			Retire_Slot(&slot);
			continue;
		}

		if (state == SLOT_LIVE && silent && slot.Playing.load(std::memory_order_acquire)) {
			Advance_Silent(&slot);
		}
	}
}


AudioBackendStream * Audio_Backend_Open_Stream(int ringbytes, int rate, int bits, int channels)
{
	if (ringbytes <= 0 || rate <= 0) {
		return(nullptr);
	}

	if (bits != 8 && bits != 16) {
		return(nullptr);
	}

	if (channels != 1 && channels != 2) {
		return(nullptr);
	}

	AudioBackendStream * stream = nullptr;
	for (AudioBackendStream & slot : Slots) {
		if (slot.State.load(std::memory_order_acquire) == SLOT_FREE) {
			stream = &slot;
			break;
		}
	}

	if (stream == nullptr) {
		return(nullptr);
	}

	stream->Ring = new unsigned char[ringbytes];

	/*
	** Silence is the zero point of signed sixteen bit samples and the midpoint of the
	** unsigned eight bit ones, so an untouched ring has to be filled rather than cleared.
	*/
	std::memset(stream->Ring, (bits == 8) ? 0x80 : 0x00, (size_t)ringbytes);

	stream->RingSize = ringbytes;
	stream->Rate = rate;
	stream->Bits = bits;
	stream->Channels = channels;
	stream->BlockAlign = (bits / 8) * channels;

	int const quarter = ringbytes / 4;
	int const duration = (int)((long long)LOOKAHEAD_MS * Byte_Rate(stream) / 1000);
	stream->Lookahead = std::min(quarter, duration) / stream->BlockAlign * stream->BlockAlign;

	stream->Playing.store(false, std::memory_order_relaxed);
	stream->Gain.store(1.0f, std::memory_order_relaxed);
	stream->PlayBase.store(0, std::memory_order_relaxed);
	stream->TakenBytes.store(0, std::memory_order_relaxed);
	stream->SeekRequest.store(-1, std::memory_order_relaxed);
	stream->SilentClock = Now_Milliseconds();

	ma_uint32 const outrate = DeviceReady ? Device.sampleRate : (ma_uint32)rate;
	ma_data_converter_config converter = ma_data_converter_config_init(
		Source_Format(bits), ma_format_f32, (ma_uint32)channels, 2, (ma_uint32)rate, outrate);
	converter.resampling.algorithm = ma_resample_algorithm_linear;

	stream->HasConverter = (ma_data_converter_init(&converter, nullptr, &stream->Converter) == MA_SUCCESS);
	if (!stream->HasConverter) {
		delete[] stream->Ring;
		stream->Ring = nullptr;
		return(nullptr);
	}

	stream->State.store(SLOT_LIVE, std::memory_order_release);
	return(stream);
}


void Audio_Backend_Close_Stream(AudioBackendStream * stream)
{
	if (stream == nullptr) {
		return;
	}

	stream->Playing.store(false, std::memory_order_release);
	stream->RetiredAt = stream->Passes.load(std::memory_order_acquire);
	stream->State.store(SLOT_RETIRING, std::memory_order_release);
}


unsigned char * Audio_Backend_Ring(AudioBackendStream * stream)
{
	return((stream != nullptr) ? stream->Ring : nullptr);
}


int Audio_Backend_Ring_Size(AudioBackendStream const * stream)
{
	return((stream != nullptr) ? stream->RingSize : 0);
}


void Audio_Backend_Start(AudioBackendStream * stream)
{
	if (stream == nullptr) {
		return;
	}

	stream->SilentClock = Now_Milliseconds();
	stream->Playing.store(true, std::memory_order_release);

	Audio_Backend_Resume();
}


void Audio_Backend_Stop(AudioBackendStream * stream)
{
	if (stream != nullptr) {
		stream->Playing.store(false, std::memory_order_release);
	}
}


bool Audio_Backend_Is_Playing(AudioBackendStream const * stream)
{
	return((stream != nullptr) && stream->Playing.load(std::memory_order_acquire));
}


void Audio_Backend_Seek(AudioBackendStream * stream, int offset)
{
	if (stream == nullptr) {
		return;
	}

	if (offset < 0 || offset >= stream->RingSize) {
		offset = 0;
	}
	offset -= offset % stream->BlockAlign;

	stream->SeekRequest.store(offset, std::memory_order_release);

	/*
	** With no device thread running there is nobody to pick the request up, so the seek is
	** applied here instead.
	*/
	if (!DeviceReady || !Running.load(std::memory_order_acquire)) {
		if (stream->SeekRequest.exchange(-1, std::memory_order_acq_rel) >= 0) {
			stream->PlayBase.store(offset, std::memory_order_relaxed);
			stream->TakenBytes.store(0, std::memory_order_release);
			stream->SilentClock = Now_Milliseconds();
		}
	}
}


int Audio_Backend_Play_Cursor(AudioBackendStream const * stream)
{
	if (stream == nullptr) {
		return(0);
	}

	long long const taken = stream->TakenBytes.load(std::memory_order_acquire);
	long long const heard = std::max<long long>(0, taken - Latency_Bytes(stream));

	return((int)((stream->PlayBase.load(std::memory_order_relaxed) + heard) % stream->RingSize));
}


int Audio_Backend_Write_Cursor(AudioBackendStream const * stream)
{
	if (stream == nullptr) {
		return(0);
	}

	long long const taken = stream->TakenBytes.load(std::memory_order_acquire);

	return((int)((stream->PlayBase.load(std::memory_order_relaxed) + taken) % stream->RingSize));
}


int Audio_Backend_Lookahead(AudioBackendStream const * stream)
{
	return((stream != nullptr) ? stream->Lookahead : 0);
}


void Audio_Backend_Set_Gain(AudioBackendStream * stream, float gain)
{
	if (stream != nullptr) {
		stream->Gain.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed);
	}
}
