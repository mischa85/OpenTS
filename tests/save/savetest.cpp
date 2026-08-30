/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the compound file a saved game is written into: docfile.cpp's writer and reader,
// over streams small enough for the mini stream, large enough for sectors of their own, and
// large enough to push the allocation table past what the header can locate.
//
// Every file it touches it creates itself, in a scratch directory named by the first
// argument, so it reads no game data and leaves nothing behind.
//
// On Windows it does the check that matters most and that only Windows can do: it writes a
// file with docfile.cpp and reads it with OLE, and writes one with OLE and reads it with
// docfile.cpp. A save written in a browser is only worth carrying to a desktop if those two
// agree.

#include "docfile.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


static int Failures = 0;
static int Checks = 0;
static std::string Scratch;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;

	Failures++;
	printf("FAIL %s\n", name);
}


static void Check_Result(char const * name, HRESULT actual, HRESULT expected)
{
	Checks++;
	if (actual == expected) return;

	Failures++;
	printf("FAIL %s: got 0x%08lx, expected 0x%08lx\n", name,
		(unsigned long)actual, (unsigned long)expected);
}


static std::string Scratch_Path(char const * name)
{
	return(Scratch + "\\" + name);
}


/*
** The bytes each stream carries. A counted sequence rather than a constant, so that a read
** landing at the wrong offset shows up as wrong data rather than as a match.
*/
static std::vector<unsigned char> Pattern(std::size_t length, unsigned int seed)
{
	std::vector<unsigned char> data(length);
	unsigned int state = seed * 2654435761u + 1u;

	for (std::size_t index = 0; index < length; index++) {
		state = state * 1103515245u + 12345u;
		data[index] = (unsigned char)((state >> 16) & 0xFF);
	}

	return(data);
}


struct SampleType
{
	OLECHAR const * Name;
	std::size_t Length;
	unsigned int Seed;
};

/*
** One stream below the mini stream cutoff, one exactly at it, one spanning several sectors,
** one empty, and one large enough that the allocation table outgrows the 109 entries the
** header holds and has to be located through a table of its own.
*/
static SampleType const Samples[] = {
	{ L"Scenario Description", 33, 1 },
	{ L"Version", 4, 2 },
	{ L"Edge", 4096, 3 },
	{ L"Below Edge", 4095, 4 },
	{ L"Empty", 0, 5 },
	{ L"CONTENTS", 7500000, 6 },
};


static bool Write_Samples(IStorage * storage)
{
	for (SampleType const & sample : Samples) {
		IStream * stream = nullptr;

		if (FAILED(storage->CreateStream(sample.Name, STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE,
				0, 0, &stream))) {
			return(false);
		}

		std::vector<unsigned char> const data = Pattern(sample.Length, sample.Seed);

		if (!data.empty()) {
			ULONG written = 0;

			if (FAILED(stream->Write(data.data(), (ULONG)data.size(), &written)) || written != data.size()) {
				stream->Release();
				return(false);
			}
		}

		stream->Release();
	}

	return(SUCCEEDED(storage->Commit(0)));
}


static void Check_Samples(char const * label, IStorage * storage)
{
	for (SampleType const & sample : Samples) {
		IStream * stream = nullptr;
		std::string const what = std::string(label) + ": stream is readable";

		if (FAILED(storage->OpenStream(sample.Name, NULL, STGM_SHARE_EXCLUSIVE, 0, &stream))) {
			Check(what.c_str(), false);
			continue;
		}

		std::vector<unsigned char> const expected = Pattern(sample.Length, sample.Seed);
		std::vector<unsigned char> got(sample.Length + 1, 0);
		ULONG read = 0;

		stream->Read(got.data(), (ULONG)got.size(), &read);
		stream->Release();

		std::string const content = std::string(label) + ": stream content";
		Check(content.c_str(), read == sample.Length
			&& (sample.Length == 0 || memcmp(got.data(), expected.data(), sample.Length) == 0));
	}
}


