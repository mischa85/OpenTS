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
// The reader is also given a container this writer would never have produced, assembled here
// byte by byte, so that what it follows is the geometry a file records rather than the one it
// would have written. That is the only check of the reader against a foreign layout that runs
// on every target.
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


static std::vector<unsigned char> Read_Whole_File(char const * path)
{
	std::vector<unsigned char> data;

	HANDLE const file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return(data);

	DWORD const size = GetFileSize(file, NULL);

	if (size != INVALID_FILE_SIZE && size > 0) {
		data.resize(size);

		DWORD got = 0;
		if (!ReadFile(file, data.data(), size, &got, NULL) || got != size) data.clear();
	}

	CloseHandle(file);

	return(data);
}


static bool Write_Whole_File(char const * path, std::vector<unsigned char> const & data)
{
	HANDLE const file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return(false);

	DWORD written = 0;
	bool const ok = (WriteFile(file, data.data(), (DWORD)data.size(), &written, NULL) != FALSE)
		&& written == data.size();

	CloseHandle(file);

	return(ok);
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


/*
** savever.cpp creates each of its streams without STGM_CREATE, so the default disposition has
** to be the one that refuses a name already there rather than the one that empties it. A
** sub-storage is refused outright, which is what docfile.h says it does instead of half
** supporting one.
*/
static void Test_Storage_Rules(void)
{
	std::string const path = Scratch_Path("rules.sav");
	IStorage * storage = nullptr;

	if (FAILED(DocFile_Create(path.c_str(), STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, &storage))) {
		Check("rules: create", false);
		return;
	}

	IStream * stream = nullptr;
	Check_Result("rules: a new name is created without STGM_CREATE",
		storage->CreateStream(L"Version", STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, 0, &stream), S_OK);
	if (stream != nullptr) stream->Release();

	stream = nullptr;
	Check_Result("rules: a name already there is refused without STGM_CREATE",
		storage->CreateStream(L"Version", STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, 0, &stream),
		STG_E_FILEALREADYEXISTS);
	Check("rules: the refusal hands back nothing", stream == nullptr);

	IStorage * nested = nullptr;
	Check_Result("rules: a sub-storage is refused",
		storage->CreateStorage(L"Nested", STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, 0, 0, &nested),
		STG_E_UNIMPLEMENTEDFUNCTION);
	Check("rules: the refused sub-storage hands back nothing", nested == nullptr);

	storage->Release();
}


/*
** Get_Savefile_Info opens every save the load dialog lists for writing and only reads it, so a
** storage that was opened and not changed has to leave the file exactly as it found it.
*/
static void Test_Untouched_Update(void)
{
	std::string const path = Scratch_Path("untouched.sav");
	IStorage * storage = nullptr;

	if (FAILED(DocFile_Create(path.c_str(), STGM_CREATE | STGM_SHARE_EXCLUSIVE | STGM_READWRITE, &storage))) {
		Check("untouched: create", false);
		return;
	}

	Check("untouched: write", Write_Samples(storage));
	storage->Release();

	std::vector<unsigned char> const before = Read_Whole_File(path.c_str());
	Check("untouched: the file is there to compare against", !before.empty());

	storage = nullptr;
	if (FAILED(DocFile_Open(path.c_str(), STGM_SHARE_EXCLUSIVE | STGM_READWRITE, &storage))) {
		Check("untouched: reopened for writing", false);
		return;
	}

	Check_Samples("untouched", storage);
	storage->Release();

	Check("untouched: reading through a writable storage rewrites nothing",
		Read_Whole_File(path.c_str()) == before);
}


static void Put_U16_At(unsigned char * into, unsigned int value)
{
	into[0] = (unsigned char)(value & 0xFF);
	into[1] = (unsigned char)((value >> 8) & 0xFF);
}


static void Put_U32_At(unsigned char * into, unsigned int value)
{
	into[0] = (unsigned char)(value & 0xFF);
	into[1] = (unsigned char)((value >> 8) & 0xFF);
	into[2] = (unsigned char)((value >> 16) & 0xFF);
	into[3] = (unsigned char)((value >> 24) & 0xFF);
}


/*
** A directory entry carries its name as UTF-16 followed by a terminator, and the byte count
** at 0x40 counts the terminator with it.
*/
static void Put_Entry_Name(unsigned char * entry, OLECHAR const * name)
{
	unsigned int length = 0;

	while (name[length] != 0) {
		Put_U16_At(entry + length * 2, (unsigned int)(unsigned short)name[length]);
		length++;
	}

	Put_U16_At(entry + 0x40, (length + 1) * 2);
}


/*
** A container no writer here would produce, assembled byte by byte: the sectors of its largest
** stream run backwards, its directory is two sectors that do not adjoin, and a sub-storage
** holds a stream of its own. The reader has to follow what the file records, take the three
** streams the root holds, and leave the sub-storage's out of them.
*/
static void Test_Foreign_Container(void)
{
	unsigned int const SECTOR = 512;
	unsigned int const FREE = 0xFFFFFFFFu;
	unsigned int const ENDCHAIN = 0xFFFFFFFEu;
	unsigned int const FATSEC = 0xFFFFFFFDu;
	unsigned int const NONE = 0xFFFFFFFFu;
	unsigned int const SECTORS = 13;

	std::vector<unsigned char> image((std::size_t)(SECTORS + 1) * SECTOR, 0);
	unsigned char * const header = image.data();
	unsigned char * const body = image.data() + SECTOR;

	auto sector = [body](unsigned int which) -> unsigned char * {
		return(body + (std::size_t)which * SECTOR);
	};

	/*
	** Entries 0 to 3 live in sector 11 and entries 4 to 7 in sector 10, which the directory
	** chain visits in that order.
	*/
	auto entry = [&sector](unsigned int index) -> unsigned char * {
		return(sector((index < 4) ? 11u : 10u) + (std::size_t)(index % 4) * 128);
	};

	/*
	** A stream exactly at the cutoff, so it takes whole sectors, laid down in reverse.
	*/
	std::vector<unsigned char> const deep = Pattern(4096, 11);
	std::vector<unsigned char> const tiny = Pattern(100, 12);
	std::vector<unsigned char> const buried = Pattern(50, 13);

	for (unsigned int block = 0; block < 8; block++) {
		memcpy(sector(7 - block), &deep[(std::size_t)block * SECTOR], SECTOR);
	}

	/*
	** The mini stream, which the root owns: two mini sectors of the small stream and then one
	** belonging to the sub-storage.
	*/
	memcpy(sector(8), tiny.data(), tiny.size());
	memcpy(sector(8) + 128, buried.data(), buried.size());

	for (unsigned int slot = 0; slot < 128; slot++) Put_U32_At(sector(9) + slot * 4, FREE);
	Put_U32_At(sector(9) + 0 * 4, 1);
	Put_U32_At(sector(9) + 1 * 4, ENDCHAIN);
	Put_U32_At(sector(9) + 2 * 4, ENDCHAIN);

	for (unsigned int index = 0; index < 8; index++) {
		unsigned char * const at = entry(index);

		at[0x42] = 0;
		at[0x43] = 1;
		Put_U32_At(at + 0x44, NONE);
		Put_U32_At(at + 0x48, NONE);
		Put_U32_At(at + 0x4C, NONE);
		Put_U32_At(at + 0x74, ENDCHAIN);
	}

	Put_Entry_Name(entry(0), L"Root Entry");
	entry(0)[0x42] = 5;
	Put_U32_At(entry(0) + 0x4C, 2);
	Put_U32_At(entry(0) + 0x74, 8);
	Put_U32_At(entry(0) + 0x78, 192);

	Put_Entry_Name(entry(1), L"Deep");
	entry(1)[0x42] = 2;
	Put_U32_At(entry(1) + 0x74, 7);
	Put_U32_At(entry(1) + 0x78, (unsigned int)deep.size());

	Put_Entry_Name(entry(2), L"Tiny");
	entry(2)[0x42] = 2;
	Put_U32_At(entry(2) + 0x44, 1);
	Put_U32_At(entry(2) + 0x48, 3);
	Put_U32_At(entry(2) + 0x74, 0);
	Put_U32_At(entry(2) + 0x78, (unsigned int)tiny.size());

	Put_Entry_Name(entry(3), L"Zero");
	entry(3)[0x42] = 2;
	Put_U32_At(entry(3) + 0x48, 4);
	Put_U32_At(entry(3) + 0x78, 0);

	Put_Entry_Name(entry(4), L"Nested");
	entry(4)[0x42] = 1;
	Put_U32_At(entry(4) + 0x4C, 5);

	Put_Entry_Name(entry(5), L"Buried");
	entry(5)[0x42] = 2;
	Put_U32_At(entry(5) + 0x74, 2);
	Put_U32_At(entry(5) + 0x78, (unsigned int)buried.size());

	for (unsigned int slot = 0; slot < 128; slot++) Put_U32_At(sector(12) + slot * 4, FREE);
	for (unsigned int which = 7; which > 0; which--) Put_U32_At(sector(12) + which * 4, which - 1);
	Put_U32_At(sector(12) + 0 * 4, ENDCHAIN);
	Put_U32_At(sector(12) + 8 * 4, ENDCHAIN);
	Put_U32_At(sector(12) + 9 * 4, ENDCHAIN);
	Put_U32_At(sector(12) + 10 * 4, ENDCHAIN);
	Put_U32_At(sector(12) + 11 * 4, 10);
	Put_U32_At(sector(12) + 12 * 4, FATSEC);

	unsigned char const signature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

	memcpy(header, signature, sizeof(signature));
	Put_U16_At(header + 0x18, 0x003E);
	Put_U16_At(header + 0x1A, 3);
	Put_U16_At(header + 0x1C, 0xFFFE);
	Put_U16_At(header + 0x1E, 9);
	Put_U16_At(header + 0x20, 6);
	Put_U32_At(header + 0x2C, 1);
	Put_U32_At(header + 0x30, 11);
	Put_U32_At(header + 0x38, 4096);
	Put_U32_At(header + 0x3C, 9);
	Put_U32_At(header + 0x40, 1);
	Put_U32_At(header + 0x44, ENDCHAIN);
	Put_U32_At(header + 0x48, 0);

	for (unsigned int slot = 0; slot < 109; slot++) {
		Put_U32_At(header + 0x4C + slot * 4, (slot == 0) ? 12u : FREE);
	}

	std::string const path = Scratch_Path("foreign.sav");

	if (!Write_Whole_File(path.c_str(), image)) {
		Check("foreign: the container was written", false);
		return;
	}

	Check_Result("foreign: it is recognized as a storage", DocFile_Is_Storage_File(path.c_str()), S_OK);

	IStorage * storage = nullptr;
	Check_Result("foreign: it opens", DocFile_Open(path.c_str(), STGM_SHARE_DENY_WRITE, &storage), S_OK);
	if (storage == nullptr) return;

	struct ExpectedType {
		OLECHAR const * Name;
		std::vector<unsigned char> const * Data;
	};

	ExpectedType const expected[] = { { L"Deep", &deep }, { L"Tiny", &tiny } };

	for (ExpectedType const & wanted : expected) {
		IStream * stream = nullptr;

		if (FAILED(storage->OpenStream(wanted.Name, NULL, STGM_SHARE_EXCLUSIVE, 0, &stream))) {
			Check("foreign: a root stream is readable", false);
			continue;
		}

		std::vector<unsigned char> got(wanted.Data->size() + 1, 0);
		ULONG read = 0;

		stream->Read(got.data(), (ULONG)got.size(), &read);
		stream->Release();

		Check("foreign: a root stream reads back whole",
			read == wanted.Data->size() && memcmp(got.data(), wanted.Data->data(), wanted.Data->size()) == 0);
	}

	IStream * stream = nullptr;
	Check_Result("foreign: an empty stream is there",
		storage->OpenStream(L"Zero", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream), S_OK);

	if (stream != nullptr) {
		STATSTG statstg;

		memset(&statstg, 0, sizeof(statstg));
		stream->Stat(&statstg, STATFLAG_NONAME);
		Check("foreign: the empty stream is empty", statstg.cbSize.QuadPart == 0);
		stream->Release();
	}

	stream = nullptr;
	Check_Result("foreign: a sub-storage's stream is not one of the root's",
		storage->OpenStream(L"Buried", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream), STG_E_FILENOTFOUND);
	Check("foreign: the sub-storage's stream hands back nothing", stream == nullptr);

	stream = nullptr;
	Check_Result("foreign: the sub-storage itself is not a stream",
		storage->OpenStream(L"Nested", NULL, STGM_SHARE_EXCLUSIVE, 0, &stream), STG_E_FILENOTFOUND);

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
		"roundtrip.sav", "rewrite.sav", "rules.sav", "untouched.sav", "foreign.sav", "plain.dat",
		"version.sav", "ours.sav", "theirs.sav"
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
	Test_Storage_Rules();
	Test_Untouched_Update();
	Test_Foreign_Container();
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
