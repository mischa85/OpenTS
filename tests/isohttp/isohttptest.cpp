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
// over a local image file. What a request makes of an answer once it has one is covered,
// against a stub standing in for the transport, since deciding which image was answered for
// is the harness's own arithmetic and not the network's.

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

	/*
	**	A block nobody has read displaces nothing. The guessing offers a whole disc in one
	**	pass, so letting it evict would leave a full store holding wherever the pass finished
	**	rather than what the game read.
	*/
	evicted.clear();
	bounded.Note(blocks + 1, 65536, evicted, ISOBlockIndexClass::ADMIT_GUESS);

	Check(evicted.empty() && !bounded.Holds(blocks + 1), "a full store declines a block nobody read");
	Check(bounded.Holds(1), "rather than dropping one it is already holding");

	evicted.clear();
	bounded.Note(blocks + 2, 65536, evicted, ISOBlockIndexClass::ADMIT_READ);

	Check(evicted.size() == 1 && evicted[0] == 1 && bounded.Holds(blocks + 2),
		"while a block the engine read still displaces the oldest");

	/*
	**	The cap is a runtime figure, since what a browser will hold is not a constant. Set
	**	lower than what is held, it evicts down to itself; a store that fits underneath the
	**	new figure loses nothing.
	*/
	evicted.clear();
	bounded.Cap(16 * 65536, evicted);

	Check(bounded.Cap() == 16 * 65536 && bounded.Count() == 16,
		"lowering the cap evicts down to it");
	Check(evicted.size() == (std::size_t)blocks - 16, "and names every block that has to go");
	Check(bounded.Holds(blocks + 2), "keeping the newest of them");

	evicted.clear();
	bounded.Cap(0, evicted);
	Check(bounded.Cap() == 16 * 65536 && evicted.empty(), "a cap of nothing leaves the one that is set");

	/*
	**	And a write the origin refuses. The blocks of that batch were never stored, so the
	**	index has to stop offering them; everything an earlier batch wrote stays.
	*/
	std::vector<std::uint64_t> const refused = {blocks + 2, blocks - 1, 999999};

	bounded.Forget(refused);

	Check(!bounded.Holds(blocks + 2) && !bounded.Holds(blocks - 1),
		"a refused batch stops being served");
	Check(bounded.Count() == 14 && bounded.Bytes() == 14 * 65536,
		"and is taken off what the store is holding");

	/*
	**	The record has to survive the largest store the ceiling allows, because a record that
	**	does not fit the buffer it is read back through is a store thrown away whole.
	*/
	ISOBlockIndexClass full;
	std::uint64_t const most = ISOBlockIndexClass::STORE_MAX / 65536;

	full.Reset(key);
	full.Cap(ISOBlockIndexClass::STORE_MAX, evicted);

	for (std::uint64_t index_number = 0; index_number < most; index_number++) {
		evicted.clear();
		full.Note(index_number, 65536, evicted);
	}

	std::string const large = full.Encode();

	Check(full.Count() == (std::size_t)most && evicted.empty(),
		"an image may fill the largest store the ceiling allows");
	Check(large.size() < ISOBlockIndexClass::RECORD_MAX,
		"and the record describing it fits what reads it back");

	ISOBlockIndexClass reread;

	Check(reread.Adopt(large.c_str(), key) && reread.Count() == (std::size_t)most
		&& reread.Bytes() == full.Bytes(), "so the next run takes the whole of it on");
}


/*
**	Stands in for the browser's transport for the length of one probe. A ranged request is
**	answered the way a pool serving a large image does: the range is honoured, and the
**	request reports having ended at whichever node took it rather than where it was sent.
**	Nothing leaves the harness, and no server or document is involved.
*/
void Serve_From_Node(char const * page, char const * node, double total, char const * tag)
{
	EM_ASM({
		globalThis.location = {href: UTF8ToString($0)};

		var node = UTF8ToString($1);
		var total = $2;
		var tag = UTF8ToString($3);

		globalThis.XMLHttpRequest = function () {
			var answer = this;

			answer.status = 0;
			answer.responseURL = "";
			answer.open = function () {};
			answer.setRequestHeader = function () {};
			answer.send = function () {
				globalThis.__opentsAsked = (globalThis.__opentsAsked || 0) + 1;
				answer.status = 206;
				answer.responseURL = node;
			};
			answer.getResponseHeader = function (name) {
				var wanted = ("" + name).toLowerCase();
				if (wanted === "content-range") return "bytes 0-0/" + total;
				if (wanted === "etag") return tag;
				return null;
			};
		};
	}, page, node, total, tag);
}


void Stop_Serving(void)
{
	EM_ASM({
		delete globalThis.XMLHttpRequest;
		delete globalThis.location;
	});
}


// How many requests the transport above has been asked for since the count was cleared.
void Asked_Reset(void)
{
	EM_ASM({ globalThis.__opentsAsked = 0; });
}


int Asked(void)
{
	return(EM_ASM_INT({ return globalThis.__opentsAsked || 0; }));
}