/*
** The written container has to be a compound file by inspection as well as by round trip: a
** reader that is not this one decides from the header alone which geometry to read it with.
*/
static void Check_Header(char const * path)
{
	HANDLE const file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		Check("header: the file is there", false);
		return;
	}

	unsigned char header[512];
	DWORD read = 0;
	ReadFile(file, header, sizeof(header), &read, NULL);
	CloseHandle(file);

	if (read != sizeof(header)) {
		Check("header: a whole header was written", false);
		return;
	}

	unsigned char const signature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

	Check("header: signature", memcmp(header, signature, sizeof(signature)) == 0);
	Check("header: little endian mark", header[0x1C] == 0xFE && header[0x1D] == 0xFF);
	Check("header: major version 3", header[0x1A] == 3 && header[0x1B] == 0);
	Check("header: 512 byte sectors", header[0x1E] == 9 && header[0x1F] == 0);
	Check("header: 64 byte mini sectors", header[0x20] == 6 && header[0x21] == 0);
	Check("header: mini stream cutoff of 4096",
		header[0x38] == 0x00 && header[0x39] == 0x10 && header[0x3A] == 0 && header[0x3B] == 0);
}


static void Test_Round_Trip(void)
{
	std::string const path = Scratch_Path("roundtrip.sav");
	IStorage * storage = nullptr;

	Check_Result("create", DocFile_Create(path.c_str(), STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE,
		&storage), S_OK);
	if (storage == nullptr) return;

	Check("write", Write_Samples(storage));
	storage->Release();

	Check_Header(path.c_str());
	Check_Result("is a storage file", DocFile_Is_Storage_File(path.c_str()), S_OK);

	storage = nullptr;
	Check_Result("open", DocFile_Open(path.c_str(), STGM_SHARE_DENY_WRITE, &storage), S_OK);
	if (storage == nullptr) return;

	Check_Samples("round trip", storage);

	/*
	** A storage looks up a name the way Windows does, without regard to case, and answers for
	** one it does not hold rather than handing back an empty stream.
	*/
	IStream * stream = nullptr;
	Check_Result("case insensitive lookup",
		storage->OpenStream(L"contents", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream), S_OK);
	if (stream != nullptr) stream->Release();

	stream = nullptr;
	Check_Result("missing stream",
		storage->OpenStream(L"Not There", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream), STG_E_FILENOTFOUND);
	Check("missing stream hands back nothing", stream == nullptr);

	storage->Release();
}


static void Test_Rewrite(void)
{
	std::string const path = Scratch_Path("rewrite.sav");
	IStorage * storage = nullptr;

	if (FAILED(DocFile_Create(path.c_str(), STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, &storage))) {
		Check("rewrite: create", false);
		return;
	}

	IStream * stream = nullptr;
	storage->CreateStream(L"CONTENTS", STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, 0, &stream);

	std::vector<unsigned char> const first = Pattern(200000, 7);
	stream->Write(first.data(), (ULONG)first.size(), NULL);

	/*
	** saveload.cpp releases the stream before it commits the storage, so the bytes have to
	** belong to the storage rather than to the stream that wrote them.
	*/
	stream->Release();
	Check_Result("rewrite: commit", storage->Commit(0), S_OK);
	storage->Release();

	storage = nullptr;
	if (FAILED(DocFile_Open(path.c_str(), STGM_SHARE_DENY_WRITE, &storage))) {
		Check("rewrite: reopen", false);
		return;
	}

	stream = nullptr;
	storage->OpenStream(L"CONTENTS", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream);

	std::vector<unsigned char> got(first.size(), 0);
	ULONG read = 0;
	stream->Read(got.data(), (ULONG)got.size(), &read);

	Check("rewrite: content survives releasing the stream first",
		read == first.size() && memcmp(got.data(), first.data(), first.size()) == 0);

	stream->Release();
	storage->Release();
}


