/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the read-only half of the WebAssembly target's file layer: the branch of
// CreateFileA and the FindFirstFileA family that answers out of a mounted ISO9660 image
// rather than out of the host directory. The image is built here, so no game disc, game
// data or original executable is involved, and none may become one.
//
// The transport the browser reads an image through is not covered. It needs a server and a
// document, neither of which a test may depend on, so what runs here is the same code path
// over a local image file.

#include "isohttp.h"
#include "win32compat.h"

#include <unistd.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-62s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition) {
		Failures++;
	}
}


/*
**	A repeatable byte pattern, so a read can be compared against what was written without
**	carrying a table of expected bytes around.
*/
std::vector<unsigned char> Pattern(unsigned int seed, std::size_t size)
{
	std::vector<unsigned char> data(size);
	unsigned int state = seed * 2654435761u + 1u;

	for (std::size_t index = 0; index < size; index++) {
		state = state * 1103515245u + 12345u;
		data[index] = (unsigned char)(state >> 16);
	}

	return data;
}


/*
**	------------------------------------------------------------------------------------
**	A minimal ISO9660 writer. It lays out a primary descriptor, its terminator, a root
**	directory of one sector, one subdirectory of one sector, and the file contents after
**	them. That is the whole of what the reader under test is asked for here.
**	------------------------------------------------------------------------------------
*/

struct FileSpec {
	std::string Name;
	std::vector<unsigned char> Data;
};

struct DirSpec {
	std::string Name;
	std::vector<FileSpec> Files;
};


void Put_Both_32(unsigned char * field, std::uint32_t value)
{
	field[0] = (unsigned char)(value & 0xFF);
	field[1] = (unsigned char)((value >> 8) & 0xFF);
	field[2] = (unsigned char)((value >> 16) & 0xFF);
	field[3] = (unsigned char)((value >> 24) & 0xFF);
	field[4] = (unsigned char)((value >> 24) & 0xFF);
	field[5] = (unsigned char)((value >> 16) & 0xFF);
	field[6] = (unsigned char)((value >> 8) & 0xFF);
	field[7] = (unsigned char)(value & 0xFF);
}


void Put_Both_16(unsigned char * field, std::uint16_t value)
{
	field[0] = (unsigned char)(value & 0xFF);
	field[1] = (unsigned char)((value >> 8) & 0xFF);
	field[2] = (unsigned char)((value >> 8) & 0xFF);
	field[3] = (unsigned char)(value & 0xFF);
}


/*
**	The identifier a name is recorded under. A file carries the version suffix ECMA-119
**	requires; a directory carries the name alone.
*/
std::string File_Identifier(std::string const & name)
{
	return(name + ";1");
}


unsigned int Record_Size(std::string const & identifier)
{
	unsigned int size = 33 + (unsigned int)identifier.size();

	return(size + (size & 1));
}


void Write_Record(unsigned char * record, std::string const & identifier, std::uint32_t start,
	std::uint32_t length, bool directory)
{
	unsigned int const size = Record_Size(identifier);

	std::memset(record, 0, size);

	record[0] = (unsigned char)size;
	Put_Both_32(record + 2, start);
	Put_Both_32(record + 10, length);

	record[18] = (unsigned char)(2020 - 1900);
	record[19] = 6;
	record[20] = 15;
	record[21] = 12;
	record[22] = 30;
	record[23] = 20;

	record[25] = directory ? (unsigned char)ISO_RECORD_DIRECTORY : (unsigned char)0;
	Put_Both_16(record + 28, 1);
	record[32] = (unsigned char)identifier.size();
	std::memcpy(record + 33, identifier.data(), identifier.size());
}


void Write_Self_And_Parent(unsigned char * sector, std::uint32_t self, std::uint32_t parent, std::uint32_t size)
{
	Write_Record(sector, std::string(1, '\0'), self, size, true);
	Write_Record(sector + 34, std::string(1, '\1'), parent, size, true);
}


