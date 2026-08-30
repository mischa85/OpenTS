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

#include <emscripten/emscripten.h>
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


/*
**	------------------------------------------------------------------------------------
**	How the host names the discs. A page sets Module.opentsImage and a shell sets
**	OPENTS_IMAGE; either may name one image or several, and the reading of that is the same
**	code for both. The environment is the half a harness can set, so it is the half tested.
**	------------------------------------------------------------------------------------
*/
void Check_Image_Locations(void)
{
	EM_ASM({ process.env["OPENTS_IMAGE"] = " one.iso ,two.iso, ,three.iso "; });

	std::vector<std::string> locations;

	ISO_Image_Locations(locations);

	Check(locations.size() == 3, "a comma separated list names one image per entry");
	Check(locations.size() == 3 && locations[0] == "one.iso" && locations[1] == "two.iso"
		&& locations[2] == "three.iso",
		"in the order given, trimmed, and with nothing between two commas");
}


/*
**	------------------------------------------------------------------------------------
**	Several discs at once. A game comes on more than one of them, so what is checked here
**	is that the mounted images are searched in order, that each offers its installed data
**	directory ahead of its own root, and that a wildcard search reports what all of them
**	hold together.
**	------------------------------------------------------------------------------------
*/
void Check_Several_Images(void)
{
	std::vector<unsigned char> const oldmulti = Pattern(11, 700);
	std::vector<unsigned char> const newmulti = Pattern(12, 1500);
	std::vector<unsigned char> const tibsun = Pattern(13, 900);
	std::vector<unsigned char> const expand = Pattern(14, 300);

	/*
	**	The expansion disc, the first of the three, carries its own copy of MULTI.MIX. The
	**	base disc carries an older one in its root, and the second base disc neither.
	*/
	std::vector<FileSpec> expansionroot;
	expansionroot.push_back(FileSpec{"MAPS03.MIX", Pattern(15, 100)});

	DirSpec expansioninstall;
	expansioninstall.Name = "INSTALL";
	expansioninstall.Files.push_back(FileSpec{"MULTI.MIX", newmulti});
	expansioninstall.Files.push_back(FileSpec{"EXPAND01.MIX", expand});

	std::vector<FileSpec> firstroot;
	firstroot.push_back(FileSpec{"MAPS01.MIX", Pattern(16, 100)});
	firstroot.push_back(FileSpec{"MULTI.MIX", oldmulti});

	DirSpec firstinstall;
	firstinstall.Name = "INSTALL";
	firstinstall.Files.push_back(FileSpec{"TIBSUN.MIX", tibsun});

	std::vector<FileSpec> secondroot;
	secondroot.push_back(FileSpec{"MAPS02.MIX", Pattern(17, 100)});

	DirSpec secondinstall;
	secondinstall.Name = "INSTALL";

	Check(Write_File("discthree.iso", Build_Image(expansionroot, expansioninstall))
		&& Write_File("discone.iso", Build_Image(firstroot, firstinstall))
		&& Write_File("disctwo.iso", Build_Image(secondroot, secondinstall)),
		"three test images are written");

	Check(Win32_Mount_Image("discthree.iso") && Win32_Mount_Image("discone.iso")
		&& Win32_Mount_Image("disctwo.iso"), "all three mount, and mounting appends");

	std::vector<unsigned char> read;

	Check(Read_Through_Handle("MULTI.MIX", read) && read == newmulti,
		"a name several discs carry resolves on the first of them");
	Check(Read_Through_Handle("TIBSUN.MIX", read) && read == tibsun,
		"a name only a later disc carries resolves there");
	Check(Read_Through_Handle("EXPAND01.MIX", read) && read == expand,
		"and the installed directory of a disc is searched ahead of its root");
	Check(GetFileAttributesA("TIBSUN.MIX") == FILE_ATTRIBUTE_READONLY,
		"a name on any of the discs reports read only attributes");

	std::vector<std::string> found = Search("MAPS*.MIX");

	Check(found.size() == 3 && found[0] == "MAPS01.MIX" && found[1] == "MAPS02.MIX"
		&& found[2] == "MAPS03.MIX", "a wildcard search reports what every disc holds");

	found = Search("MULTI.MIX");
	Check(found.size() == 1, "and reports a name several discs carry once");

	WIN32_FIND_DATAA data;
	HANDLE const handle = FindFirstFileA("MULTI.MIX", &data);

	Check(handle != INVALID_HANDLE_VALUE && data.nFileSizeLow == (DWORD)newmulti.size(),
		"reporting it as the copy that opens");
	FindClose(handle);

	Win32_Unmount_Image();

	Check(Search("MAPS*.MIX").empty(), "unmounting takes every disc away");

	std::remove("discthree.iso");
	std::remove("discone.iso");
	std::remove("disctwo.iso");
}