/*
**	Stands in for the browser's local storage, which is where what a probe learned is kept
**	because it is the one store that answers before the engine has a wait to spend. node has
**	none, so without this every run is a first run and nothing below could be told apart.
*/
void Serve_Storage(void)
{
	/*
	**	Written a property at a time because the block below is a macro argument, and a
	**	comma between two of them at this level would end it.
	*/
	EM_ASM({
		var held = new Map();
		var store = {};

		store.getItem = function (key) {
			var name = "" + key;
			return held.has(name) ? held.get(name) : null;
		};
		store.setItem = function (key, value) { held.set("" + key, "" + value); };
		store.removeItem = function (key) { held.delete("" + key); };

		globalThis.localStorage = store;
	});
}


void Stop_Storing(void)
{
	EM_ASM({ delete globalThis.localStorage; });
}


/*
**	A server that answers the one request that says what the image is and refuses every
**	range past it, which is what an image that has been replaced under its old location
**	looks like to a run that believed a record instead of asking.
*/
void Serve_Changed(char const * page, double total, char const * tag)
{
	EM_ASM({
		globalThis.location = {href: UTF8ToString($0)};

		var total = $1;
		var tag = UTF8ToString($2);

		globalThis.XMLHttpRequest = function () {
			var answer = this;
			var probing = false;

			answer.status = 0;
			answer.response = null;
			answer.responseURL = "";
			answer.open = function () {};
			answer.overrideMimeType = function () {};
			answer.setRequestHeader = function (name, value) {
				if (("" + name).toLowerCase() !== "range") return;
				probing = ("" + value) === "bytes=0-0";
			};
			answer.send = function () {
				globalThis.__opentsAsked = (globalThis.__opentsAsked || 0) + 1;
				answer.status = probing ? 206 : 416;
			};
			answer.getResponseHeader = function (name) {
				var wanted = ("" + name).toLowerCase();
				if (wanted === "content-range") return "bytes 0-0/" + total;
				if (wanted === "etag") return tag;
				return null;
			};
		};
	}, page, total, tag);
}


/*
**	------------------------------------------------------------------------------------
**	What a probe learned, and what it costs to have to learn it. A probe is a round trip
**	spent before the engine has read a byte, and a game on three discs spends three of
**	them one after another, because the transport it goes through is synchronous. Nothing
**	it answers changes while the location does not, so the answer is kept and the second
**	launch asks nobody.
**	------------------------------------------------------------------------------------
*/
void Check_Probe_Record(void)
{
	ISOProbeClass written;

	written.Length = 710277120;
	written.Validator = "\"58f20419\"";
	written.Trip = 42.5;
	written.Rate = 1234.75;

	ISOProbeClass read;

	Check(read.Decode(written.Encode().c_str()), "a written record reads back");
	Check(read.Length == written.Length && read.Validator == written.Validator,
		"as the image it was written for");
	Check(read.Trip > 42.0 && read.Trip < 43.0 && read.Rate > 1234.0 && read.Rate < 1235.0,
		"and as what the link to it was measured to cost");

	Check(!read.Decode(""), "an empty record is not a record");
	Check(!read.Decode(nullptr), "and neither is nothing at all");
	Check(!read.Decode("something-else|4096|0.000|0.000|\"abc\""),
		"a record this build cannot read is refused");
	Check(!read.Decode("opents-probe-1|0|0.000|0.000|\"abc\""),
		"and so is one naming an image of no length");
	Check(!read.Decode("opents-probe-1|4096|0.000"), "and one that stops part way through");
	Check(read.Length == 0 && read.Validator.empty(),
		"none of which leaves anything behind to be believed");

	/*
	**	The validator is whatever the server chose to call the file, so it is the last field
	**	and takes the rest of the line rather than being escaped into one.
	*/
	ISOProbeClass odd;

	odd.Length = 4096;
	odd.Validator = "W/\"a|b\"";

	Check(read.Decode(odd.Encode().c_str()) && read.Validator == "W/\"a|b\"",
		"a validator carrying the separator is read back whole");

	ISOProbeClass rough;

	rough.Length = 4096;
	rough.Validator = "one\ntwo";

	Check(read.Decode(rough.Encode().c_str()) && read.Validator.find('\n') == std::string::npos,
		"and one carrying a line end is flattened rather than written");
}