static void Test_Not_A_Storage(void)
{
	std::string const path = Scratch_Path("plain.dat");

	HANDLE const file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		Check("plain file: created", false);
		return;
	}

	DWORD written = 0;
	WriteFile(file, "not a compound file at all", 26, &written, NULL);
	CloseHandle(file);

	Check_Result("plain file is not a storage", DocFile_Is_Storage_File(path.c_str()), S_FALSE);

	IStorage * storage = nullptr;
	Check_Result("plain file refuses to open as a storage",
		DocFile_Open(path.c_str(), STGM_SHARE_DENY_WRITE, &storage), STG_E_INVALIDHEADER);
	Check("plain file hands back nothing", storage == nullptr);

	Check_Result("a name with no file behind it",
		DocFile_Is_Storage_File(Scratch_Path("nothing.dat").c_str()), STG_E_FILENOTFOUND);
}


/*
** Reads a stream the way savever.cpp reads a version string: a character at a time until the
** terminator, stopping at the buffer's end and at the stream's, so that neither a stream
** without a terminator nor one longer than the buffer runs off it.
*/
static int Read_Wide_Stream(IStream * stream, OLECHAR * buffer, int room)
{
	int index = 0;

	while (index < room - 1) {
		ULONG got = 0;

		HRESULT const result = stream->Read(&buffer[index], (ULONG)sizeof(buffer[index]), &got);
		if (FAILED(result) || got != sizeof(buffer[index]) || buffer[index] == 0) break;
		index++;
	}

	buffer[index] = 0;
	return(index);
}


/*
** The version information a save carries is one stream per value, and a string value is the
** UTF-16 text with its terminator after it. savever.cpp writes those streams and reads them
** back a character at a time, so what is pinned here is what that depends on: the length a
** terminated string occupies, and what the reader is told when it asks for a character the
** stream does not have.
**
** The length is the half that has actually been wrong. A wide length measured after the text
** was converted is measured in characters of the library's width rather than the build's, and
** a stream written from that length carries the wrong text and no terminator at all. Such a
** stream is here as well, because saves full of them exist.
*/
static void Test_Version_Strings(void)
{
	std::string const path = Scratch_Path("version.sav");
	OLECHAR const text[] = L"Reinforce Phoenix Base";
	int const characters = (int)(sizeof(text) / sizeof(text[0])) - 1;

	IStorage * storage = nullptr;

	if (FAILED(DocFile_Create(path.c_str(), STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, &storage))) {
		Check("version strings: created", false);
		return;
	}

	IStream * stream = nullptr;

	if (SUCCEEDED(storage->CreateStream(L"Scenario Description",
			STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, 0, &stream))) {
		ULONG written = 0;

		stream->Write(text, (ULONG)sizeof(text), &written);
		stream->Commit(0);
		stream->Release();

		Check("version strings: the terminator is written too", written == sizeof(text));
	} else {
		Check("version strings: the stream is creatable", false);
	}

	stream = nullptr;
	if (SUCCEEDED(storage->CreateStream(L"Player House",
			STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, 0, &stream))) {
		stream->Write(text, (ULONG)(characters * sizeof(text[0])), NULL);
		stream->Commit(0);
		stream->Release();
	} else {
		Check("version strings: the unterminated stream is creatable", false);
	}

	storage->Commit(0);
	storage->Release();

	storage = nullptr;
	if (FAILED(DocFile_Open(path.c_str(), STGM_SHARE_DENY_WRITE, &storage))) {
		Check("version strings: reopened", false);
		return;
	}

	stream = nullptr;
	if (SUCCEEDED(storage->OpenStream(L"Scenario Description", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream))) {
		STATSTG statstg;

		memset(&statstg, 0, sizeof(statstg));
		stream->Stat(&statstg, STATFLAG_NONAME);
		Check("version strings: the stream is the text and its terminator",
			statstg.cbSize.QuadPart == (ULONGLONG)sizeof(text));

		OLECHAR buffer[128];
		int const length = Read_Wide_Stream(stream, buffer, (int)(sizeof(buffer) / sizeof(buffer[0])));

		Check("version strings: the terminator ends the text", length == characters);
		Check("version strings: the text came back",
			memcmp(buffer, text, (std::size_t)characters * sizeof(text[0])) == 0);

		/*
		** Asking for what is not there is a short read rather than a failure, which is what
		** stops the reader on a stream that carries no terminator.
		*/
		OLECHAR past = 0x4141;
		ULONG got = 0;

		HRESULT const result = stream->Read(&past, (ULONG)sizeof(past), &got);
		Check("version strings: a read past the end does not fail", SUCCEEDED(result));
		Check("version strings: a read past the end reports nothing read", got == 0);

		stream->Release();
	} else {
		Check("version strings: the stream is readable", false);
	}

	stream = nullptr;
	if (SUCCEEDED(storage->OpenStream(L"Player House", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream))) {
		OLECHAR buffer[128];
		int const length = Read_Wide_Stream(stream, buffer, (int)(sizeof(buffer) / sizeof(buffer[0])));

		Check("version strings: an unterminated stream ends at its last character",
			length == characters);
		Check("version strings: an unterminated stream still reads back",
			memcmp(buffer, text, (std::size_t)characters * sizeof(text[0])) == 0);

		stream->Release();
	} else {
		Check("version strings: the unterminated stream is readable", false);
	}

	storage->Release();
}