/*
**	------------------------------------------------------------------------------------
**	What the browser's database is allowed to be believed about. The transport needs a
**	server and a document, but the decision that a stored block belongs to the image now
**	being read is arithmetic over a key and a list, and that is what is checked here.
**	------------------------------------------------------------------------------------
*/
void Check_Block_Index(void)
{
	std::string const key = ISOBlockIndexClass::Signature("http://host/opents-data.iso", 4096, "\"abc\"");

	Check(!key.empty(), "an image that can be identified has a key");
	Check(ISOBlockIndexClass::Signature("http://host/opents-data.iso", 4096, "\"abc\"") == key,
		"the same image answers to the same key");
	Check(ISOBlockIndexClass::Signature("http://host/other.iso", 4096, "\"abc\"") != key,
		"a different URL is a different image");
	Check(ISOBlockIndexClass::Signature("http://host/opents-data.iso", 8192, "\"abc\"") != key,
		"a different length is a different image");
	Check(ISOBlockIndexClass::Signature("http://host/opents-data.iso", 4096, "\"xyz\"") != key,
		"a server that names a new version names a different image");
	Check(ISOBlockIndexClass::Signature("http://host/opents-data.iso", 4096, "") != key,
		"and a server that names none is not the same as one that does");

	Check(ISOBlockIndexClass::Signature(nullptr, 4096, "").empty()
		&& ISOBlockIndexClass::Signature("", 4096, "").empty()
		&& ISOBlockIndexClass::Signature("http://host/opents-data.iso", 0, "").empty(),
		"an image that cannot be identified has no key, and so no store");

	std::string const flattened = ISOBlockIndexClass::Signature("http://host/x.iso", 1, "a\nb\tc\x80");
	Check(flattened.find('\n') == std::string::npos && flattened.find('\t') == std::string::npos,
		"a key carries nothing that would end the line it is stored on");

	/*
	**	The slot is where one image's blocks are kept, so two images must never share one,
	**	and a new version of the same image must land on the one it already has.
	*/
	Check(ISOBlockIndexClass::Store_Slot("http://host/ts1.iso")
		!= ISOBlockIndexClass::Store_Slot("http://host/ts2.iso"),
		"two images are held in slots of their own");
	Check(ISOBlockIndexClass::Store_Slot("http://host/ts1.iso")
		== ISOBlockIndexClass::Store_Slot("http://host/ts1.iso"),
		"and one image answers to the same slot however it is identified");
	Check(ISOBlockIndexClass::Store_Slot(nullptr).empty() && ISOBlockIndexClass::Store_Slot("").empty(),
		"an image that cannot be identified has no slot, and so no store");

	/*
	**	A store that was written for this image is taken on; one written for any other is
	**	refused and leaves nothing behind, which is what keeps another image's sectors from
	**	being served as this one's.
	*/
	ISOBlockIndexClass index;

	index.Reset(key);

	std::vector<std::uint64_t> evicted;
	index.Note(3, 65536, evicted);
	index.Note(9, 65536, evicted);
	index.Note(4, 1024, evicted);

	Check(evicted.empty() && index.Count() == 3 && index.Bytes() == 65536 + 65536 + 1024,
		"a fresh index records what was stored");
	Check(index.Holds(3) && index.Holds(9) && index.Holds(4) && !index.Holds(5),
		"and answers for the blocks it recorded");

	index.Note(3, 65536, evicted);
	Check(evicted.empty() && index.Count() == 3, "recording a block twice records it once");

	std::string const record = index.Encode();

	ISOBlockIndexClass restored;

	Check(restored.Adopt(record.c_str(), key), "a record written for this image is taken on");
	Check(restored.Count() == 3 && restored.Bytes() == index.Bytes()
		&& restored.Holds(3) && restored.Holds(9) && restored.Holds(4),
		"and reports the same blocks it was written with");

	std::string const other = ISOBlockIndexClass::Signature("http://host/opents-data.iso", 4096, "\"xyz\"");

	Check(!restored.Adopt(record.c_str(), other), "a record written for another image is refused");
	Check(restored.Count() == 0 && !restored.Holds(3), "and leaves no block behind to be served");

	Check(!restored.Adopt("", key), "an empty record is not a record");
	Check(!restored.Adopt("opents-iso-1\n", key), "a record with no key is refused");
	Check(!restored.Adopt(("opents-iso-1\n" + key + "\n3:65536,9").c_str(), key),
		"a record that does not parse is refused whole");
	Check(!restored.Adopt(("something-else\n" + key + "\n3:65536").c_str(), key),
		"a record another version wrote is refused");
	Check(restored.Count() == 0, "and none of them leaves a block behind");

	Check(restored.Adopt(("opents-iso-1\n" + key + "\n").c_str(), key) && restored.Count() == 0,
		"a record for this image holding nothing is taken on empty");

	/*
	**	The cap. What no longer fits goes, oldest first, and the caller is told which blocks
	**	to delete so the record and the database stay describing the same thing.
	*/
	ISOBlockIndexClass bounded;

	bounded.Reset(key);

	std::uint64_t const blocks = ISOBlockIndexClass::STORE_LIMIT / 65536;

	for (std::uint64_t index_number = 0; index_number < blocks; index_number++) {
		evicted.clear();
		bounded.Note(index_number, 65536, evicted);
	}

	Check(bounded.Count() == (std::size_t)blocks && bounded.Bytes() == ISOBlockIndexClass::STORE_LIMIT,
		"the store fills to the cap");

	evicted.clear();
	bounded.Note(blocks, 65536, evicted);

	Check(evicted.size() == 1 && evicted[0] == 0, "and the next block evicts the oldest one");
	Check(!bounded.Holds(0) && bounded.Holds(blocks), "which stops being served and is replaced");
	Check(bounded.Bytes() == ISOBlockIndexClass::STORE_LIMIT, "leaving the store no larger than the cap");
}


