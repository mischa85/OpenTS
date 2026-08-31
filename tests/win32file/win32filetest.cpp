/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the Win32 file API the engine reads its archives through: CreateFileA and its
// creation dispositions, ReadFile, WriteFile, SetFilePointer, GetFileSize, CloseHandle,
// DeleteFileA, the attribute and file-time calls, and the FindFirstFileA family.
//
// Every file it touches it creates itself, in a scratch directory named by the first
// argument, so it reads no game data and leaves nothing behind.
//
// The harness is written against the Win32 API rather than against the WebAssembly
// target's substitute for it, and builds on both. On Windows it establishes what the API
// actually does; on WebAssembly it holds win32compat.cpp to that same account. A check
// that would pass against the substitute but not against Windows is worth nothing here.

#if defined(__EMSCRIPTEN__)
#include "win32compat.h"
#else
#include <windows.h>
#endif

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


static void Check_Equal(char const * name, unsigned long actual, unsigned long expected)
{
	Checks++;
	if (actual == expected) return;

	Failures++;
	printf("FAIL %s: got %lu, expected %lu\n", name, actual, expected);
}


static std::string Scratch_Path(char const * name)
{
	return(Scratch + "\\" + name);
}


/*
** The pattern each written file carries. It is a byte sequence rather than a constant so
** that a read landing at the wrong offset shows up as wrong data rather than as a match.
*/
static std::vector<unsigned char> Pattern(std::size_t length)
{
	std::vector<unsigned char> bytes(length);

	for (std::size_t index = 0; index < length; index++) {
		bytes[index] = (unsigned char)((index * 37 + (index >> 8)) & 0xFF);
	}
	return(bytes);
}


