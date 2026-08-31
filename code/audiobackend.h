/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The sound driver's private output interface. Only audiobackend.cpp includes OpenAL, so
// no OpenAL type appears here and no other translation unit needs the library's headers.
// The sound driver is the only caller, and it reaches this through the DirectSound shaped
// object declared in dsaudio.h, which audiobackend.cpp builds on top of these calls.
//
// A stream is a looping byte ring, which is the shape the engine's mixing already assumes:
// it decodes samples into the ring itself and watches the play cursor to decide when to
// decode more. The backend only carries the ring out to the page.

#pragma once

#ifndef _WIN32

#include "win32compat.h"

struct AudioBackendStream;


/*
** The output device. Init reports whether a device opened at all; a page that refuses one
** leaves the engine running against silent streams rather than against nothing.
*/
bool Audio_Backend_Init(void);
LPDIRECTSOUND Audio_Create_Sound_Object(void);
void Audio_Backend_Shutdown(void);

/*
** A page will not start audio until the reader has interacted with it, so a stream can be
** started while output is still stopped. Audio_Backend_Is_Running reports whether the page
** is actually producing sound; Audio_Backend_Resume asks it to start and is cheap enough
** to call from the service routine on every pass.
*/
bool Audio_Backend_Is_Running(void);
void Audio_Backend_Resume(void);

// Carries each playing ring out to the device. Call at least as often as the engine
// refills its rings, which for a 32K ring is a few times per second.
void Audio_Backend_Service(void);


AudioBackendStream * Audio_Backend_Open_Stream(int ringbytes, int rate, int bits, int channels);
void Audio_Backend_Close_Stream(AudioBackendStream * stream);

// The ring the caller writes samples into. It stays valid until the stream is closed and
// its contents survive a stop, so a sample can be replayed without being decoded again.
unsigned char * Audio_Backend_Ring(AudioBackendStream * stream);
int Audio_Backend_Ring_Size(AudioBackendStream const * stream);

void Audio_Backend_Start(AudioBackendStream * stream);
void Audio_Backend_Stop(AudioBackendStream * stream);
bool Audio_Backend_Is_Playing(AudioBackendStream const * stream);

// Restarts playback at a ring offset, discarding whatever the device had already taken.
void Audio_Backend_Seek(AudioBackendStream * stream, int offset);

// The ring offset the device is playing, and the offset past which it has not yet taken
// data. The caller may write anywhere outside the region between the two.
int Audio_Backend_Play_Cursor(AudioBackendStream const * stream);
int Audio_Backend_Write_Cursor(AudioBackendStream const * stream);

// How far beyond the play cursor the device is kept supplied, which is how far the write
// cursor can run ahead. It has to be long enough that the page's own scheduler never finds
// the stream empty, and short enough to stay inside what the caller keeps written.
int Audio_Backend_Lookahead(AudioBackendStream const * stream);

// Playback gain as a linear multiplier, where 1 is unattenuated.
void Audio_Backend_Set_Gain(AudioBackendStream * stream, float gain);

#endif