/*
**	------------------------------------------------------------------------------------
**	How far in front of a read the fetching is allowed to run. Whether a request actually
**	overlaps the decoding is a question about a browser, but which blocks are worth asking
**	for is arithmetic over block numbers, and a window aimed wrongly is the difference
**	between a movie that plays and a disc that is pulled down for nothing.
**	------------------------------------------------------------------------------------
*/
void Check_Read_Ahead(void)
{
	std::uint64_t const blocks = 1000;
	std::uint64_t start = 0;
	std::uint64_t count = 0;

	ISOReadAheadClass ahead;

	Check(!ahead.Span(blocks, start, count), "nothing is fetched ahead of a source nobody has read");

	/*
	**	A read on its own says nothing about where the next one is going, and neither does a
	**	second one somewhere else. Metadata is read in small pieces all over an image, and
	**	pulling a window in after each would cost more than the reads themselves.
	*/
	ahead.Note(500, 500);
	Check(!ahead.Span(blocks, start, count), "one read establishes no run");

	ahead.Note(20, 20);
	Check(ahead.Broke() && !ahead.Span(blocks, start, count), "a read somewhere else is a seek, not a run");

	ahead.Note(700, 700);
	ahead.Note(701, 701);
	Check(!ahead.Broke(), "a read in the block after the last one continues the run");
	Check(!ahead.Span(blocks, start, count), "which is not yet enough of a run to fetch on");

	/*
	**	Most of a movie's reads are shorter than a block and never leave the one before them.
	**	Those hold the run rather than building it, so what the window follows is the reads
	**	that actually move forward.
	*/
	ahead.Note(701, 701);
	ahead.Note(701, 701);
	Check(!ahead.Span(blocks, start, count), "a read that does not leave its block does not advance the run");

	ahead.Note(702, 702);
	Check(ahead.Span(blocks, start, count), "a run that keeps moving forward is fetched ahead of");
	Check(start == 703, "the span begins where the reading has not reached");
	Check(count == (std::uint64_t)ISOReadAheadClass::WINDOW_MIN,
		"and opens no wider than the window a young run has earned");

	/*
	**	Asking twice for the same blocks would spend the connection on bytes already on
	**	their way, so what has been asked for is not asked for again until the cursor has
	**	moved past it.
	*/
	ahead.Issued(start + count);
	Check(!ahead.Span(blocks, start, count), "what has been asked for is not asked for again");

	/*
	**	The window widens as the run proves itself, which is what keeps a short burst cheap
	**	and lets a movie reach far enough ahead to cover a round trip.
	*/
	for (unsigned int step = 0; step < 8; step++) {
		std::uint64_t const at = 703 + step;
		ahead.Note(at, at);
		if (ahead.Span(blocks, start, count)) ahead.Issued(start + count);
	}

	Check(ahead.Edge() - ahead.Cursor() <= (std::uint64_t)ISOReadAheadClass::WINDOW_MAX,
		"a long run never reaches further ahead than the window allows");
	Check(ahead.Edge() - ahead.Cursor() > (std::uint64_t)ISOReadAheadClass::WINDOW_MIN,
		"but does reach further than a young one");

	/*
	**	A seek makes everything in front of the run it left worthless. The run starts again
	**	from where the seek landed and asks for nothing until it is believed once more.
	*/
	ahead.Note(10, 10);
	Check(ahead.Broke(), "a seek out of the run is reported so what is in flight can be let go");
	Check(!ahead.Span(blocks, start, count), "and the window closes until a new run is established");
	Check(ahead.Cursor() == 11, "the run starts again from where the seek landed");

	/*
	**	The image ends, and a request that runs past it would be answered short or refused.
	*/
	ISOReadAheadClass edge;

	for (std::uint64_t at = blocks - 6; at < blocks; at++) {
		edge.Note(at, at);
	}

	if (edge.Span(blocks, start, count)) {
		Check(start + count <= blocks, "no span is asked for past the end of the image");
	} else {
		Check(edge.Cursor() >= blocks, "nothing is asked for once the reading has reached the end");
	}

	/*
	**	A read may cover more than one block. It still counts as one step of a run, and the
	**	window opens in front of where it finished rather than where it began.
	*/
	ISOReadAheadClass extent;

	extent.Note(0, 1);
	extent.Note(2, 3);
	extent.Note(4, 5);
	Check(extent.Span(blocks, start, count) && start == 6,
		"a run of multi-block reads is followed from where the last of them ended");
	Check(count <= (std::uint64_t)ISOReadAheadClass::SPAN_MAX,
		"and no one request is larger than a span");

	/*
	**	A read long enough to be a span of its own carries its own round trip and goes out as
	**	one request whatever it covers, so there is nothing in front of it worth guessing at.
	**	That is how a mixfile is loaded, and the loading moves from file to file.
	*/
	ISOReadAheadClass loading;

	loading.Note(0, 39);
	loading.Note(40, 79);
	loading.Note(80, 119);
	Check(!loading.Span(blocks, start, count),
		"a read that is already a span of its own is not fetched ahead of");

	loading.Note(120, 120);
	loading.Note(121, 121);
	Check(loading.Span(blocks, start, count) && start == 122,
		"and the window opens again as soon as the reads are short enough to need it");

	extent.Reset();
	Check(extent.Cursor() == 0 && extent.Run() == 0 && !extent.Span(blocks, start, count),
		"a source that is closed forgets where its reading had reached");
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

	Check_Image_Locations();
	Check_Several_Images();
	Check_Block_Index();
	Check_Read_Ahead();

	std::printf("\n%s\n", Failures == 0 ? "all checks passed" : "checks failed");
	return (Failures == 0) ? 0 : 1;
}
