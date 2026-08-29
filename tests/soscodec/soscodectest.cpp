/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the SOS ADPCM decoders without the engine or any game data. The known-answer
// vectors below were computed by hand from the ADPCM difference and index tables and
// checked against the decoder's own tables; they are what pins the implementation to the
// original stream format rather than to a generic ADPCM description.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "soscomp.h"

namespace {

int Failures = 0;


void Report(char const * name, bool ok)
{
	std::printf("%-52s %s\n", name, ok ? "ok" : "FAILED");
	if (!ok) {
		Failures++;
	}
}


void Check(char const * name, long actual, long expected)
{
	bool ok = (actual == expected);
	if (!ok) {
		std::printf("%-52s FAILED (got %ld, expected %ld)\n", name, actual, expected);
		Failures++;
	} else {
		std::printf("%-52s ok\n", name);
	}
}


bool Same_Bytes(void const * left, void const * right, std::size_t size)
{
	return std::memcmp(left, right, size) == 0;
}


/*
** Reference tables, transcribed from the decoder's own definition. Keeping a second copy
** here means a test failure distinguishes a broken decoder from a broken table.
*/

short const StepTable[89] = {
	7,     8,     9,     10,    11,    12,    13,    14,
	16,    17,    19,    21,    23,    25,    28,    31,
	34,    37,    41,    45,    50,    55,    60,    66,
	73,    80,    88,    97,    107,   118,   130,   143,
	157,   173,   190,   209,   230,   253,   279,   307,
	337,   371,   408,   449,   494,   544,   598,   658,
	724,   796,   876,   963,   1060,  1166,  1282,  1411,
	1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
	3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
	7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

short const IndexAdjust[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};


int Reference_Difference(int step, int token)
{
	int difference = step >> 3;

	if ((token & 4) != 0) {
		difference += step;
	}
	if ((token & 2) != 0) {
		difference += step >> 1;
	}
	if ((token & 1) != 0) {
		difference += step >> 2;
	}
	if ((token & 8) != 0) {
		difference = -difference;
	}

	return difference;
}


int Reference_Next_Index(int index, int token)
{
	int next = index + IndexAdjust[token];

	if (next < 0) {
		next = 0;
	}
	if (next > 88) {
		next = 88;
	}

	return next;
}


int Clamp(int sample)
{
	if (sample > 32767) {
		return 32767;
	}
	if (sample < -32768) {
		return -32768;
	}

	return sample;
}


/*
** Golden values lifted verbatim from the historical difference and index tables, spread
** across the whole index range. The tables the decoder builds must reproduce these.
*/
struct GoldenEntry {
	int Index;
	int Token;
	int Difference;
	int NextIndex;
};

GoldenEntry const GoldenTable[] = {
	{0,  0,  0,      0},
	{0,  1,  1,      0},
	{0,  2,  3,      0},
	{0,  3,  4,      0},
	{0,  4,  7,      2},
	{0,  5,  8,      4},
	{0,  6,  10,     6},
	{0,  7,  11,     8},
	{0,  8,  0,      0},
	{0,  9,  -1,     0},
	{0,  10, -3,     0},
	{0,  11, -4,     0},
	{0,  12, -7,     2},
	{0,  13, -8,     4},
	{0,  14, -10,    6},
	{0,  15, -11,    8},
	{1,  0,  1,      0},
	{1,  1,  3,      0},
	{1,  2,  5,      0},
	{1,  3,  7,      0},
	{1,  4,  9,      3},
	{2,  0,  1,      1},
	{2,  1,  3,      1},
	{2,  2,  5,      1},
	{2,  3,  7,      1},
	{2,  4,  10,     4},
	{2,  5,  12,     6},
	{2,  6,  14,     8},
	{2,  7,  16,     10},
	{2,  15, -16,    10},
	{8,  7,  30,     16},
	{16, 7,  63,     24},
	{24, 7,  136,    32},
	{32, 7,  293,    40},
	{40, 7,  631,    48},
	{48, 7,  1357,   56},
	{56, 7,  2910,   64},
	{88, 6,  53245,  88},
	{88, 7,  61436,  88},
	{88, 8,  -4095,  87},
	{88, 9,  -12286, 87},
	{88, 10, -20478, 87},
	{88, 11, -28669, 87},
	{88, 12, -36862, 88},
	{88, 13, -45053, 88},
	{88, 14, -53245, 88},
	{88, 15, -61436, 88},
};


void Test_Tables(void)
{
	bool ok = true;

	for (GoldenEntry const & entry : GoldenTable) {
		if (Reference_Difference(StepTable[entry.Index], entry.Token) != entry.Difference) {
			ok = false;
		}
		if (Reference_Next_Index(entry.Index, entry.Token) != entry.NextIndex) {
			ok = false;
		}
	}

	Report("tables: golden difference and index entries", ok);
}


/*
** Drives sosCODECDecompressData over a whole buffer in one call. The caller owns the
** stream state so a test can preload it.
*/
void Decode_Mono16(_SOS_COMPRESS_INFO & info, unsigned char const * source, short * dest, unsigned long bytes)
{
	info.lpSource = (char *)source;
	info.lpDest = (char *)dest;
	sosCODECDecompressData(&info, bytes);
}


void Test_Mono16_Known_Answer(void)
{
	/*
	** 0x12 0x34 decodes low nybble first, so the tokens are 2, 1, 4, 3. Starting from a
	** zero predictor at index 0 (step 7) that gives differences 3, 1, 7 and 7, and the
	** index walks 0, 0, 0, 2, 1.
	*/
	unsigned char const source[] = {0x12, 0x34};
	short expected[] = {3, 4, 11, 18};
	short dest[4] = {0, 0, 0, 0};

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);

	Decode_Mono16(info, source, dest, sizeof(dest));

	Report("sosCODEC mono16: known-answer samples", Same_Bytes(dest, expected, sizeof(expected)));
	Check("sosCODEC mono16: final dwPredicted", info.dwPredicted, 18);

	// wIndex carries the step index scaled by 32; index 1 is stored as 32.
	Check("sosCODEC mono16: final wIndex (scaled by 32)", info.wIndex, 32);
}


void Test_Mono16_Return_Value(void)
{
	unsigned char const source[] = {0x12, 0x34};
	short dest[4] = {0, 0, 0, 0};

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	info.lpSource = (char *)source;
	info.lpDest = (char *)dest;

	Check("sosCODEC mono16: returns the destination byte count",
		(long)sosCODECDecompressData(&info, sizeof(dest)), (long)sizeof(dest));

	info.wChannels = 2;
	Check("sosCODEC: unsupported format decodes nothing",
		(long)sosCODECDecompressData(&info, sizeof(dest)), 0);
}


void Test_Mono16_Saturation(void)
{
	// Token 7 from index 0 adds 11, then 30 from index 8; both overshoot +32767.
	unsigned char const high_source[] = {0x77};
	short high_dest[2] = {0, 0};
	short high_expected[] = {32767, 32767};

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	info.dwPredicted = 32760;

	Decode_Mono16(info, high_source, high_dest, sizeof(high_dest));

	Report("sosCODEC mono16: saturates at +32767", Same_Bytes(high_dest, high_expected, sizeof(high_expected)));
	Check("sosCODEC mono16: index still advances past saturation", info.wIndex, 16 * 32);

	// Token 15 is the same magnitude negated, so the predictor bottoms out at -32768.
	unsigned char const low_source[] = {0xFF};
	short low_dest[2] = {0, 0};
	short low_expected[] = {-32768, -32768};

	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	info.dwPredicted = -32760;

	Decode_Mono16(info, low_source, low_dest, sizeof(low_dest));

	Report("sosCODEC mono16: saturates at -32768", Same_Bytes(low_dest, low_expected, sizeof(low_expected)));
}


void Test_General_Mono16_Known_Answer(void)
{
	// The general decoder walks the same tokens as the 16-bit mono path.
	unsigned char const source[] = {0x12, 0x34};
	short expected[] = {3, 4, 11, 18};
	short dest[4] = {0, 0, 0, 0};

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	General_sosCODECInitStream(&info);
	info.lpSource = (char *)source;
	info.lpDest = (char *)dest;

	unsigned long returned = General_sosCODECDecompressData(&info, sizeof(dest));

	Report("General mono16: known-answer samples", Same_Bytes(dest, expected, sizeof(expected)));
	Check("General mono16: returns the destination byte count", (long)returned, (long)sizeof(dest));
	Check("General mono16: final dwPredicted", info.dwPredicted, 18);

	// This decoder stores the raw step index, unlike the 16-bit mono path.
	Check("General mono16: final wIndex (unscaled)", info.wIndex, 1);
	Check("General mono16: final wStep", info.wStep, 8);
	Check("General mono16: final wCode", info.wCode, 3);
	Check("General mono16: final wCodeBuf", info.wCodeBuf, 0x34);
	Check("General mono16: final dwDifference", info.dwDifference, 7);
	Check("General mono16: final dwSampleIndex", (long)info.dwSampleIndex, 4);
}


void Test_General_Mono8_Known_Answer(void)
{
	/*
	** Four 0x77 bytes are eight maximum-positive tokens, so the predictor runs
	** 11, 41, 104, 240, 533, 1164, 2521, 5431 and the index steps by eight each time.
	** An 8-bit sample is the predictor's high byte with its sign bit flipped.
	*/
	unsigned char const source[] = {0x77, 0x77, 0x77, 0x77};
	unsigned char expected[] = {0x80, 0x80, 0x80, 0x80, 0x82, 0x84, 0x89, 0x95};
	unsigned char dest[8];
	std::memset(dest, 0, sizeof(dest));

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 8;
	info.wChannels = 1;
	General_sosCODECInitStream(&info);
	info.lpSource = (char *)source;
	info.lpDest = (char *)dest;

	General_sosCODECDecompressData(&info, sizeof(dest));

	Report("General mono8: known-answer samples", Same_Bytes(dest, expected, sizeof(expected)));
	Check("General mono8: final dwPredicted", info.dwPredicted, 5431);
	Check("General mono8: final wIndex", info.wIndex, 64);
	Check("General mono8: final wStep", info.wStep, 3327);
	Check("General mono8: final dwSampleIndex", (long)info.dwSampleIndex, 8);
}


void Test_General_Stereo16_Known_Answer(void)
{
	/*
	** Stereo interleaves whole token bytes: the left channel takes bytes 0 and 2, the
	** right channel bytes 1 and 3. Left sees tokens 2, 1, 4, 3 and right sees 7, 7, 7, 7.
	*/
	unsigned char const source[] = {0x12, 0x77, 0x34, 0x77};
	short expected[] = {3, 11, 4, 41, 11, 104, 18, 240};
	short dest[8];
	std::memset(dest, 0, sizeof(dest));

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 2;
	General_sosCODECInitStream(&info);
	info.lpSource = (char *)source;
	info.lpDest = (char *)dest;

	General_sosCODECDecompressData(&info, sizeof(dest));

	Report("General stereo16: known-answer samples", Same_Bytes(dest, expected, sizeof(expected)));
	Check("General stereo16: left dwPredicted", info.dwPredicted, 18);
	Check("General stereo16: left wIndex", info.wIndex, 1);
	Check("General stereo16: left wStep", info.wStep, 8);
	Check("General stereo16: left dwSampleIndex", (long)info.dwSampleIndex, 4);
	Check("General stereo16: right dwPredicted2", info.dwPredicted2, 240);
	Check("General stereo16: right wIndex2", info.wIndex2, 32);
	Check("General stereo16: right wStep2", info.wStep2, 157);
	Check("General stereo16: right dwSampleIndex2", (long)info.dwSampleIndex2, 4);
}


void Test_General_Stereo8_Known_Answer(void)
{
	/*
	** Left takes the 0x77 bytes and right the 0xFF bytes, so the two channels run the
	** same magnitudes with opposite signs. The negative side exercises the arithmetic
	** shift used to pick the high byte out of a negative predictor.
	*/
	unsigned char const source[] = {0x77, 0xFF, 0x77, 0xFF, 0x77, 0xFF, 0x77, 0xFF};
	unsigned char expected[] = {
		0x80, 0x7F, 0x80, 0x7F, 0x80, 0x7F, 0x80, 0x7F,
		0x82, 0x7D, 0x84, 0x7B, 0x89, 0x76, 0x95, 0x6A
	};
	unsigned char dest[16];
	std::memset(dest, 0, sizeof(dest));

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 8;
	info.wChannels = 2;
	General_sosCODECInitStream(&info);
	info.lpSource = (char *)source;
	info.lpDest = (char *)dest;

	General_sosCODECDecompressData(&info, sizeof(dest));

	Report("General stereo8: known-answer samples", Same_Bytes(dest, expected, sizeof(expected)));
	Check("General stereo8: left dwPredicted", info.dwPredicted, 5431);
	Check("General stereo8: right dwPredicted2", info.dwPredicted2, -5431);
}


// A deterministic pseudo-random token stream. The decoder accepts any nybble pattern.
std::vector<unsigned char> Make_Token_Stream(std::size_t size, unsigned int seed)
{
	std::vector<unsigned char> stream(size);
	unsigned int state = seed;

	for (std::size_t i = 0; i < size; i++) {
		state = state * 1664525u + 1013904223u;
		stream[i] = (unsigned char)(state >> 24);
	}

	return stream;
}


void Test_Mono16_Chunk_Continuity(void)
{
	std::vector<unsigned char> source = Make_Token_Stream(4096, 12345u);
	std::size_t const samples = source.size() * 2;

	std::vector<short> whole(samples, 0);
	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	Decode_Mono16(info, source.data(), whole.data(), (unsigned long)(samples * 2));

	/*
	** The same stream cut into chunks must land on the same samples and the same stream
	** state. A chunk always resumes at the low nybble of a source byte, so its sample
	** count has to be even; the sizes are otherwise chosen to straddle the boundaries of
	** the eight-sample block the decoder works in.
	*/
	std::size_t const chunk_samples[] = {2, 4, 6, 10, 14, 16, 18, 30, 32, 34, 62, 64, 100};

	bool ok = true;
	std::size_t offset = 0;
	std::size_t chunk = 0;

	std::vector<short> pieces(samples, 0);
	_SOS_COMPRESS_INFO chunked;
	std::memset(&chunked, 0, sizeof(chunked));
	chunked.wBitSize = 16;
	chunked.wChannels = 1;
	sosCODECInitStream(&chunked);

	while (offset < samples) {
		std::size_t take = chunk_samples[chunk % (sizeof(chunk_samples) / sizeof(chunk_samples[0]))];
		chunk++;
		if (offset + take > samples) {
			take = samples - offset;
		}

		Decode_Mono16(chunked, source.data() + offset / 2, pieces.data() + offset, (unsigned long)(take * 2));
		offset += take;
	}

	if (!Same_Bytes(whole.data(), pieces.data(), samples * sizeof(short))) {
		ok = false;
	}
	if (info.dwPredicted != chunked.dwPredicted || info.wIndex != chunked.wIndex) {
		ok = false;
	}

	Report("sosCODEC mono16: chunked decode matches one call", ok);
}


void Test_General_Mono16_Chunk_Continuity(void)
{
	std::vector<unsigned char> source = Make_Token_Stream(4096, 987u);
	std::size_t const samples = source.size() * 2;

	std::vector<short> whole(samples, 0);
	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	General_sosCODECInitStream(&info);
	info.lpSource = (char *)source.data();
	info.lpDest = (char *)whole.data();
	General_sosCODECDecompressData(&info, (unsigned long)(samples * 2));

	/*
	** This decoder restarts its nybble phase on every call, so a chunk boundary is only
	** valid on an even sample. Chunk sizes are even for that reason.
	*/
	std::size_t const chunk_samples[] = {2, 4, 6, 8, 16, 18, 30, 64, 100};

	std::vector<short> pieces(samples, 0);
	_SOS_COMPRESS_INFO chunked;
	std::memset(&chunked, 0, sizeof(chunked));
	chunked.wBitSize = 16;
	chunked.wChannels = 1;
	General_sosCODECInitStream(&chunked);

	std::size_t offset = 0;
	std::size_t chunk = 0;

	while (offset < samples) {
		std::size_t take = chunk_samples[chunk % (sizeof(chunk_samples) / sizeof(chunk_samples[0]))];
		chunk++;
		if (offset + take > samples) {
			take = samples - offset;
		}

		chunked.lpSource = (char *)(source.data() + offset / 2);
		chunked.lpDest = (char *)(pieces.data() + offset);
		General_sosCODECDecompressData(&chunked, (unsigned long)(take * 2));
		offset += take;
	}

	bool ok = Same_Bytes(whole.data(), pieces.data(), samples * sizeof(short));
	if (info.dwPredicted != chunked.dwPredicted || info.wIndex != chunked.wIndex || info.wStep != chunked.wStep) {
		ok = false;
	}

	Report("General mono16: chunked decode matches one call", ok);
}


void Test_Decoders_Agree(void)
{
	/*
	** The two decoders implement the same 16-bit mono format by different means, so any
	** token stream must produce identical output from both.
	*/
	std::vector<unsigned char> source = Make_Token_Stream(8192, 555u);
	std::size_t const samples = source.size() * 2;

	std::vector<short> fast(samples, 0);
	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	Decode_Mono16(info, source.data(), fast.data(), (unsigned long)(samples * 2));

	std::vector<short> general(samples, 0);
	_SOS_COMPRESS_INFO ginfo;
	std::memset(&ginfo, 0, sizeof(ginfo));
	ginfo.wBitSize = 16;
	ginfo.wChannels = 1;
	General_sosCODECInitStream(&ginfo);
	ginfo.lpSource = (char *)source.data();
	ginfo.lpDest = (char *)general.data();
	General_sosCODECDecompressData(&ginfo, (unsigned long)(samples * 2));

	bool ok = Same_Bytes(fast.data(), general.data(), samples * sizeof(short));
	if (info.dwPredicted != ginfo.dwPredicted) {
		ok = false;
	}
	if (info.wIndex != (short)(ginfo.wIndex * 32)) {
		ok = false;
	}

	Report("both decoders agree on 16-bit mono", ok);
}


/*
** A reference encoder, used only to produce a stream whose decoded output can be compared
** against the signal it came from. It picks, for each sample, the token whose decoded
** result lands closest to the target.
*/
std::vector<unsigned char> Encode_Mono16(short const * samples, std::size_t count)
{
	std::vector<unsigned char> stream((count + 1) / 2, 0);
	int predicted = 0;
	int index = 0;

	for (std::size_t i = 0; i < count; i++) {
		int best_token = 0;
		int best_error = -1;
		int best_value = 0;

		for (int token = 0; token < 16; token++) {
			int value = Clamp(predicted + Reference_Difference(StepTable[index], token));
			int error = value - samples[i];
			if (error < 0) {
				error = -error;
			}
			if (best_error < 0 || error < best_error) {
				best_error = error;
				best_token = token;
				best_value = value;
			}
		}

		if ((i & 1) != 0) {
			stream[i / 2] = (unsigned char)(stream[i / 2] | (best_token << 4));
		} else {
			stream[i / 2] = (unsigned char)(best_token & 0x0F);
		}

		predicted = best_value;
		index = Reference_Next_Index(index, best_token);
	}

	return stream;
}


void Test_Round_Trip(void)
{
	/*
	** A 1 kHz tone at 22050 Hz is the kind of signal this codec was built for. ADPCM is
	** lossy, so the check is on the error being small relative to the amplitude rather
	** than on an exact match.
	*/
	std::size_t const count = 8192;
	double const amplitude = 20000.0;

	std::vector<short> original(count);
	for (std::size_t i = 0; i < count; i++) {
		original[i] = (short)(amplitude * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * (double)i / 22050.0));
	}