void Check_Probe_Recall(void)
{
	char const * const page = "https://player.example/game/";
	char const * const image = "https://mirror.example/download/ts1.iso";
	double const total = 710277120.0;

	Serve_Storage();
	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total, "\"58f20419\"");

	ISOHttpSourceClass source;

	Asked_Reset();
	Check(source.Open(image), "an image nothing is known about opens");
	Check(Asked() == 1, "by asking the server what it is");
	Check(!source.Recalled(), "which is what a first run does");

	std::string const slot = source.Store_Key();
	std::string const signature = source.Store_Signature();

	source.Close();

	Asked_Reset();
	Check(source.Open(image), "the same image opens a second time");
	Check(Asked() == 0, "without asking the server anything at all");
	Check(source.Recalled(), "out of what the run before it was told");
	Check(source.Total_Size() == (std::uint64_t)total, "knowing how long the image is");
	Check(source.Store_Key() == slot && source.Store_Signature() == signature,
		"and landing on the blocks that run stored");

	source.Close();

	Asked_Reset();
	Check(source.Open("https://mirror.example/download/ts2.iso"),
		"a location nothing is held for opens");
	Check(Asked() == 1, "by asking the server, since it is not the location that was kept");

	source.Close();

	/*
	**	A relative name is the same image as the location it resolves to, so a page that
	**	names it either way is one record rather than two.
	*/
	Asked_Reset();
	Check(source.Open("https://player.example/game/ts3.iso"), "an image beside the page opens");
	Check(Asked() == 1, "by asking the server the first time");

	source.Close();

	Asked_Reset();
	Check(source.Open("ts3.iso") && Asked() == 0,
		"and the same image named beside the page is the record already held");

	source.Close();

	/*
	**	And this is the whole of what not asking gives up: an image replaced under a
	**	location that did not change is read as the image that was there before. What
	**	notices it is the check the run makes long after it has its data, which drops the
	**	record so that the next launch asks -- and that is a fetch nothing here can wait on.
	*/
	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total + 65536.0, "\"5f000000\"");

	Asked_Reset();
	Check(source.Open(image) && Asked() == 0 && source.Store_Signature() == signature,
		"an image replaced under an unchanged location is still read as the old one");

	source.Close();
	Stop_Serving();

	/*
	**	Until a read the server will not answer says so. A range refused is the first
	**	evidence a run that asked nothing has that what it believed is wrong, and asking
	**	then is what re-establishes the image and lets go of the blocks held for the old one.
	*/
	Serve_Changed(page, total + 65536.0, "\"5f000000\"");

	unsigned char scrap[2048];

	Check(source.Open(image) && source.Recalled(),
		"the image opens again out of the record that is now wrong");

	Asked_Reset();
	Check(!source.Read_At(0, scrap, sizeof(scrap)), "a read the server refuses fails");
	Check(Asked() > 1, "having asked the server what the image is before giving up on it");
	Check(!source.Recalled(), "so the image is no longer one nothing was asked about");
	Check(source.Total_Size() == (std::uint64_t)(total + 65536.0), "and is the length it now has");
	Check(source.Store_Signature() != signature,
		"under a signature the blocks stored for the old one do not answer to");

	source.Close();

	Asked_Reset();
	Check(source.Open(image) && Asked() == 0 && source.Store_Signature() != signature,
		"and what was learned in doing so is what the next run opens it with");

	source.Close();
	Stop_Serving();
	Stop_Storing();

	/*
	**	A host with no such storage -- node, and a browser whose owner has turned it off --
	**	reports having nothing, and every run is a first run exactly as it was before.
	*/
	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total, "\"58f20419\"");

	Asked_Reset();
	Check(source.Open(image) && Asked() == 1 && !source.Recalled(),
		"a host that keeps nothing asks the server every time");

	source.Close();
	Stop_Serving();
}


/*
**	------------------------------------------------------------------------------------
**	Which of a request's two URLs identifies the image: the one it was sent to, or the one
**	it ended at. A large image is commonly served from a pool that redirects each request
**	to whichever node answers it, so the URL a request ends at changes between one request
**	and the next while the image does not. Keying the store on that one gives a second run
**	a slot the first wrote nothing under, and the image is pulled down again, once per node.
**	------------------------------------------------------------------------------------
*/
void Check_Image_Identity(void)
{
	char const * const page = "https://player.example/game/";
	char const * const image = "https://mirror.example/download/ts1.iso";
	double const total = 710277120.0;

	ISOHttpSourceClass source;

	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total, "\"58f20419\"");

	Check(source.Open(image), "an image behind a redirecting pool opens");

	std::string const slot = source.Store_Key();
	std::string const signature = source.Store_Signature();

	Check(!slot.empty() && !signature.empty(), "and is identified well enough to be stored");
	Check(slot.find("node-one") == std::string::npos && signature.find("node-one") == std::string::npos,
		"by where it was asked for rather than by whichever node answered");

	source.Close();

	/*
	**	The same image a second time, from a different node. This is the case the store was
	**	losing: everything about the image is the same, and only the answering node differs.
	*/
	Serve_From_Node(page, "https://node-two.example/items/ts1.iso", total, "\"58f20419\"");

	Check(source.Open(image), "the same image opens again with another node answering");
	Check(source.Store_Key() == slot, "and lands in the slot the earlier run wrote under");
	Check(source.Store_Signature() == signature, "so the blocks that run stored are still believed");

	source.Close();

	/*
	**	What the redirect stopped carrying, the validator and the length still do. A node
	**	answering with something else is a different image and is caught as one.
	*/
	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total, "\"5f000000\"");

	Check(source.Open(image), "an image the server names a new version of opens");
	Check(source.Store_Key() == slot, "in the slot the old version is held in");
	Check(source.Store_Signature() != signature, "and is not the image whose blocks are stored there");

	source.Close();

	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total + 65536.0, "\"58f20419\"");

	Check(source.Open(image) && source.Store_Key() == slot && source.Store_Signature() != signature,
		"an image that has changed length is caught the same way");

	source.Close();

	/*
	**	A server naming no version at all leaves the key resting on the location and the
	**	length. Two images still keep slots of their own, which is what stops one disc's
	**	sectors being served as another's.
	*/
	Serve_From_Node(page, "https://node-one.example/items/ts1.iso", total, "");

	Check(source.Open(image) && !source.Store_Signature().empty(),
		"an image no server names a version of is still identified");

	std::string const unnamed = source.Store_Signature();

	Check(unnamed != signature, "differently from the same image with a version named");

	source.Close();

	Check(source.Open("https://mirror.example/download/ts2.iso"), "a second disc opens");
	Check(source.Store_Key() != slot && source.Store_Signature() != unnamed,
		"and is held in a slot of its own however alike the two answers are");

	source.Close();

	/*
	**	A location named relative to the page still becomes one key rather than one per
	**	directory the page is reached from, which is what the probe resolved it for.
	*/
	Check(source.Open("ts1.iso"), "an image named beside the page opens");
	Check(source.Store_Key() == std::string(page) + "ts1.iso",
		"and is identified by where the page resolves that name to");

	source.Close();
	Stop_Serving();
}