/*
**	Builds the whole image. Layout: descriptors at sectors 16 and 17, the root directory at
**	18, one subdirectory at 19, and every file's contents from sector 20 onwards.
*/
std::vector<unsigned char> Build_Image(std::vector<FileSpec> const & rootfiles, DirSpec const & sub)
{
	std::uint32_t const rootblock = 18;
	std::uint32_t const subblock = 19;
	std::uint32_t content = 20;

	std::vector<std::uint32_t> rootstarts;
	std::vector<std::uint32_t> substarts;

	for (FileSpec const & file : rootfiles) {
		rootstarts.push_back(content);
		content += (std::uint32_t)((file.Data.size() + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE);
		if (file.Data.empty()) content++;
	}

	for (FileSpec const & file : sub.Files) {
		substarts.push_back(content);
		content += (std::uint32_t)((file.Data.size() + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE);
		if (file.Data.empty()) content++;
	}

	std::vector<unsigned char> image((std::size_t)content * ISO_SECTOR_SIZE, 0);

	/*
	**	The primary descriptor and the terminator that ends the set.
	*/
	unsigned char * const primary = image.data() + (std::size_t)16 * ISO_SECTOR_SIZE;

	primary[0] = (unsigned char)ISO_DESCRIPTOR_PRIMARY;
	std::memcpy(primary + 1, "CD001", 5);
	primary[6] = 1;
	std::memset(primary + 8, ' ', 32);
	std::memset(primary + 40, ' ', 32);
	std::memcpy(primary + 40, "OPENTSTEST", 10);
	Put_Both_32(primary + 80, content);
	Put_Both_16(primary + 120, 1);
	Put_Both_16(primary + 124, 1);
	Put_Both_16(primary + 128, ISO_SECTOR_SIZE);
	Write_Record(primary + 156, std::string(1, '\0'), rootblock, ISO_SECTOR_SIZE, true);
	primary[881] = 1;

	unsigned char * const terminator = image.data() + (std::size_t)17 * ISO_SECTOR_SIZE;

	terminator[0] = (unsigned char)ISO_DESCRIPTOR_TERMINATOR;
	std::memcpy(terminator + 1, "CD001", 5);
	terminator[6] = 1;

	/*
	**	The root directory, and the subdirectory it names.
	*/
	unsigned char * const root = image.data() + (std::size_t)rootblock * ISO_SECTOR_SIZE;
	unsigned int offset = 0;

	Write_Self_And_Parent(root, rootblock, rootblock, ISO_SECTOR_SIZE);
	offset = 68;

	std::string const subidentifier = sub.Name;

	Write_Record(root + offset, subidentifier, subblock, ISO_SECTOR_SIZE, true);
	offset += Record_Size(subidentifier);

	for (std::size_t index = 0; index < rootfiles.size(); index++) {
		std::string const identifier = File_Identifier(rootfiles[index].Name);

		Write_Record(root + offset, identifier, rootstarts[index], (std::uint32_t)rootfiles[index].Data.size(), false);
		offset += Record_Size(identifier);
	}

	unsigned char * const folder = image.data() + (std::size_t)subblock * ISO_SECTOR_SIZE;

	Write_Self_And_Parent(folder, subblock, rootblock, ISO_SECTOR_SIZE);
	offset = 68;

	for (std::size_t index = 0; index < sub.Files.size(); index++) {
		std::string const identifier = File_Identifier(sub.Files[index].Name);

		Write_Record(folder + offset, identifier, substarts[index], (std::uint32_t)sub.Files[index].Data.size(), false);
		offset += Record_Size(identifier);
	}

	/*
	**	The file contents.
	*/
	for (std::size_t index = 0; index < rootfiles.size(); index++) {
		std::memcpy(image.data() + (std::size_t)rootstarts[index] * ISO_SECTOR_SIZE,
			rootfiles[index].Data.data(), rootfiles[index].Data.size());
	}

	for (std::size_t index = 0; index < sub.Files.size(); index++) {
		std::memcpy(image.data() + (std::size_t)substarts[index] * ISO_SECTOR_SIZE,
			sub.Files[index].Data.data(), sub.Files[index].Data.size());
	}

	return image;
}


bool Write_File(std::string const & path, std::vector<unsigned char> const & data)
{
	std::FILE * const file = std::fopen(path.c_str(), "wb");

	if (file == nullptr) return false;

	bool const written = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();

	std::fclose(file);
	return written;
}


/*
**	Reads a whole file through the Win32 entry points under test.
*/
bool Read_Through_Handle(char const * name, std::vector<unsigned char> & out)
{
	HANDLE const handle = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (handle == INVALID_HANDLE_VALUE) return false;

	DWORD const size = GetFileSize(handle, nullptr);

	out.assign(size != 0 ? size : 1, 0);

	DWORD read = 0;
	bool const ok = (ReadFile(handle, out.data(), size, &read, nullptr) != FALSE) && read == size;

	out.resize(read);
	CloseHandle(handle);
	return ok;
}


bool Reports(std::vector<std::string> const & found, char const * name)
{
	for (std::string const & entry : found) {
		if (entry == name) return true;
	}

	return false;
}


std::vector<std::string> Search(char const * pattern)
{
	std::vector<std::string> found;
	WIN32_FIND_DATAA data;

	HANDLE const handle = FindFirstFileA(pattern, &data);
	if (handle == INVALID_HANDLE_VALUE) return found;

	do {
		found.push_back(data.cFileName);
	} while (FindNextFileA(handle, &data) != FALSE);

	FindClose(handle);
	return found;
}

} // namespace


int main(int argc, char ** argv)
{
	std::string directory = (argc > 1) ? argv[1] : ".";

	while (!directory.empty() && (directory.back() == '/' || directory.back() == '\\')) {
		directory.pop_back();
	}

	/*
	**	The searches below name no directory, so the host half of every one of them is the
	**	directory the harness runs in. It is given one of its own.
	*/
	Check(::chdir(directory.c_str()) == 0, "the harness runs in a directory of its own");

	std::string const imagepath = "isohttptest.iso";

	/*
	**	CATALOG has no extension, which is what tells `*.*` apart from `*`; TIBSUN.MIX and
	**	SIDEBAR.MIX are written out of alphabetical order so the search has something to
	**	sort.
	*/
	std::vector<unsigned char> const tibsun = Pattern(1, 5000);
	std::vector<unsigned char> const sidebar = Pattern(2, 100);
	std::vector<unsigned char> const catalog = Pattern(3, 40);
	std::vector<unsigned char> const nested = Pattern(4, 3000);

	std::vector<FileSpec> rootfiles;
	rootfiles.push_back(FileSpec{"TIBSUN.MIX", tibsun});
	rootfiles.push_back(FileSpec{"SIDEBAR.MIX", sidebar});
	rootfiles.push_back(FileSpec{"CATALOG", catalog});

	DirSpec sub;
	sub.Name = "CACHE";
	sub.Files.push_back(FileSpec{"NESTED.DAT", nested});

	Check(Write_File(imagepath, Build_Image(rootfiles, sub)), "the test image is written");
	Check(Win32_Mount_Image(imagepath.c_str()), "the image mounts as the read only filesystem");

	/*
	**	Opening and reading a file the host has no copy of.
	*/
	std::vector<unsigned char> read;

	Check(Read_Through_Handle("TIBSUN.MIX", read), "a file on the image opens and reads");
	Check(read == tibsun, "the bytes read are the bytes on the image");

	Check(Read_Through_Handle("tibsun.mix", read) && read == tibsun, "the name matches without regard to case");
	Check(Read_Through_Handle("CACHE\\NESTED.DAT", read) && read == nested, "a file in a subdirectory reads");
	Check(Read_Through_Handle("./CACHE/NESTED.DAT", read) && read == nested, "either separator and a leading dot resolve");
	Check(!Read_Through_Handle("../TIBSUN.MIX", read), "a path climbing out of the volume is refused");
	Check(!Read_Through_Handle("NOTHERE.MIX", read), "a name on neither side is not found");

	/*
	**	The handle answers the rest of the file API.
	*/
	HANDLE const handle = CreateFileA("TIBSUN.MIX", GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	Check(handle != INVALID_HANDLE_VALUE, "an image handle is handed out");

	DWORD high = 0;

	Check(GetFileSize(handle, &high) == (DWORD)tibsun.size() && high == 0, "the size is the size on the image");

	unsigned char window[16];
	DWORD got = 0;

	Check(SetFilePointer(handle, 4096, nullptr, FILE_BEGIN) == 4096, "a seek from the start reports the position");
	Check(ReadFile(handle, window, sizeof(window), &got, nullptr) != FALSE && got == sizeof(window),
		"a read after a seek delivers");
	Check(std::memcmp(window, tibsun.data() + 4096, sizeof(window)) == 0, "it delivers from where the seek put it");

	Check(SetFilePointer(handle, -16, nullptr, FILE_CURRENT) == 4096, "a relative seek moves from the position");
	Check(SetFilePointer(handle, 0, nullptr, FILE_END) == (DWORD)tibsun.size(), "a seek to the end reports the size");
	Check(ReadFile(handle, window, sizeof(window), &got, nullptr) != FALSE && got == 0,
		"a read at the end succeeds with nothing");
	Check(SetFilePointer(handle, -1, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER
		&& GetLastError() == ERROR_NEGATIVE_SEEK, "a seek before the start is refused");

	Check(SetFilePointer(handle, (LONG)tibsun.size() - 4, nullptr, FILE_BEGIN) == (DWORD)tibsun.size() - 4,
		"the position can be put just short of the end");
	Check(ReadFile(handle, window, sizeof(window), &got, nullptr) != FALSE && got == 4,
		"a read across the end is short rather than failed");

	Check(WriteFile(handle, window, 4, &got, nullptr) == FALSE && GetLastError() == ERROR_ACCESS_DENIED,
		"a write to the image is denied");
	Check(SetEndOfFile(handle) == FALSE && GetLastError() == ERROR_ACCESS_DENIED,
		"truncating the image is denied");
	Check(SetFileTime(handle, nullptr, nullptr, nullptr) == FALSE && GetLastError() == ERROR_ACCESS_DENIED,
		"stamping the image is denied");

	BY_HANDLE_FILE_INFORMATION information;

	Check(GetFileInformationByHandle(handle, &information) != FALSE
		&& information.nFileSizeLow == (DWORD)tibsun.size()
		&& (information.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0,
		"the handle reports a read only file of the right size");

	FILETIME written;

	Check(GetFileTime(handle, nullptr, nullptr, &written) != FALSE
		&& (written.dwLowDateTime != 0 || written.dwHighDateTime != 0),
		"the recorded date reaches the caller");

	Check(CloseHandle(handle) != FALSE, "the handle closes");
	Check(CloseHandle(handle) == FALSE && GetLastError() == ERROR_INVALID_HANDLE, "and does not close twice");

	Check(GetFileAttributesA("SIDEBAR.MIX") == FILE_ATTRIBUTE_READONLY,
		"a name only the image has reports read only attributes");
	Check(GetFileAttributesA("NOTHERE.MIX") == INVALID_FILE_ATTRIBUTES,
		"a name neither side has reports none");

	/*
	**	Searching. The order is what decides which of several archives overrides which, so
	**	it is asserted rather than assumed.
	*/
	std::vector<std::string> found = Search("*.MIX");

	Check(found.size() == 2 && found[0] == "SIDEBAR.MIX" && found[1] == "TIBSUN.MIX",
		"a wildcard search reports the image in case insensitive order");

	/*
	**	`*.*` is DOS for every file, so it has to reach a name with no extension. The host
	**	half of the same search reports the directory's own entries, so what is asserted is
	**	that the image's extensionless name is among them.
	*/
	found = Search("*.*");
	Check(Reports(found, "CATALOG") && Reports(found, "TIBSUN.MIX"),
		"`*.*` reports the extensionless name as well");

	found = Search("TIBSUN.MIX");
	Check(found.size() == 1 && found[0] == "TIBSUN.MIX", "a search with no wildcard names one entry");

	found = Search("CACHE/*.DAT");
	Check(found.size() == 1 && found[0] == "NESTED.DAT", "a search inside a directory of the image reports it");

	found = Search("*.NONE");
	Check(found.empty(), "a wildcard matching nothing reports nothing");

	/*
	**	The host sits over the image. A file written beside it shadows the one on it, and a
	**	search reports the host's copy once rather than both.
	*/
	std::vector<unsigned char> const shadow = Pattern(9, 64);

	Check(Write_File("SIDEBAR.MIX", shadow), "a file is written beside the image");
	Check(Read_Through_Handle("SIDEBAR.MIX", read) && read == shadow, "the host's copy is the one that opens");

	found = Search("*.MIX");
	Check(found.size() == 2 && found[0] == "SIDEBAR.MIX" && found[1] == "TIBSUN.MIX",
		"and the search reports it once, in the same order");

	DeleteFileA("SIDEBAR.MIX");

	/*
	**	Unmounting takes the read only half away again.
	*/
	Win32_Unmount_Image();

	Check(!Read_Through_Handle("TIBSUN.MIX", read), "nothing resolves on the image once it is unmounted");
	Check(Search("*.MIX").empty(), "and nothing is left to search");

	std::remove(imagepath.c_str());

	std::printf("\n%s\n", Failures == 0 ? "all checks passed" : "checks failed");
	return (Failures == 0) ? 0 : 1;
}