	std::vector<unsigned char> stream = Encode_Mono16(original.data(), count);

	std::vector<short> decoded(count, 0);
	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	Decode_Mono16(info, stream.data(), decoded.data(), (unsigned long)(count * 2));

	/*
	** The first few samples are the predictor climbing from zero, so they are excluded
	** from the error measurement.
	*/
	std::size_t const settle = 64;
	double signal = 0.0;
	double noise = 0.0;
	int worst = 0;

	for (std::size_t i = settle; i < count; i++) {
		double error = (double)decoded[i] - (double)original[i];
		signal += (double)original[i] * (double)original[i];
		noise += error * error;

		int magnitude = (int)(decoded[i] - original[i]);
		if (magnitude < 0) {
			magnitude = -magnitude;
		}
		if (magnitude > worst) {
			worst = magnitude;
		}
	}

	double snr = 10.0 * std::log10(signal / (noise > 0.0 ? noise : 1.0));
	std::printf("  round trip: SNR %.1f dB, worst sample error %d\n", snr, worst);

	Report("round trip: SNR above 25 dB", snr > 25.0);
	Report("round trip: worst error under 8% of amplitude", worst < (int)(amplitude * 0.08));

	// Re-encoding the decoded signal must reproduce the same stream: the codec is a
	// deterministic function of the predictor state.
	std::vector<unsigned char> again = Encode_Mono16(decoded.data(), count);
	std::vector<short> twice(count, 0);
	_SOS_COMPRESS_INFO second;
	std::memset(&second, 0, sizeof(second));
	second.wBitSize = 16;
	second.wChannels = 1;
	sosCODECInitStream(&second);
	Decode_Mono16(second, again.data(), twice.data(), (unsigned long)(count * 2));