/*
**	Stands in for the transport for the length of a read rather than a probe. Every range is
**	answered out of a pattern the harness can check, and each answer is made to take a few
**	milliseconds so that the read is recorded as a stall the way a slow link's would be.
*/
void Serve_Slowly(char const * page, double total)
{
	EM_ASM({
		globalThis.location = {href: UTF8ToString($0)};

		var total = $1;

		globalThis.XMLHttpRequest = function () {
			var answer = this;
			var first = 0;
			var last = 0;

			answer.status = 0;
			answer.response = null;
			answer.responseURL = "";
			answer.open = function () {};
			answer.overrideMimeType = function () {};
			answer.setRequestHeader = function (name, value) {
				if (("" + name).toLowerCase() !== "range") return;

				var span = ("" + value).split("=")[1];
				var dash = span.indexOf("-");

				first = parseInt(span.substring(0, dash), 10);
				last = parseInt(span.substring(dash + 1), 10);
			};
			answer.send = function () {
				var count = last - first + 1;
				var bytes = new Uint8Array(count);

				for (var index = 0; index < count; index++) {
					bytes[index] = (first + index) & 255;
				}

				answer.response = bytes.buffer;
				answer.status = 206;

				var until = performance.now() + 3;
				while (performance.now() < until) {}
			};
			answer.getResponseHeader = function (name) {
				var wanted = ("" + name).toLowerCase();
				if (wanted === "content-range") return "bytes " + first + "-" + last + "/" + total;
				if (wanted === "etag") return "\"slow\"";
				return null;
			};
		};
	}, page, total);
}


/*
**	The last line the stall record wrote, or an empty string when it has written none.
*/
std::string Last_Stall(void)
{
	char line[256];

	line[0] = '\0';

	EM_ASM({
		var state = globalThis.OpenTS_State;
		var stalls = (state && state.stalls) ? state.stalls : [];
		var text = stalls.length > 0 ? "" + stalls[stalls.length - 1] : "";
		var count = 0;

		while (count < text.length && count + 1 < $1) {
			HEAPU8[$0 + count] = text.charCodeAt(count) & 255;
			count++;
		}

		HEAPU8[$0 + count] = 0;
	}, line, (int)sizeof(line));

	return(std::string(line));
}


std::string Stall_Field(std::string const & line, unsigned int index)
{
	std::size_t cursor = 0;

	for (;;) {
		std::size_t stop = line.find(' ', cursor);
		if (stop == std::string::npos) stop = line.size();

		if (index == 0) return(line.substr(cursor, stop - cursor));
		if (stop == line.size()) return(std::string());

		cursor = stop + 1;
		index--;
	}
}


/*
**	------------------------------------------------------------------------------------
**	What a stall says it was waiting for. A read shorter than a block is served out of the
**	whole block that holds it, and the block starts wherever the arithmetic puts it -- as
**	much as a block before the read. Recording the block instead of the read names whatever
**	file the image happens to carry in front of the one being opened, which turns the first
**	read of an archive into a read of the installer sitting ahead of it and sends anyone
**	reading the record after the wrong thing.
**	------------------------------------------------------------------------------------
*/
void Check_Stall_Record(void)
{
	char const * const page = "https://player.example/game/";
	double const total = 1048576.0;

	ISOHttpSourceClass source;

	Serve_Slowly(page, total);

	EM_ASM({
		if (globalThis.OpenTS_State) globalThis.OpenTS_State.stalls = [];
	});

	Check(source.Open("https://mirror.example/discs/slow.iso"), "an image on a slow link opens");

	/*
	**	A read of sixteen bytes a long way into the block that holds them. The block begins
	**	at 65536 and the read at 100000, which is what the record has to tell apart.
	*/
	unsigned char scrap[16];

	Check(source.Read_At(100000, scrap, sizeof(scrap)), "a read shorter than a block is served");

	std::string line = Last_Stall();

	Check(Stall_Field(line, 3) == "100000", "and the stall names the offset the read asked for");
	Check(Stall_Field(line, 4) == "16", "and the length it asked for");
	Check(Stall_Field(line, 6) == "demand", "as a read the engine waited on");

	/*
	**	A read that is already a whole block is fetched as it stands, so the read and the
	**	span are the same thing and the record says so.
	*/
	std::vector<unsigned char> whole(ISO_BLOCK_SIZE);

	Check(source.Read_At(262144, whole.data(), (unsigned int)whole.size()),
		"a read of a whole block is served");

	line = Last_Stall();

	Check(Stall_Field(line, 3) == "262144" && Stall_Field(line, 4) == "65536",
		"and is recorded as the block it both asked for and fetched");

	source.Close();
	Stop_Serving();
}


