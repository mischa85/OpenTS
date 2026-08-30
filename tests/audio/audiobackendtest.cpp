/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the WebAssembly sound backend without the engine and without game data. It
// covers the contract the sound driver actually leans on: a looping ring whose play cursor
// advances at the sample rate and wraps, a lock window that splits at the wrap, and the
// status and volume calls the driver makes around them.
//
// A command-line host has no Web Audio, so the OpenAL device stays closed here and the
// streams run on the silent clock. That is the same path a page takes when it refuses
// audio outright, and it is what keeps the driver retiring its samples on time. The
// device-side queue bookkeeping needs a browser and is not covered.

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <emscripten/emscripten.h>

#include "audiobackend.h"
#include "dsaudio.h"

namespace {

int Failures = 0;


void Report(char const * name, bool ok)
{
	std::printf("%-64s %s\n", name, ok ? "ok" : "FAILED");
	if (!ok) {
		Failures++;
	}
}


void Report_Value(char const * name, long actual, long expected)
{
	bool ok = (actual == expected);
	if (!ok) {
		std::printf("%-64s FAILED (got %ld, expected %ld)\n", name, actual, expected);
		Failures++;
	} else {
		std::printf("%-64s ok\n", name);
	}
}


// Burns wall-clock time. The silent cursor is driven by the clock, so the test has to let
// real time pass rather than stepping a counter.
void Spin(double milliseconds)
{
	double start = emscripten_get_now();
	while (emscripten_get_now() - start < milliseconds) {
	}
}


void Test_Stream_Rejects_Bad_Formats(void)
{
	Report("open stream rejects a zero ring", Audio_Backend_Open_Stream(0, 22050, 16, 1) == nullptr);
	Report("open stream rejects a zero rate", Audio_Backend_Open_Stream(4096, 0, 16, 1) == nullptr);
	Report("open stream rejects twelve bit samples", Audio_Backend_Open_Stream(4096, 22050, 12, 1) == nullptr);
	Report("open stream rejects three channels", Audio_Backend_Open_Stream(4096, 22050, 16, 3) == nullptr);
}


/*
** The two buffer shapes the engine actually creates, as bytes of ring and the byte rate
** the ring plays out at: the sound driver's thirty two kilobyte effect buffer at twenty
** two kilohertz sixteen bit mono, and the movie player's two audio blocks at twenty two
** kilohertz sixteen bit stereo.
*/
struct BufferShapeType
{
	char const * Name;
	int RingSize;
	int Rate;
	int Bits;
	int Channels;
};

BufferShapeType const _Shapes[] = {
	{"the sound driver's effect buffer", 32768, 22050, 16, 1},
	{"the movie player's audio buffer", 32768, 22050, 16, 2},
};


/*
** A page hands its audio to the output from a scheduler of its own that reaches a fixed
** distance ahead, so a stream kept supplied for less than that empties between two of its
** passes: the device stops, and a caller reading the play cursor back for a clock -- which
** is what the movie player does -- stops with it. The other side of it is that the device
** must not be handed a lap of the ring the caller has not written yet, and no caller keeps
** less than a quarter of its ring written ahead of the cursor.
*/
void Test_Lookahead_Is_Long_Enough_And_Safe(void)
{
	int const NEEDED_MS = 60;

	for (BufferShapeType const & shape : _Shapes) {
		AudioBackendStream * stream = Audio_Backend_Open_Stream(shape.RingSize, shape.Rate, shape.Bits, shape.Channels);

		if (stream == nullptr) {
			Report(shape.Name, false);
			continue;
		}

		int byterate = shape.Rate * (shape.Bits / 8) * shape.Channels;
		int lookahead = Audio_Backend_Lookahead(stream);

		char line[160];

		std::snprintf(line, sizeof(line), "%s is kept at least %d ms ahead", shape.Name, NEEDED_MS);
		Report(line, lookahead >= byterate * NEEDED_MS / 1000);

		std::snprintf(line, sizeof(line), "%s is not taken beyond a quarter ring", shape.Name);
		Report(line, lookahead <= shape.RingSize / 4);

		Audio_Backend_Close_Stream(stream);
	}
}


void Test_Ring_Starts_Silent(void)
{
	AudioBackendStream * eight = Audio_Backend_Open_Stream(256, 22050, 8, 1);
	AudioBackendStream * sixteen = Audio_Backend_Open_Stream(256, 22050, 16, 1);

	bool ok = (eight != nullptr && sixteen != nullptr);
	if (ok) {
		unsigned char const * ring8 = Audio_Backend_Ring(eight);
		unsigned char const * ring16 = Audio_Backend_Ring(sixteen);

		for (int index = 0; index < 256; index++) {
			if (ring8[index] != 0x80 || ring16[index] != 0x00) {
				ok = false;
				break;
			}
		}
	}

	Report("a new ring holds silence for its sample width", ok);
	Report_Value("ring size is the size asked for", Audio_Backend_Ring_Size(eight), 256);

	Audio_Backend_Close_Stream(eight);
	Audio_Backend_Close_Stream(sixteen);
}


void Test_Cursor_Advances_And_Wraps(void)
{
	/*
	** Forty-eight kilohertz stereo runs at 192000 bytes a second, so an eight kilobyte
	** ring turns over in about forty-three milliseconds and the test does not have to sit
	** still for long to see it wrap.
	*/
	int const size = 8192;
	int const byterate = 48000 * 4;

	AudioBackendStream * stream = Audio_Backend_Open_Stream(size, 48000, 16, 2);
	if (stream == nullptr) {
		Report("stream opens for the cursor test", false);
		return;
	}

	Report("a stream does not play until it is started", !Audio_Backend_Is_Playing(stream));
	Report_Value("the play cursor starts at the beginning", Audio_Backend_Play_Cursor(stream), 0);

	Audio_Backend_Start(stream);
	Report("a started stream reports playing", Audio_Backend_Is_Playing(stream));

	/*
	** Ten milliseconds is about a quarter of the ring, so the cursor has moved but has not
	** yet come back round to where it started.
	*/
	Spin(10.0);
	Audio_Backend_Service();

	int cursor = Audio_Backend_Play_Cursor(stream);
	int expected = (int)(10.0 * byterate / 1000.0);
	Report("the cursor advances at the sample rate", cursor > expected / 2 && cursor < expected * 2);
	Report("the cursor is a whole number of frames", (cursor % 4) == 0);
	Report("the write cursor is not behind the play cursor with no device", Audio_Backend_Write_Cursor(stream) == cursor);

	/*
	** Long enough for the ring to turn over several times. The total advance is counted
	** across the wraps, so the rate is measured over a window long enough that the
	** granularity of one pass does not show up in it.
	*/
	int previous = cursor;
	long long advanced = 0;
	bool wrapped = false;
	bool inside = true;

	double start = emscripten_get_now();
	for (int pass = 0; pass < 40; pass++) {
		Spin(5.0);
		Audio_Backend_Service();

		int now = Audio_Backend_Play_Cursor(stream);
		if (now >= size || now < 0) {
			inside = false;
			break;
		}

		advanced += (now >= previous) ? (now - previous) : (now + size - previous);
		if (now < previous) {
			wrapped = true;
		}
		previous = now;
	}
	double elapsed = emscripten_get_now() - start;

	Report("the cursor stays inside the ring", inside);
	Report("the cursor wraps at the end of the ring", wrapped);

	long long expectedbytes = (long long)(elapsed * byterate / 1000.0);
	Report("the cursor keeps to the sample rate across the wraps",
		advanced > expectedbytes * 9 / 10 && advanced < expectedbytes * 11 / 10);

	Audio_Backend_Stop(stream);
	Report("a stopped stream reports not playing", !Audio_Backend_Is_Playing(stream));

	cursor = Audio_Backend_Play_Cursor(stream);
	Spin(20.0);
	Audio_Backend_Service();
	Report("a stopped cursor does not advance", Audio_Backend_Play_Cursor(stream) == cursor);

	Audio_Backend_Seek(stream, 1024);
	Report_Value("a seek moves the cursor to the offset asked for", Audio_Backend_Play_Cursor(stream), 1024);
	Report_Value("a seek moves the write cursor with it", Audio_Backend_Write_Cursor(stream), 1024);

	Audio_Backend_Seek(stream, size + 16);
	Report_Value("a seek past the ring falls back to the beginning", Audio_Backend_Play_Cursor(stream), 0);

	Audio_Backend_Close_Stream(stream);
}


/*
** The driver's own view. Everything below goes through the DirectSound shaped object,
** which is the only surface dsaudio.cpp and the movie player ever touch.
*/
void Test_Sound_Object(void)
{
	LPDIRECTSOUND object = Audio_Create_Sound_Object();
	if (object == nullptr) {
		Report("a sound object is created", false);
		return;
	}
	Report("a sound object is created", true);

	Report("the cooperative level is accepted", object->SetCooperativeLevel(nullptr, DSSCL_PRIORITY) == DS_OK);

	DSCAPS caps;
	std::memset(&caps, 0, sizeof(caps));
	caps.dwSize = sizeof(caps);
	Report("device capabilities are reported", object->GetCaps(&caps) == DS_OK);
	Report("no emulated driver is claimed", (caps.dwFlags & DSCAPS_EMULDRIVER) == 0);

	/*
	** The driver hands over an uninitialized description when it tries to rebuild a buffer
	** for a sample whose format does not match. DirectSound refuses one whose size field is
	** wrong, and so must this, or the driver would be given a buffer built out of whatever
	** happened to be on the stack.
	*/
	DSBUFFERDESC junk;
	std::memset(&junk, 0xCD, sizeof(junk));
	IDirectSoundBuffer * refused = nullptr;
	Report("a description with a wrong size field is refused", object->CreateSoundBuffer(&junk, &refused, nullptr) != DS_OK);
	Report("a refused description yields no buffer", refused == nullptr);

	/*
	** The primary buffer.
	*/
	DSBUFFERDESC desc;
	std::memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME;

	IDirectSoundBuffer * primary = nullptr;
	Report("the primary buffer is created", object->CreateSoundBuffer(&desc, &primary, nullptr) == DS_OK && primary != nullptr);

	if (primary != nullptr) {
		WAVEFORMATEX format;
		std::memset(&format, 0, sizeof(format));
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 1;
		format.nSamplesPerSec = 22050;
		format.wBitsPerSample = 16;
		format.nBlockAlign = 2;
		format.nAvgBytesPerSec = 44100;

		Report("the primary buffer format is accepted", primary->SetFormat(&format) == DS_OK);

		WAVEFORMATEX readback;
		std::memset(&readback, 0, sizeof(readback));
		Report("the primary buffer format reads back", primary->GetFormat(&readback, sizeof(readback), nullptr) == DS_OK);
		Report("the primary buffer keeps the format it was given",
			readback.nChannels == 1 && readback.nSamplesPerSec == 22050 && readback.wBitsPerSample == 16);

		Report("the primary buffer plays", primary->Play(0, 0, DSBPLAY_LOOPING) == DS_OK);
		Report_Value("releasing the primary buffer drops its last reference", (long)primary->Release(), 0);
	}

	/*
	** A secondary buffer, built the way the sound driver builds one.
	*/
	int const size = 32 * 1024;

	WAVEFORMATEX wave;
	std::memset(&wave, 0, sizeof(wave));
	wave.wFormatTag = WAVE_FORMAT_PCM;
	wave.nChannels = 1;
	wave.nSamplesPerSec = 22050;
	wave.wBitsPerSample = 16;
	wave.nBlockAlign = 2;
	wave.nAvgBytesPerSec = 44100;

	std::memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DSBCAPS_CTRLVOLUME;
	desc.dwBufferBytes = size;
	desc.lpwfxFormat = &wave;

	IDirectSoundBuffer * buffer = nullptr;
	bool created = (object->CreateSoundBuffer(&desc, &buffer, nullptr) == DS_OK && buffer != nullptr);
	Report("a secondary buffer is created", created);

	if (created) {
		DSBCAPS bufcaps;
		std::memset(&bufcaps, 0, sizeof(bufcaps));
		bufcaps.dwSize = sizeof(bufcaps);
		buffer->GetCaps(&bufcaps);
		Report_Value("the buffer is the size asked for", (long)bufcaps.dwBufferBytes, size);

		/*
		** Locking the whole buffer, which is what the driver does to prime a sample.
		*/
		void * first = nullptr;
		void * second = nullptr;
		DWORD firstsize = 0;
		DWORD secondsize = 0;

		Report("the whole buffer locks", buffer->Lock(0, size, &first, &firstsize, &second, &secondsize, 0) == DS_OK);
		Report_Value("a whole buffer lock is one region", (long)firstsize, size);
		Report_Value("a whole buffer lock has no second region", (long)secondsize, 0);
		Report("a lock hands back writable memory", first != nullptr);

		if (first != nullptr) {
			std::memset(first, 0x5A, size);
		}
		Report("the whole buffer unlocks", buffer->Unlock(first, firstsize, second, secondsize) == DS_OK);

		/*
		** Locking half the buffer three quarters of the way in, which is the wrapping case
		** the driver's refill hits once per turn of the ring.
		*/
		first = nullptr;
		second = nullptr;
		firstsize = 0;
		secondsize = 0;

		Report("a lock that runs off the end succeeds", buffer->Lock(size * 3 / 4, size / 2, &first, &firstsize, &second, &secondsize, 0) == DS_OK);
		Report_Value("the first region reaches the end of the buffer", (long)firstsize, size / 4);
		Report_Value("the second region holds the remainder", (long)secondsize, size / 4);
		Report("the second region starts at the beginning of the buffer", second != nullptr && second < first);
		buffer->Unlock(first, firstsize, second, secondsize);

		Report("a lock past the end of the buffer is refused",
			buffer->Lock(size, 16, &first, &firstsize, &second, &secondsize, 0) != DS_OK);

		/*
		** Status, position and volume around a play.
		*/
		DWORD status = 0xFFFFFFFF;
		Report("an idle buffer reports its status", buffer->GetStatus(&status) == DS_OK);
		Report_Value("an idle buffer is not playing", (long)status, 0);

		Report("the buffer rewinds", buffer->SetCurrentPosition(0) == DS_OK);
		Report("the buffer plays", buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK);

		buffer->GetStatus(&status);
		Report("a playing buffer reports playing", (status & DSBSTATUS_PLAYING) != 0);
		Report("a looping buffer reports looping", (status & DSBSTATUS_LOOPING) != 0);

		DWORD play = 0;
		DWORD write = 0;
		Report("the play position is reported", buffer->GetCurrentPosition(&play, &write) == DS_OK);
		Report_Value("playback starts at the rewound position", (long)play, 0);

		Spin(30.0);
		Audio_Backend_Service();
		buffer->GetCurrentPosition(&play, &write);
		Report("the play position moves while the buffer plays", play > 0 && play < (DWORD)size);

		Report("the volume is accepted", buffer->SetVolume(-2000) == DS_OK);
		LONG volume = 0;
		buffer->GetVolume(&volume);
		Report_Value("the volume reads back", (long)volume, -2000);

		Report("a volume below the floor is clamped", buffer->SetVolume(DSBVOLUME_MIN - 5000) == DS_OK);
		buffer->GetVolume(&volume);
		Report_Value("the clamped volume is the floor", (long)volume, DSBVOLUME_MIN);

		Report("the buffer stops", buffer->Stop() == DS_OK);
		buffer->GetStatus(&status);
		Report_Value("a stopped buffer is not playing", (long)status, 0);

		Report("a buffer is never lost", buffer->Restore() == DS_OK);

		Report_Value("releasing the buffer drops its last reference", (long)buffer->Release(), 0);
	}

	Report_Value("releasing the sound object drops its last reference", (long)object->Release(), 0);
}

}


/*
** The engine logs through this. The harness prints nothing so that the results stay
** readable, but the symbol has to exist for the backend to link.
*/
void DebugString(char const *, ...)
{
}


int main(void)
{
	std::printf("OpenTS WebAssembly audio backend\n\n");

	bool device = Audio_Backend_Init();
	std::printf("Output device: %s\n", device ? "open" : "none, streams run silent");
	std::printf("Page output:   %s\n\n", Audio_Backend_Is_Running() ? "running" : "stopped");

	// Asking for a resume without a page must not fall over.
	Audio_Backend_Resume();
	Audio_Backend_Service();
	Report("the backend survives a service pass with no streams", true);

	Test_Stream_Rejects_Bad_Formats();
	Test_Ring_Starts_Silent();
	Test_Lookahead_Is_Long_Enough_And_Safe();
	Test_Cursor_Advances_And_Wraps();

	Audio_Backend_Shutdown();

	Test_Sound_Object();

	std::printf("\n%s\n", (Failures == 0) ? "All audio backend checks passed." : "Audio backend checks FAILED.");

	return((Failures == 0) ? 0 : 1);
}