	Report("round trip: re-encoded stream decodes exactly",
		Same_Bytes(decoded.data(), twice.data(), count * sizeof(short)));
}


void Test_Zero_Length(void)
{
	unsigned char const source[] = {0x77};
	short dest[2] = {1234, 5678};

	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0, sizeof(info));
	info.wBitSize = 16;
	info.wChannels = 1;
	sosCODECInitStream(&info);
	Decode_Mono16(info, source, dest, 0);

	bool ok = (dest[0] == 1234 && dest[1] == 5678 && info.dwPredicted == 0 && info.wIndex == 0);

	_SOS_COMPRESS_INFO general;
	std::memset(&general, 0, sizeof(general));
	general.wBitSize = 16;
	general.wChannels = 1;
	General_sosCODECInitStream(&general);
	general.lpSource = (char *)source;
	general.lpDest = (char *)dest;
	General_sosCODECDecompressData(&general, 0);

	if (dest[0] != 1234 || dest[1] != 5678 || general.dwPredicted != 0) {
		ok = false;
	}

	Report("zero-length request decodes nothing", ok);
}


void Test_Init_Stream(void)
{
	_SOS_COMPRESS_INFO info;
	std::memset(&info, 0xAA, sizeof(info));
	sosCODECInitStream(&info);

	bool ok = (info.wIndex == 0 && info.dwPredicted == 0 && info.wIndex2 == 0 && info.dwPredicted2 == 0);
	Report("sosCODECInitStream clears index and predictor", ok);

	std::memset(&info, 0xAA, sizeof(info));
	General_sosCODECInitStream(&info);

	ok = (info.wIndex == 0 && info.wStep == 7 && info.dwPredicted == 0 && info.dwSampleIndex == 0
		&& info.wIndex2 == 0 && info.wStep2 == 7 && info.dwPredicted2 == 0 && info.dwSampleIndex2 == 0);
	Report("General_sosCODECInitStream seeds step 7", ok);
}

}  // namespace


int main(void)
{
	Test_Tables();
	Test_Init_Stream();
	Test_Mono16_Known_Answer();
	Test_Mono16_Return_Value();
	Test_Mono16_Saturation();
	Test_General_Mono16_Known_Answer();
	Test_General_Mono8_Known_Answer();
	Test_General_Stereo16_Known_Answer();
	Test_General_Stereo8_Known_Answer();
	Test_Mono16_Chunk_Continuity();
	Test_General_Mono16_Chunk_Continuity();
	Test_Decoders_Agree();
	Test_Round_Trip();
	Test_Zero_Length();

	if (Failures != 0) {
		std::printf("\n%d check(s) failed\n", Failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