/*
**	------------------------------------------------------------------------------------
**	What the link costs, and what the read ahead is worth doing because of it. The whole
**	point is that none of it is a constant: the same code reads a disc on this machine and
**	a disc on the far side of the world, and the difference between the two is three orders
**	of magnitude of round trip. The arithmetic that turns a pair of measurements into a
**	window is checked here against both.
**	------------------------------------------------------------------------------------
*/
void Check_Link(void)
{
	ISOLinkClass link;

	Check(!link.Measured() && link.Window() == (unsigned int)ISOLinkClass::WINDOW_MIN,
		"a link nothing has been read over reaches no further than the floor");
	Check(link.Span() == (unsigned int)ISOLinkClass::SPAN_MIN,
		"and asks for no more than one span at a time");
	Check(link.Flights() == (unsigned int)ISOLinkClass::FLIGHTS_MIN,
		"with no more outstanding than a link on this machine would want");

	/*
	**	A disc served from the same machine. The round trip is most of a millisecond and the
	**	rate is a hundred megabytes a second, which is a window of a few blocks: there is
	**	almost nothing in front of the reading for a window to hide.
	*/
	ISOLinkClass local;

	for (unsigned int step = 0; step < 12; step++) {
		local.Note(2048, 0.5);
		local.Note(65536, 1.15);
	}

	Check(local.Measured(), "a few requests are enough to measure a link");
	Check(local.Trip() < 1.0, "a disc on this machine costs well under a millisecond a request");
	Check(local.Window() <= 8, "so the reading is barely got in front of at all");
	Check(local.Span() == (unsigned int)ISOLinkClass::SPAN_MIN,
		"and a request asks for the same span it always did");
	Check(local.Flights() == (unsigned int)ISOLinkClass::FLIGHTS_MIN,
		"with the same number outstanding");

	/*
	**	The same server with forty milliseconds in front of it. A round trip now costs what
	**	twenty blocks take to deliver, so the window has to be about that or it empties
	**	before the request refilling it lands.
	*/
	ISOLinkClass forty;

	for (unsigned int step = 0; step < 20; step++) {
		forty.Note(2048, 40.0);
		forty.Note(65536, 43.2);
	}

	Check(forty.Trip() > 30.0 && forty.Trip() < 50.0, "forty milliseconds is measured as forty");
	Check(forty.Window() > local.Window() * 4,
		"a link with a round trip in front of it is reached much further ahead of");
	Check(forty.Flights() == (unsigned int)ISOLinkClass::FLIGHTS_MIN,
		"one request at a time still fills a link this close");

	/*
	**	And a hundred and fifty, which is a server on another continent. The window is
	**	several times what forty earned, the requests are larger rather than merely more
	**	frequent, and more of them are allowed beside each other because a single stream no
	**	longer fills the link by itself.
	*/
	ISOLinkClass distant;

	for (unsigned int step = 0; step < 20; step++) {
		distant.Note(2048, 150.0);
		distant.Note(65536, 153.2);
	}

	Check(distant.Window() > forty.Window() * 2, "a distant server is reached further ahead of again");
	Check(distant.Span() > forty.Span(), "and is asked for more at a time rather than more often");
	Check(distant.Flights() > forty.Flights(), "with more requests beside each other");
	Check(distant.Reach() > forty.Reach(),
		"and more of a file is worth taking whole rather than paying another trip for");

	Check(distant.Window() <= (unsigned int)ISOLinkClass::WINDOW_MAX
		&& distant.Span() <= (unsigned int)ISOLinkClass::SPAN_MAX
		&& distant.Flights() <= (unsigned int)ISOLinkClass::FLIGHTS_MAX,
		"and none of the three runs past what it is allowed");

	/*
	**	A run that opens an image out of a record has measured nothing yet and would reach
	**	no further than the floor, which on a distant link is the whole problem the window
	**	exists for. What the run before it measured of the same location is a far better
	**	opening guess, and is only an opening guess: a reading of its own takes over.
	*/
	ISOLinkClass seeded;

	seeded.Seed(distant.Trip(), distant.Rate());

	Check(seeded.Measured() && seeded.Window() == distant.Window(),
		"an image opened out of a record reaches as far ahead as the run that wrote it");

	seeded.Seed(0.5, 100000.0);

	Check(seeded.Window() == distant.Window(),
		"and takes on nothing over a reading it already has");

	ISOLinkClass partial;

	partial.Seed(150.0, 0.0);

	Check(!partial.Measured() && partial.Trip() > 100.0,
		"a record naming a round trip and no rate leaves the rate to be measured");

	/*
	**	One request that queued behind another says nothing about the link, and a window
	**	that widened on it would spend the connection on a stall that has already passed.
	**	One that was faster than anything seen so far does say something, and is taken.
	*/
	ISOLinkClass drift;

	for (unsigned int step = 0; step < 20; step++) {
		drift.Note(2048, 40.0);
		drift.Note(65536, 43.2);
	}

	unsigned int const settled = drift.Window();

	drift.Note(2048, 4000.0);
	Check(drift.Window() < settled * 3, "a single stalled request does not open the window wide");

	for (unsigned int step = 0; step < 20; step++) {
		drift.Note(2048, 40.0);
	}
	Check(drift.Trip() < 60.0, "and the reading it stalled behind is left behind within a few more");

	/*
	**	A link that genuinely worsens is followed, or every window behind it is short of what
	**	it now takes to refill one.
	*/
	for (unsigned int step = 0; step < 40; step++) {
		drift.Note(2048, 150.0);
	}
	Check(drift.Trip() > 120.0, "a link that has really slowed down is followed to where it went");
}