static HANDLE Open_For_Write(char const * name)
{
	return(CreateFileA(Scratch_Path(name).c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}


static HANDLE Open_For_Read(char const * name)
{
	return(CreateFileA(Scratch_Path(name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
}


/*
** Writes a file of the given length and reports whether every step of the write reported
** success. The four calls used here are the ones RawFileClass::Write makes.
*/
static bool Write_File(char const * name, std::size_t length)
{
	HANDLE const file = Open_For_Write(name);

	if (file == INVALID_HANDLE_VALUE) return(false);

	std::vector<unsigned char> const bytes = Pattern(length);
	DWORD written = 0;
	BOOL const wrote = WriteFile(file, bytes.empty() ? "" : (void const *)bytes.data(), (DWORD)length, &written, NULL);

	CloseHandle(file);
	return(wrote != FALSE && written == (DWORD)length);
}


static void Test_Write_Read_Size(void)
{
	std::size_t const length = 5000;

	Check("write a file", Write_File("basic.dat", length));

	HANDLE const file = Open_For_Read("basic.dat");
	Check("open the file just written", file != INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE) return;

	Check_Equal("size of the file just written", GetFileSize(file, NULL), (unsigned long)length);

	/*
	** A high word is asked for separately because the two forms take different paths, and a
	** file this small must report a zero upper half rather than leaving the word untouched.
	*/
	DWORD high = 0xCDCDCDCD;
	Check_Equal("size of the file, low word", GetFileSize(file, &high), (unsigned long)length);
	Check_Equal("size of the file, high word", high, 0);

	std::vector<unsigned char> read(length + 16, 0xCD);
	DWORD got = 0;
	Check("read the whole file", ReadFile(file, read.data(), (DWORD)length, &got, NULL) != FALSE);
	Check_Equal("bytes read", got, (unsigned long)length);
	Check("contents of the file", memcmp(read.data(), Pattern(length).data(), length) == 0);

	/*
	** Win32 reports the end of a file as a successful read of nothing, not as a failure,
	** and RawFileClass::Read relies on that to end its loop.
	*/
	got = 0xCDCDCDCD;
	Check("read past the end succeeds", ReadFile(file, read.data(), 16, &got, NULL) != FALSE);
	Check_Equal("read past the end returns nothing", got, 0);

	CloseHandle(file);
}


static void Test_Seek(void)
{
	std::size_t const length = 5000;

	Check("write the file to seek in", Write_File("seek.dat", length));

	HANDLE const file = Open_For_Read("seek.dat");
	Check("open the file to seek in", file != INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE) return;

	Check_Equal("seek to an absolute position", SetFilePointer(file, 1000, NULL, FILE_BEGIN), 1000);
	Check_Equal("seek forward from there", SetFilePointer(file, 500, NULL, FILE_CURRENT), 1500);
	Check_Equal("seek back from there", SetFilePointer(file, -200, NULL, FILE_CURRENT), 1300);
	Check_Equal("seek to the end", SetFilePointer(file, 0, NULL, FILE_END), (unsigned long)length);
	Check_Equal("seek back from the end", SetFilePointer(file, -100, NULL, FILE_END), (unsigned long)length - 100);

	/*
	** Asking where the pointer is, which is the seek RawFileClass makes most often.
	*/
	Check_Equal("seek by nothing reports the position", SetFilePointer(file, 0, NULL, FILE_CURRENT),
		(unsigned long)length - 100);

	/*
	** The high word is read as the upper half of the distance and written back as the upper
	** half of the position, so it has to be set going in and has to come back cleared here.
	*/
	LONG high = 0;
	Check_Equal("seek with a high word, low half", SetFilePointer(file, 2048, &high, FILE_BEGIN), 2048);
	Check_Equal("seek with a high word, high half", (unsigned long)high, 0);

	/*
	** Reading after a seek must come from the position seeked to.
	*/
	unsigned char read[64];
	DWORD got = 0;
	Check("read after a seek", ReadFile(file, read, sizeof(read), &got, NULL) != FALSE);
	Check_Equal("bytes read after a seek", got, sizeof(read));
	Check("contents after a seek", memcmp(read, Pattern(length).data() + 2048, sizeof(read)) == 0);

	Check_Equal("seek with an unknown origin fails", SetFilePointer(file, 0, NULL, 99),
		(unsigned long)INVALID_SET_FILE_POINTER);

	CloseHandle(file);

	Check_Equal("seek on a closed handle fails", SetFilePointer(file, 0, NULL, FILE_BEGIN),
		(unsigned long)INVALID_SET_FILE_POINTER);
}


static void Test_Creation_Dispositions(void)
{
	Check("write the file to reopen", Write_File("disposition.dat", 100));

	/*
	** OPEN_EXISTING on a name that is not there is the failure RawFileClass::Is_Available
	** reads as "no such file", and it must not create anything.
	*/
	HANDLE missing = CreateFileA(Scratch_Path("absent.dat").c_str(), GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("opening a missing file fails", missing == INVALID_HANDLE_VALUE);
	Check_Equal("opening a missing file reports why", GetLastError(), ERROR_FILE_NOT_FOUND);
	Check_Equal("opening a missing file creates nothing",
		GetFileAttributesA(Scratch_Path("absent.dat").c_str()), INVALID_FILE_ATTRIBUTES);

	/*
	** CREATE_NEW refuses a name already taken. The debug log's writer depends on it to keep
	** two runs started in the same second apart.
	*/
	HANDLE const clash = CreateFileA(Scratch_Path("disposition.dat").c_str(), GENERIC_WRITE,
		FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("creating over an existing file fails", clash == INVALID_HANDLE_VALUE);
	if (clash != INVALID_HANDLE_VALUE) CloseHandle(clash);

	/*
	** OPEN_ALWAYS keeps what is there, and says through the last-error slot that it found
	** something. This is the disposition RawFileClass opens READ|WRITE with.
	*/
	SetLastError(NO_ERROR);
	HANDLE const kept = CreateFileA(Scratch_Path("disposition.dat").c_str(), GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("opening an existing file always succeeds", kept != INVALID_HANDLE_VALUE);
	Check_Equal("opening an existing file reports that it was there", GetLastError(), ERROR_ALREADY_EXISTS);

	if (kept != INVALID_HANDLE_VALUE) {
		Check_Equal("opening an existing file keeps its length", GetFileSize(kept, NULL), 100);

		/*
		** A handle opened for both must serve a write followed by a read of what was
		** written, which is what a file opened READ|WRITE is opened for.
		*/
		Check_Equal("seek before writing", SetFilePointer(kept, 40, NULL, FILE_BEGIN), 40);

		unsigned char const marker[4] = {0xDE, 0xAD, 0xBE, 0xEF};
		DWORD written = 0;
		Check("write through a read-write handle", WriteFile(kept, marker, sizeof(marker), &written, NULL) != FALSE);
		Check_Equal("bytes written through a read-write handle", written, sizeof(marker));

		Check_Equal("seek back to the marker", SetFilePointer(kept, 40, NULL, FILE_BEGIN), 40);

		unsigned char read[4] = {0, 0, 0, 0};
		DWORD got = 0;
		Check("read through a read-write handle", ReadFile(kept, read, sizeof(read), &got, NULL) != FALSE);
		Check_Equal("bytes read through a read-write handle", got, sizeof(read));
		Check("the marker reads back", memcmp(read, marker, sizeof(marker)) == 0);

		Check_Equal("writing inside a file does not lengthen it", GetFileSize(kept, NULL), 100);

		CloseHandle(kept);
	}

	/*
	** OPEN_ALWAYS on a name that is not there creates it and reports no prior file.
	*/
	SetLastError(NO_ERROR);
	HANDLE const fresh = CreateFileA(Scratch_Path("fresh.dat").c_str(), GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("opening a missing file always creates it", fresh != INVALID_HANDLE_VALUE);
	Check("creating a file reports no prior file", GetLastError() != ERROR_ALREADY_EXISTS);
	if (fresh != INVALID_HANDLE_VALUE) {
		Check_Equal("a file just created is empty", GetFileSize(fresh, NULL), 0);
		CloseHandle(fresh);
	}

	/*
	** CREATE_ALWAYS discards what is there. RawFileClass opens WRITE this way.
	*/
	Check("truncate an existing file", Write_File("disposition.dat", 8));
	HANDLE const truncated = Open_For_Read("disposition.dat");
	if (truncated != INVALID_HANDLE_VALUE) {
		Check_Equal("length after truncation", GetFileSize(truncated, NULL), 8);
		CloseHandle(truncated);
	}

	/*
	** A directory is not a file, however tempting the host makes it.
	*/
	HANDLE const folder = CreateFileA(Scratch.c_str(), GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("opening a directory as a file fails", folder == INVALID_HANDLE_VALUE);
	if (folder != INVALID_HANDLE_VALUE) CloseHandle(folder);
}


static void Test_Attributes(void)
{
	Check("write the file to inspect", Write_File("attributes.dat", 64));

	std::string const path = Scratch_Path("attributes.dat");
	DWORD attributes = GetFileAttributesA(path.c_str());

	Check("a file has attributes", attributes != INVALID_FILE_ATTRIBUTES);
	Check("a file is not a directory", (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
	Check("a file is not read-only to begin with", (attributes & FILE_ATTRIBUTE_READONLY) == 0);

	Check("the scratch directory is a directory",
		(GetFileAttributesA(Scratch.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0);

	Check_Equal("a missing file has no attributes",
		GetFileAttributesA(Scratch_Path("nothing-here.dat").c_str()), INVALID_FILE_ATTRIBUTES);

	Check("mark the file read-only", SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
	attributes = GetFileAttributesA(path.c_str());
	Check("the file reads back as read-only", (attributes & FILE_ATTRIBUTE_READONLY) != 0);

	Check("clear the read-only mark", SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE);
	attributes = GetFileAttributesA(path.c_str());
	Check("the file reads back as writable", (attributes & FILE_ATTRIBUTE_READONLY) == 0);
}


/*
** RawFileClass reads and writes a file's time through the DOS packing, so the round trip
** checked here is the one it makes: a DOS date and time in, a DOS date and time out.
*/
static void Test_File_Times(void)
{
	Check("write the file to stamp", Write_File("times.dat", 32));

	HANDLE const file = CreateFileA(Scratch_Path("times.dat").c_str(), GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("open the file to stamp", file != INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE) return;

	WORD const dosdate = (WORD)(((1999 - 1980) << 9) | (7 << 5) | 14);
	WORD const dostime = (WORD)((13 << 11) | (45 << 5) | (30 / 2));

	FILETIME stamp;
	Check("unpack a DOS date and time", DosDateTimeToFileTime(dosdate, dostime, &stamp) != FALSE);
	Check("stamp the file", SetFileTime(file, NULL, &stamp, &stamp) != FALSE);

	FILETIME written;
	Check("read the stamp back", GetFileTime(file, NULL, NULL, &written) != FALSE);

	WORD readdate = 0;
	WORD readtime = 0;
	Check("pack the stamp read back", FileTimeToDosDateTime(&written, &readdate, &readtime) != FALSE);
	Check_Equal("the date survives the round trip", readdate, dosdate);
	Check_Equal("the time survives the round trip", readtime, dostime);

	/*
	** The same route RawFileClass::Get_Date_Time takes.
	*/
	BY_HANDLE_FILE_INFORMATION information;
	Check("query the file by handle", GetFileInformationByHandle(file, &information) != FALSE);
	Check_Equal("the queried length", information.nFileSizeLow, 32);
	Check_Equal("the queried length has no high half", information.nFileSizeHigh, 0);

	readdate = 0;
	readtime = 0;
	Check("pack the queried write time",
		FileTimeToDosDateTime(&information.ftLastWriteTime, &readdate, &readtime) != FALSE);
	Check_Equal("the queried date matches the stamp", readdate, dosdate);
	Check_Equal("the queried time matches the stamp", readtime, dostime);

	CloseHandle(file);
}


static void Test_Wildcard_Scan(void)
{
	Check("write the first file to scan for", Write_File("scan_b.mix", 200));
	Check("write the second file to scan for", Write_File("scan_a.mix", 100));
	Check("write a file the scan must skip", Write_File("other.txt", 10));

	WIN32_FIND_DATAA found;
	HANDLE const search = FindFirstFileA(Scratch_Path("scan_*.mix").c_str(), &found);

	Check("a wildcard scan finds something", search != INVALID_HANDLE_VALUE);
	if (search == INVALID_HANDLE_VALUE) return;

	/*
	** The name handed back is the file's alone, without the directory the pattern named.
	** CDFileClass copies it straight over the pattern it was given and opens the result.
	*/
	std::vector<std::string> names;
	std::vector<unsigned long> sizes;

	do {
		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
		names.push_back(found.cFileName);
		sizes.push_back(found.nFileSizeLow);
	} while (FindNextFileA(search, &found) != FALSE);

	Check_Equal("the scan ends by running out of files", GetLastError(), ERROR_NO_MORE_FILES);
	Check("close the scan", FindClose(search) != FALSE);

	Check_Equal("the scan found both files", (unsigned long)names.size(), 2);
	if (names.size() == 2) {
		/*
		** Sorted, so the file written second comes first. The order decides which of a set
		** of expansion archives overrides which, and it must not depend on the host.
		*/
		Check("the scan is in name order", names[0] == "scan_a.mix" && names[1] == "scan_b.mix");
		Check_Equal("the length of the first match", sizes[0], 100);
		Check_Equal("the length of the second match", sizes[1], 200);
	}

	/*
	** A pattern nothing answers.
	*/
	WIN32_FIND_DATAA nothing;
	Check("a scan with no matches fails",
		FindFirstFileA(Scratch_Path("nomatch_*.mix").c_str(), &nothing) == INVALID_HANDLE_VALUE);

	/*
	** A search without a wildcard names one entry. The map generator asks after its cache
	** directory this way, so a directory has to answer.
	*/
	WIN32_FIND_DATAA single;
	HANDLE const named = FindFirstFileA(Scratch_Path("scan_a.mix").c_str(), &single);
	Check("a scan without a wildcard finds the file named", named != INVALID_HANDLE_VALUE);
	if (named != INVALID_HANDLE_VALUE) {
		Check("the entry found is the file named", strcmp(single.cFileName, "scan_a.mix") == 0);
		Check_Equal("no further entries follow", (unsigned long)FindNextFileA(named, &single), 0);
		FindClose(named);
	}

	HANDLE const folder = FindFirstFileA(Scratch.c_str(), &single);
	Check("a scan without a wildcard finds a directory", folder != INVALID_HANDLE_VALUE);
	if (folder != INVALID_HANDLE_VALUE) {
		Check("the directory reports itself as one", (single.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
		FindClose(folder);
	}

	Check("closing a scan that was never opened fails", FindClose(INVALID_HANDLE_VALUE) == FALSE);
}


/*
** A subdirectory reached with a backslash, and a name asked for in a case other than the
** one it was created under. The engine does both: it spells its paths with backslashes and
** asks for TIBSUN.MIX in upper case whatever case the file was installed under.
*/
static void Test_Subdirectory_And_Case(void)
{
	std::string const folder = Scratch_Path("sub");

	Check("create a subdirectory", CreateDirectoryA(folder.c_str(), NULL) != FALSE);
	Check("creating it twice fails", CreateDirectoryA(folder.c_str(), NULL) == FALSE);
	Check_Equal("creating it twice says why", GetLastError(), ERROR_ALREADY_EXISTS);

	std::string const path = folder + "\\MixedCase.Dat";
	HANDLE const file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("create a file in the subdirectory", file != INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE) return;

	DWORD written = 0;
	WriteFile(file, "opents", 6, &written, NULL);
	CloseHandle(file);

	std::string const shouted = folder + "\\MIXEDCASE.DAT";
	HANDLE const reopened = CreateFileA(shouted.c_str(), GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check("open the file under a different case", reopened != INVALID_HANDLE_VALUE);

	if (reopened != INVALID_HANDLE_VALUE) {
		char read[8] = {0};
		DWORD got = 0;
		ReadFile(reopened, read, 6, &got, NULL);
		Check_Equal("length read under a different case", got, 6);
		Check("contents read under a different case", memcmp(read, "opents", 6) == 0);
		CloseHandle(reopened);
	}

	/*
	** A scan whose pattern is spelled in the other case, which is what the engine's
	** MAPS*.MIX and ECACHE*.MIX scans amount to on a case-sensitive host.
	*/
	WIN32_FIND_DATAA found;
	HANDLE const search = FindFirstFileA((folder + "\\mixedcase.*").c_str(), &found);
	Check("scan for a pattern in a different case", search != INVALID_HANDLE_VALUE);
	if (search != INVALID_HANDLE_VALUE) FindClose(search);

	Check("delete the file in the subdirectory", DeleteFileA(path.c_str()) != FALSE);
	Check("remove the subdirectory", RemoveDirectoryA(folder.c_str()) != FALSE);
}


static void Test_Delete(void)
{
	Check("write the file to delete", Write_File("doomed.dat", 16));

	std::string const path = Scratch_Path("doomed.dat");

	Check("delete the file", DeleteFileA(path.c_str()) != FALSE);
	Check_Equal("the file is gone", GetFileAttributesA(path.c_str()), INVALID_FILE_ATTRIBUTES);
	Check("deleting it again fails", DeleteFileA(path.c_str()) == FALSE);
}


/*
** A handle must be distinguishable from both values the API reserves, and must stop being
** valid once it is closed.
*/
static void Test_Handles(void)
{
	Check("write the file to hold open", Write_File("handle.dat", 16));

	HANDLE const first = Open_For_Read("handle.dat");
	HANDLE const second = Open_For_Read("handle.dat");

	Check("a handle is not the failure value", first != INVALID_HANDLE_VALUE);
	Check("a handle is not null", first != NULL);
	Check("two opens give two handles", first != second);

	Check("close the first handle", CloseHandle(first) != FALSE);
	Check("closing it twice fails", CloseHandle(first) == FALSE);

	/*
	** The handle still open must be unaffected by the other one closing.
	*/
	Check_Equal("the other handle still works", GetFileSize(second, NULL), 16);
	Check("close the second handle", CloseHandle(second) != FALSE);

	Check_Equal("the size of a closed handle cannot be had", GetFileSize(second, NULL),
		(unsigned long)INVALID_FILE_SIZE);
	Check_Equal("the size of the failure value cannot be had", GetFileSize(INVALID_HANDLE_VALUE, NULL),
		(unsigned long)INVALID_FILE_SIZE);
}


static void Clean_Up(void)
{
	char const * const leftovers[] = {
		"basic.dat", "seek.dat", "disposition.dat", "fresh.dat", "attributes.dat",
		"times.dat", "scan_a.mix", "scan_b.mix", "other.txt", "handle.dat"
	};

	for (char const * name : leftovers) {
		SetFileAttributesA(Scratch_Path(name).c_str(), FILE_ATTRIBUTE_NORMAL);
		DeleteFileA(Scratch_Path(name).c_str());
	}

	RemoveDirectoryA(Scratch.c_str());
}


int main(int argc, char ** argv)
{
	Scratch = (argc > 1) ? argv[1] : ".";
	Scratch += "\\win32file-scratch";

	if (!CreateDirectoryA(Scratch.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		printf("FATAL: could not create the scratch directory %s\n", Scratch.c_str());
		return(1);
	}

	Test_Write_Read_Size();
	Test_Seek();
	Test_Creation_Dispositions();
	Test_Attributes();
	Test_File_Times();
	Test_Wildcard_Scan();
	Test_Subdirectory_And_Case();
	Test_Delete();
	Test_Handles();

	Clean_Up();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