#if !defined(__EMSCRIPTEN__)
/*
** The interchange check. It is the reason docfile.cpp is built on this target at all: OLE is
** the reader a save written in a browser has to satisfy, and the writer a save loaded in a
** browser has to have been written by.
*/
static void Test_Against_OLE(void)
{
	std::string const ours = Scratch_Path("ours.sav");
	std::string const theirs = Scratch_Path("theirs.sav");

	WCHAR wide[MAX_PATH];
	IStorage * storage = nullptr;

	if (FAILED(DocFile_Create(ours.c_str(), STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, &storage))) {
		Check("interchange: our writer created a file", false);
		return;
	}

	Check("interchange: our writer wrote the samples", Write_Samples(storage));
	storage->Release();

	MultiByteToWideChar(CP_ACP, 0, ours.c_str(), -1, wide, MAX_PATH);
	Check_Result("interchange: OLE recognizes our file", StgIsStorageFile(wide), S_OK);

	storage = nullptr;
	if (SUCCEEDED(StgOpenStorage(wide, NULL, STGM_SHARE_DENY_WRITE, NULL, 0, &storage))) {
		Check_Samples("interchange: OLE reads our file", storage);
		storage->Release();
	} else {
		Check("interchange: OLE opens our file", false);
	}

	MultiByteToWideChar(CP_ACP, 0, theirs.c_str(), -1, wide, MAX_PATH);
	storage = nullptr;

	if (FAILED(StgCreateDocfile(wide, STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, &storage))) {
		Check("interchange: OLE created a file", false);
		return;
	}

	Check("interchange: OLE wrote the samples", Write_Samples(storage));
	storage->Release();

	Check_Result("interchange: we recognize OLE's file", DocFile_Is_Storage_File(theirs.c_str()), S_OK);

	storage = nullptr;
	if (SUCCEEDED(DocFile_Open(theirs.c_str(), STGM_SHARE_DENY_WRITE, &storage))) {
		Check_Samples("interchange: we read OLE's file", storage);
		storage->Release();
	} else {
		Check("interchange: we open OLE's file", false);
	}
}
#endif


static void Clean_Up(void)
{
	char const * const leftovers[] = {
		"roundtrip.sav", "rewrite.sav", "plain.dat", "version.sav", "ours.sav", "theirs.sav"
	};

	for (char const * name : leftovers) {
		DeleteFileA(Scratch_Path(name).c_str());
	}

	RemoveDirectoryA(Scratch.c_str());
}


int main(int argc, char ** argv)
{
	Scratch = (argc > 1) ? argv[1] : ".";
	Scratch += "\\save-scratch";

	if (!CreateDirectoryA(Scratch.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		printf("FATAL: could not create the scratch directory %s\n", Scratch.c_str());
		return(1);
	}

	Test_Round_Trip();
	Test_Rewrite();
	Test_Not_A_Storage();
	Test_Version_Strings();
#if !defined(__EMSCRIPTEN__)
	CoInitialize(NULL);
	Test_Against_OLE();
	CoUninitialize();
#endif

	Clean_Up();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