/*
**	------------------------------------------------------------------------------------
**	A run the file layer declared. An ISO9660 file is one run of consecutive sectors, so an
**	open says both of the things a block source cannot work out for itself: that the reading
**	is about to go forward, and where it stops. What is checked here is that the declaration
**	costs the two reads of noticing nothing, and that nothing in front of the reading is
**	ever asked for past the end of the file.
**	------------------------------------------------------------------------------------
*/
void Check_Declared_Runs(void)
{
	std::uint64_t const blocks = 20000;
	unsigned int const window = 64;
	unsigned int const span = 16;
	std::uint64_t start = 0;
	std::uint64_t count = 0;

	ISOReadAheadClass file;

	file.Begin(400, 460);
	Check(file.Bounded() && file.Limit() == 460 && file.Cursor() == 400,
		"a declared run starts where the file does");
	Check(file.Continues(400), "and is believed before anything has been read of it");
	Check(file.Span(blocks, window, span, start, count)
		&& start == 400 && count == (std::uint64_t)span,
		"a whole request is open before the file has been read from at all");

	/*
	**	The first read of the file opens a window, where an undeclared run would have needed
	**	a second read to be believed at all. That is a whole round trip off the front of
	**	every file the engine opens.
	*/
	file.Note(400, 400);
	Check(file.Span(blocks, window, span, start, count) && start == 401,
		"the first read of a declared file is fetched ahead of");

	file.Issued(start + count);

	/*
	**	The reading runs on, and nothing in front of it leaves the file.
	*/
	for (std::uint64_t at = 401; at < 440; at++) {
		file.Note(at, at);
		if (file.Span(blocks, window, span, start, count)) {
			Check(start + count <= 460, "nothing is ever asked for past the end of the file");
			file.Issued(start + count);
		}
	}

	Check(file.Edge() == 460, "and the window stops there rather than running into the next one");
	Check(file.Edge() - file.Cursor() < (std::uint64_t)window,
		"so the end of a file costs less over-reading than the middle of one");

	/*
	**	A declared run reaches its whole window from the first read of it. An undeclared one
	**	may reach no further in front of the reading than the reading has covered, since the
	**	only evidence it will carry on is that it has; a declaration is better evidence than
	**	that. Earning the window a block at a time is what left a declared file paying a
	**	round trip per refill, because a file is read far faster than a distant link answers
	**	and so outran its own window every time.
	*/
	ISOReadAheadClass opened;

	opened.Begin(0, 4000);
	opened.Note(0, 0);

	while (opened.Span(blocks, window, span, start, count)) opened.Issued(start + count);

	Check(opened.Edge() >= 1 + (std::uint64_t)window / 2,
		"a declared run reaches a whole window from the first read of it");

	ISOReadAheadClass noticed;

	noticed.Note(0, 0);
	noticed.Note(1, 1);

	while (noticed.Span(blocks, window, span, start, count)) noticed.Issued(start + count);

	Check(noticed.Edge() < opened.Edge() && noticed.Edge() <= 2 + noticed.Cursor(),
		"and one nobody declared reaches no further than it has already covered");

	/*
	**	A reader taking large bites, which is what a movie player is: it tops its buffer up a
	**	few hundred kilobytes at a time. A window smaller than one of those top-ups can never
	**	be in front of the reading at all, so every top-up costs a round trip and the estimate
	**	that produced the small window has no way to find that out. A declared run therefore
	**	reaches past its own reads however little the link was measured to be worth.
	*/
	unsigned int const smallest = (unsigned int)ISOLinkClass::WINDOW_MIN;
	unsigned int const shortest = (unsigned int)ISOLinkClass::SPAN_MIN;

	ISOReadAheadClass movie;

	movie.Begin(0, 8000);
	movie.Note(0, 4);

	Check(movie.Span(blocks, smallest, shortest, start, count) && start == 5 && count > 5,
		"a declared run asks for more than one of the reader's own reads");

	std::uint64_t reached = 0;

	while (movie.Span(blocks, smallest, shortest, start, count)) {
		movie.Issued(start + count);
		reached = start + count;
	}

	Check(reached >= 5 + (std::uint64_t)ISOReadAheadClass::BOUND_MIN / 2,
		"and reaches ahead of it whatever the link was measured to be worth");

	/*
	**	A read that already covers a span of its own is not fetched ahead of when nobody
	**	said where it was going, because the loading that reads that way moves from file to
	**	file. Inside a declared file it is, since the asking cannot leave the file.
	*/
	ISOReadAheadClass bulk;

	bulk.Begin(0, 4000);
	bulk.Note(0, 39);
	Check(bulk.Span(blocks, window, span, start, count) && start == 40,
		"a bulk read inside a declared file is fetched ahead of");

	ISOReadAheadClass loose;

	loose.Note(0, 39);
	loose.Note(40, 79);
	Check(!loose.Span(blocks, window, span, start, count),
		"and the same read with nothing declared about it is not");

	/*
	**	Several runs at once, and a declaration among them. Declaring the file a run is
	**	already following leaves that run alone: the engine opens the same file many times
	**	over, and starting again at the front of it each time would ask for what the reading
	**	had already been given.
	*/
	std::uint64_t lost = 0;
	std::uint64_t stop = 0;

	ISOReadRunsClass runs;

	runs.Declare(1000, 1400);
	runs.Note(1000, 1000, lost, stop);

	Check(runs.Current().Bounded() && runs.Current().Limit() == 1400,
		"the first read inside a declared file takes the file's end with it");
	Check(runs.Current().Span(blocks, window, span, start, count) && start == 1001,
		"and is fetched ahead of from that first read");

	/*
	**	A declaration is remembered and nothing more. The engine declares a file whenever it
	**	opens one and opens far more of them than it reads, so a declaration that took a run
	**	over would spend most of its time throwing away what a stream still being read had
	**	asked for.
	*/
	runs.Current().Issued(start + count);

	std::uint64_t const held = runs.Current().Cursor();
	std::uint64_t const edge = runs.Current().Edge();

	for (unsigned int step = 0; step < 40; step++) {
		runs.Declare(20000 + step * 100, 20000 + step * 100 + 50);
	}

	runs.Note(1001, 1001, lost, stop);
	Check(runs.Current().Cursor() == held + 1 && runs.Current().Edge() == edge,
		"a stream is undisturbed by any number of files being declared beside it");
	Check(runs.Current().Limit() == 1400, "and keeps the end its own file declared");

	/*
	**	The mixfile system declares the whole of an archive and then the one embedded file
	**	it wanted. Both cover the reading, and it is the embedded one that says where it
	**	stops.
	*/
	ISOReadRunsClass nested;

	nested.Declare(0, 100000);
	nested.Declare(5000, 5100);
	nested.Note(5000, 5000, lost, stop);
	Check(nested.Current().Limit() == 5100,
		"a read inside two declarations takes the end of the narrower one");

	/*
	**	A read landing where nothing was declared is the run that has to be noticed, and is
	**	not believed until it has moved forward twice.
	*/
	ISOReadRunsClass stray;

	stray.Declare(0, 100);
	stray.Note(9000, 9000, lost, stop);
	Check(!stray.Current().Bounded() && !stray.Current().Span(blocks, window, span, start, count),
		"a read outside every declaration establishes nothing on its own");
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

	/*
	**	The window and the span the link is worth are the read ahead's inputs rather than
	**	its constants, so the checks below name what a fast link would have measured and a
	**	set of their own names what a slow one would.
	*/
	unsigned int const window = 16;
	unsigned int const span = (unsigned int)ISOLinkClass::SPAN_MIN;

	ISOReadAheadClass ahead;

	Check(!ahead.Span(blocks, window, span, start, count), "nothing is fetched ahead of a source nobody has read");
	Check(!ahead.Continues(500), "and a run nobody has read carries nothing on");

	/*
	**	A read on its own says nothing about where the next one is going, and neither does a
	**	second one somewhere else. Metadata is read in small pieces all over an image, and
	**	pulling a window in after each would cost more than the reads themselves.
	*/
	ahead.Note(500, 500);
	Check(!ahead.Span(blocks, window, span, start, count), "one read establishes no run");

	Check(!ahead.Continues(20), "a read somewhere else is a seek, not a run");
	ahead.Note(20, 20);
	Check(!ahead.Span(blocks, window, span, start, count), "and the reading starts again from where it landed");

	/*
	**	Two reads moving forward are the whole of what a run has to prove. A clip playing in
	**	the sidebar is read in a burst of three blocks and is over before a longer threshold
	**	would have believed it, so the third block has to be on its way before it is read.
	*/
	ahead.Note(700, 700);
	Check(ahead.Continues(701), "a read in the block after the last one continues the run");
	ahead.Note(701, 701);

	Check(ahead.Span(blocks, window, span, start, count), "a second read moving forward is fetched ahead of");
	Check(start == 702, "the span begins where the reading has not reached");
	Check(count == (std::uint64_t)ISOLinkClass::WINDOW_MIN,
		"and opens no wider than the window a young run has earned");

	/*
	**	Asking twice for the same blocks would spend the connection on bytes already on
	**	their way, so what has been asked for is not asked for again until the cursor has
	**	moved past it.
	*/
	ahead.Issued(start + count);
	Check(!ahead.Span(blocks, window, span, start, count), "what has been asked for is not asked for again");

	/*
	**	Most of a movie's reads are shorter than a block and never leave the one before them.
	**	Those hold the run rather than building it, so what the window follows is the reads
	**	that actually move forward.
	*/
	ahead.Note(701, 701);
	ahead.Note(701, 701);
	Check(ahead.Run() == 2, "a read that does not leave its block does not advance the run");

	/*
	**	The window widens as the run proves itself, which is what keeps a short burst cheap
	**	and lets a movie reach far enough ahead to cover a round trip.
	*/
	for (unsigned int step = 0; step < 8; step++) {
		std::uint64_t const at = 702 + step;
		ahead.Note(at, at);
		if (ahead.Span(blocks, window, span, start, count)) ahead.Issued(start + count);
	}

	Check(ahead.Edge() - ahead.Cursor() <= (std::uint64_t)window,
		"a long run never reaches further ahead than the window allows");
	Check(ahead.Edge() - ahead.Cursor() > (std::uint64_t)ISOLinkClass::WINDOW_MIN,
		"but does reach further than a young one");

	/*
	**	A seek makes everything in front of the run it left worthless. The run starts again
	**	from where the seek landed and asks for nothing until it is believed once more.
	*/
	Check(!ahead.Continues(10), "a read out of the run is reported so what is in flight can be let go");
	ahead.Note(10, 10);
	Check(!ahead.Span(blocks, window, span, start, count), "and the window closes until a new run is established");
	Check(ahead.Cursor() == 11, "the run starts again from where the seek landed");

	/*
	**	The image ends, and a request that runs past it would be answered short or refused.
	*/
	ISOReadAheadClass edge;

	for (std::uint64_t at = blocks - 6; at < blocks; at++) {
		edge.Note(at, at);
	}

	if (edge.Span(blocks, window, span, start, count)) {
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
	Check(extent.Span(blocks, window, span, start, count) && start == 6,
		"a run of multi-block reads is followed from where the last of them ended");
	Check(count <= (std::uint64_t)span, "and no one request is larger than a span");

	/*
	**	A read long enough to be a span of its own carries its own round trip and goes out as
	**	one request whatever it covers, so there is nothing in front of it worth guessing at.
	**	That is how a mixfile is loaded, and the loading moves from file to file.
	*/
	ISOReadAheadClass loading;

	loading.Note(0, 39);
	loading.Note(40, 79);
	loading.Note(80, 119);
	Check(!loading.Span(blocks, window, span, start, count),
		"a read that is already a span of its own is not fetched ahead of");

	loading.Note(120, 120);
	Check(loading.Span(blocks, window, span, start, count) && start == 121,
		"and the window opens again as soon as the reads are short enough to need it");

	extent.Reset();
	Check(extent.Cursor() == 0 && extent.Run() == 0 && !extent.Span(blocks, window, span, start, count),
		"a source that is closed forgets where its reading had reached");
}


/*
**	------------------------------------------------------------------------------------
**	Several runs at once. A clip playing in the sidebar streams while the mission around it
**	reads its map, its artwork and its music off the same image, and every one of those
**	reads lands somewhere else. What is checked here is that they stay out of each other's
**	way: a run is only given up when a fifth stream takes it over, and only its own span
**	goes with it.
**	------------------------------------------------------------------------------------
*/
void Check_Read_Runs(void)
{
	std::uint64_t const blocks = 20000;
	unsigned int const window = 16;
	unsigned int const span = (unsigned int)ISOLinkClass::SPAN_MIN;
	std::uint64_t start = 0;
	std::uint64_t count = 0;
	std::uint64_t lost = 0;
	std::uint64_t stop = 0;

	ISOReadRunsClass runs;

	Check(!runs.Note(1014, 1014, lost, stop), "the first read of a set displaces no run");
	Check(!runs.Current().Span(blocks, window, span, start, count), "and one read establishes no run");

	/*
	**	The clip's whole footprint is three blocks read back to back inside one frame. The
	**	third has to be asked for while the second is still being waited on, because there is
	**	no decoding in between for a round trip to hide in.
	*/
	runs.Note(1014, 1015, lost, stop);
	Check(runs.Current().Span(blocks, window, span, start, count) && start == 1016,
		"a clip's second block is enough of a run to ask for the third");
	runs.Current().Issued(start + count);

	/*
	**	The mission reads its artwork from the far side of the same image between the clip's
	**	reads. That is a run of its own rather than the end of the clip's.
	*/
	Check(!runs.Note(10457, 10457, lost, stop), "a read somewhere else takes a run of its own");
	runs.Note(10458, 10458, lost, stop);
	Check(runs.Current().Span(blocks, window, span, start, count) && start == 10459,
		"which earns a window of its own");
	runs.Current().Issued(start + count);

	runs.Note(1016, 1016, lost, stop);
	Check(runs.Current().Run() == 3 && runs.Current().Cursor() == 1017,
		"and the clip carries on where it had reached rather than starting again");
	Check(runs.Current().Edge() == 1018,
		"with what was asked for in front of it still on its way");

	/*
	**	A fifth stream is one more than the set holds. The run that has gone longest without
	**	a read is the one taken over, and only the span in front of that run is given up.
	*/
	runs.Note(3000, 3000, lost, stop);
	runs.Note(4000, 4000, lost, stop);

	lost = 0;
	stop = 0;
	Check(runs.Note(5000, 5000, lost, stop), "a fifth run takes over the one longest unread");
	Check(lost == 10459 && stop == 10459 + (std::uint64_t)ISOLinkClass::WINDOW_MIN,
		"and reports that run's outstanding span, so only those bytes are let go");

	runs.Note(1017, 1017, lost, stop);
	Check(runs.Current().Run() == 4 && runs.Current().Edge() == 1018,
		"the clip's run outlives four other streams reading the same image");

	runs.Reset();
	Check(runs.Current().Run() == 0 && !runs.Current().Span(blocks, window, span, start, count),
		"and a source that is closed forgets every run it was following");
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
	Check_Image_Identity();
	Check_Probe_Record();
	Check_Probe_Recall();
	Check_Stall_Record();
	Check_Link();
	Check_Read_Ahead();
	Check_Read_Runs();
	Check_Declared_Runs();

	std::printf("\n%s\n", Failures == 0 ? "all checks passed" : "checks failed");
	return (Failures == 0) ? 0 : 1;
}
