/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the Portable Executable resource reader against images the test builds for
// itself. No game data, language library or original executable is involved, and none may
// become one.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "peresource.h"

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
 * ------------------------------------------------------------------------------------
 * Byte assembly. Everything in a Portable Executable is little endian, whatever the
 * machine building the image happens to be.
 * ------------------------------------------------------------------------------------
 */
void Put_Short(std::vector<unsigned char> & out, unsigned int value)
{
	out.push_back((unsigned char)(value & 0xFF));
	out.push_back((unsigned char)((value >> 8) & 0xFF));
}


void Put_Long(std::vector<unsigned char> & out, std::uint32_t value)
{
	Put_Short(out, value & 0xFFFF);
	Put_Short(out, (value >> 16) & 0xFFFF);
}


void Patch_Long(std::vector<unsigned char> & out, std::size_t offset, std::uint32_t value)
{
	out[offset + 0] = (unsigned char)(value & 0xFF);
	out[offset + 1] = (unsigned char)((value >> 8) & 0xFF);
	out[offset + 2] = (unsigned char)((value >> 16) & 0xFF);
	out[offset + 3] = (unsigned char)((value >> 24) & 0xFF);
}


void Patch_Short(std::vector<unsigned char> & out, std::size_t offset, unsigned int value)
{
	out[offset + 0] = (unsigned char)(value & 0xFF);
	out[offset + 1] = (unsigned char)((value >> 8) & 0xFF);
}


void Pad_To(std::vector<unsigned char> & out, std::size_t alignment)
{
	while ((out.size() % alignment) != 0) {
		out.push_back(0);
	}
}


/*
 * Text in the resources is UTF-16, so the harness keeps its sample strings as code point
 * sequences rather than as source literals whose encoding would depend on the compiler.
 */
std::vector<std::uint16_t> Wide(char const * text)
{
	std::vector<std::uint16_t> result;

	for (char const * scan = text; *scan != '\0'; scan++) {
		result.push_back((std::uint16_t)(unsigned char)*scan);
	}

	return result;
}


void Put_Wide(std::vector<unsigned char> & out, std::vector<std::uint16_t> const & text, bool terminate)
{
	for (std::size_t index = 0; index < text.size(); index++) {
		Put_Short(out, text[index]);
	}

	if (terminate) {
		Put_Short(out, 0);
	}
}


/*
 * ------------------------------------------------------------------------------------
 * A resource directory builder. Resources are collected as a flat list and sorted into
 * the three level tree -- type, then name, then language -- that the format calls for.
 * ------------------------------------------------------------------------------------
 */
struct ResourceEntryType {
	unsigned int TypeID;
	std::string TypeName;      // Empty when the type is numbered.
	unsigned int NameID;
	std::string NameString;    // Empty when the resource is numbered.
	std::vector<unsigned char> Data;
};


struct DirectoryEntryType {
	unsigned int ID;
	std::string Name;
	std::size_t NamePatch;     // Where the name offset is written, for a named entry.
	std::size_t LinkPatch;     // Where the child or leaf offset is written.
};


bool Same_Type(ResourceEntryType const & left, ResourceEntryType const & right)
{
	return(left.TypeID == right.TypeID && left.TypeName == right.TypeName);
}


/*
 * A directory holds its named entries before its numbered ones, and says how many of each
 * it has. That order is what tells a reader which kind an entry is.
 */
std::size_t Order_Named_First(std::vector<std::size_t> & entries, std::vector<bool> const & named)
{
	std::vector<std::size_t> ordered;

	for (std::size_t pass = 0; pass < 2; pass++) {
		for (std::size_t index = 0; index < entries.size(); index++) {
			if (named[index] == (pass == 0)) ordered.push_back(entries[index]);
		}
	}

	std::size_t count = 0;
	for (std::size_t index = 0; index < named.size(); index++) {
		if (named[index]) count++;
	}

	entries = ordered;
	return count;
}


/*
 * Builds the whole resource section. The offsets inside the tree count from the start of
 * the section, and the leaf records hold relative virtual addresses, so the address the
 * section will be given has to be known here.
 */
std::vector<unsigned char> Build_Resources(std::vector<ResourceEntryType> const & resources, std::uint32_t address)
{
	std::vector<unsigned char> out;
	std::vector<DirectoryEntryType> patches;

	/*
	 * Collect the distinct types, in the order the caller gave them.
	 */
	std::vector<std::size_t> types;
	for (std::size_t index = 0; index < resources.size(); index++) {
		bool seen = false;
		for (std::size_t scan = 0; scan < types.size(); scan++) {
			if (Same_Type(resources[types[scan]], resources[index])) seen = true;
		}
		if (!seen) types.push_back(index);
	}

	std::vector<bool> typenamed;
	for (std::size_t scan = 0; scan < types.size(); scan++) {
		typenamed.push_back(!resources[types[scan]].TypeName.empty());
	}

	std::size_t namedtypes = Order_Named_First(types, typenamed);

	/*
	 * The root directory, one entry per type.
	 */
	Put_Long(out, 0);
	Put_Long(out, 0);
	Put_Short(out, 0);
	Put_Short(out, 0);
	Put_Short(out, (unsigned int)namedtypes);
	Put_Short(out, (unsigned int)(types.size() - namedtypes));

	for (std::size_t scan = 0; scan < types.size(); scan++) {
		ResourceEntryType const & entry = resources[types[scan]];

		DirectoryEntryType patch;
		patch.ID = entry.TypeID;
		patch.Name = entry.TypeName;
		patch.NamePatch = out.size();
		Put_Long(out, 0);
		patch.LinkPatch = out.size();
		Put_Long(out, 0);
		patches.push_back(patch);
	}

	/*
	 * One name directory per type, and one language directory per resource. The language
	 * directories are written straight after the name directory they belong to.
	 */
	std::vector<std::size_t> leafpatch(resources.size(), 0);
	std::vector<DirectoryEntryType> namepatches;

	for (std::size_t scan = 0; scan < types.size(); scan++) {
		Patch_Long(out, patches[scan].LinkPatch, (std::uint32_t)(out.size() | 0x80000000u));

		std::vector<std::size_t> members;
		for (std::size_t index = 0; index < resources.size(); index++) {
			if (Same_Type(resources[types[scan]], resources[index])) members.push_back(index);
		}

		std::vector<bool> membernamed;
		for (std::size_t index = 0; index < members.size(); index++) {
			membernamed.push_back(!resources[members[index]].NameString.empty());
		}

		std::size_t namedmembers = Order_Named_First(members, membernamed);

		Put_Long(out, 0);
		Put_Long(out, 0);
		Put_Short(out, 0);
		Put_Short(out, 0);
		Put_Short(out, (unsigned int)namedmembers);
		Put_Short(out, (unsigned int)(members.size() - namedmembers));

		std::vector<std::size_t> links;
		for (std::size_t member = 0; member < members.size(); member++) {
			ResourceEntryType const & entry = resources[members[member]];

			DirectoryEntryType patch;
			patch.ID = entry.NameID;
			patch.Name = entry.NameString;
			patch.NamePatch = out.size();
			Put_Long(out, 0);
			patch.LinkPatch = out.size();
			Put_Long(out, 0);
			namepatches.push_back(patch);
			links.push_back(members[member]);
		}

		for (std::size_t member = 0; member < members.size(); member++) {
			std::size_t which = namepatches.size() - members.size() + member;
			Patch_Long(out, namepatches[which].LinkPatch, (std::uint32_t)(out.size() | 0x80000000u));

			Put_Long(out, 0);
			Put_Long(out, 0);
			Put_Short(out, 0);
			Put_Short(out, 0);
			Put_Short(out, 0);
			Put_Short(out, 1);

			Put_Long(out, 1033);
			leafpatch[links[member]] = out.size();
			Put_Long(out, 0);
		}
	}

	/*
	 * The name strings the named entries point at.
	 */
	for (std::size_t scan = 0; scan < patches.size(); scan++) {
		if (patches[scan].Name.empty()) {
			Patch_Long(out, patches[scan].NamePatch, patches[scan].ID);
			continue;
		}

		Pad_To(out, 2);
		Patch_Long(out, patches[scan].NamePatch, (std::uint32_t)(out.size() | 0x80000000u));
		Put_Short(out, (unsigned int)patches[scan].Name.size());
		Put_Wide(out, Wide(patches[scan].Name.c_str()), false);
	}

	for (std::size_t scan = 0; scan < namepatches.size(); scan++) {
		if (namepatches[scan].Name.empty()) {
			Patch_Long(out, namepatches[scan].NamePatch, namepatches[scan].ID);
			continue;
		}

		Pad_To(out, 2);
		Patch_Long(out, namepatches[scan].NamePatch, (std::uint32_t)(out.size() | 0x80000000u));
		Put_Short(out, (unsigned int)namepatches[scan].Name.size());
		Put_Wide(out, Wide(namepatches[scan].Name.c_str()), false);
	}

	/*
	 * The data records, then the data itself.
	 */
	Pad_To(out, 4);

	std::vector<std::size_t> datapatch(resources.size(), 0);
	for (std::size_t index = 0; index < resources.size(); index++) {
		Patch_Long(out, leafpatch[index], (std::uint32_t)out.size());
		datapatch[index] = out.size();
		Put_Long(out, 0);
		Put_Long(out, (std::uint32_t)resources[index].Data.size());
		Put_Long(out, 1252);
		Put_Long(out, 0);
	}

	for (std::size_t index = 0; index < resources.size(); index++) {
		Pad_To(out, 4);
		Patch_Long(out, datapatch[index], address + (std::uint32_t)out.size());
		out.insert(out.end(), resources[index].Data.begin(), resources[index].Data.end());
	}

	return out;
}


constexpr std::uint32_t RESOURCE_ADDRESS = 0x1000;
constexpr std::uint32_t FILE_ALIGNMENT = 512;


std::uint32_t Align_Up(std::uint32_t value, std::uint32_t alignment)
{
	return((value + alignment - 1) / alignment * alignment);
}


/*
 * Wraps a resource section in the smallest Portable Executable that carries it: a DOS
 * header pointing at the COFF header, a PE32 optional header naming the resource data
 * directory, and one section.
 */
std::vector<unsigned char> Build_Image(std::vector<unsigned char> const & section)
{
	std::vector<unsigned char> out;

	out.push_back('M');
	out.push_back('Z');
	out.resize(0x3C, 0);
	Put_Long(out, 0x40);

	Put_Long(out, 0x00004550);

	Put_Short(out, 0x014C);        // Machine, i386.
	Put_Short(out, 1);             // NumberOfSections.
	Put_Long(out, 0);              // TimeDateStamp.
	Put_Long(out, 0);              // PointerToSymbolTable.
	Put_Long(out, 0);              // NumberOfSymbols.
	Put_Short(out, 224);           // SizeOfOptionalHeader.
	Put_Short(out, 0x210E);        // Characteristics.

	std::size_t optional = out.size();
	Put_Short(out, 0x010B);        // Magic, PE32.
	out.resize(optional + 92, 0);
	Put_Long(out, 16);             // NumberOfRvaAndSizes.

	for (int index = 0; index < 16; index++) {
		if (index == 2) {
			Put_Long(out, RESOURCE_ADDRESS);
			Put_Long(out, (std::uint32_t)section.size());
		} else {
			Put_Long(out, 0);
			Put_Long(out, 0);
		}
	}

	char const name[8] = {'.', 'r', 's', 'r', 'c', 0, 0, 0};
	out.insert(out.end(), name, name + 8);
	Put_Long(out, (std::uint32_t)section.size());                       // VirtualSize.
	Put_Long(out, RESOURCE_ADDRESS);                                    // VirtualAddress.
	Put_Long(out, Align_Up((std::uint32_t)section.size(), FILE_ALIGNMENT));
	Put_Long(out, FILE_ALIGNMENT);                                      // PointerToRawData.
	Put_Long(out, 0);
	Put_Long(out, 0);
	Put_Short(out, 0);
	Put_Short(out, 0);
	Put_Long(out, 0x40000040);                                          // Characteristics.

	out.resize(FILE_ALIGNMENT, 0);
	out.insert(out.end(), section.begin(), section.end());
	out.resize(FILE_ALIGNMENT + Align_Up((std::uint32_t)section.size(), FILE_ALIGNMENT), 0);

	return out;
}


/*
 * Packs sixteen strings into one bundle the way the string table does: a length in
 * characters followed by that many characters, with an absent string written as a length
 * of zero.
 */
std::vector<unsigned char> Build_String_Bundle(std::vector<std::vector<std::uint16_t>> const & strings)
{
	std::vector<unsigned char> out;

	for (std::size_t index = 0; index < 16; index++) {
		if (index < strings.size()) {
			Put_Short(out, (unsigned int)strings[index].size());
			Put_Wide(out, strings[index], false);
		} else {
			Put_Short(out, 0);
		}
	}

	return out;
}


/*
 * ------------------------------------------------------------------------------------
 * A version resource, which is its own tree of length prefixed nodes.
 * ------------------------------------------------------------------------------------
 */
std::vector<unsigned char> Build_Version_Node(char const * key, std::vector<unsigned char> const & value,
	unsigned int valuecount, unsigned int type, std::vector<std::vector<unsigned char>> const & children)
{
	std::vector<unsigned char> out;

	Put_Short(out, 0);
	Put_Short(out, valuecount);
	Put_Short(out, type);
	Put_Wide(out, Wide(key), true);
	Pad_To(out, 4);

	out.insert(out.end(), value.begin(), value.end());

	for (std::size_t index = 0; index < children.size(); index++) {
		Pad_To(out, 4);
		out.insert(out.end(), children[index].begin(), children[index].end());
	}

	Patch_Short(out, 0, (unsigned int)out.size());
	return out;
}


std::vector<unsigned char> Build_Version_String(char const * key, char const * value)
{
	std::vector<std::uint16_t> text = Wide(value);

	std::vector<unsigned char> encoded;
	Put_Wide(encoded, text, true);

	return Build_Version_Node(key, encoded, (unsigned int)text.size() + 1, 1, {});
}


std::vector<unsigned char> Build_Version_Resource(void)
{
	std::vector<std::vector<unsigned char>> entries;
	entries.push_back(Build_Version_String("InternalName", "Test Resources"));
	entries.push_back(Build_Version_String("FileVersion", "1.23"));

	std::vector<unsigned char> translation = Build_Version_Node("040904B0", {}, 0, 1, entries);
	std::vector<unsigned char> info = Build_Version_Node("StringFileInfo", {}, 0, 1, {translation});

	std::vector<unsigned char> fixed(52, 0);
	return Build_Version_Node("VS_VERSION_INFO", fixed, 52, 0, {info});
}


/*
 * ------------------------------------------------------------------------------------
 * The image every check below reads from.
 * ------------------------------------------------------------------------------------
 */
std::vector<unsigned char> const DIALOG_DATA = {0x01, 0x00, 0xFF, 0xFE, 0x7F, 0x10, 0x00, 0x2A};
std::vector<unsigned char> const CUSTOM_DATA = {'O', 'p', 'e', 'n', 'T', 'S'};


std::vector<ResourceEntryType> Sample_Resources(void)
{
	std::vector<ResourceEntryType> resources;

	/*
	 * Bundle one holds identifiers 0 through 15, bundle three holds 32 through 47. The
	 * gap in between is a string table that does not run from end to end, which is what
	 * the shipped language libraries look like.
	 */
	std::vector<std::vector<std::uint16_t>> first;
	first.push_back(Wide(""));
	first.push_back(Wide("Construction complete."));
	first.push_back(Wide("%s has left the game."));
	first.push_back(std::vector<std::uint16_t>{0x00A9, ' ', '1', '9', '9', '9'});
	first.push_back(std::vector<std::uint16_t>{0x2019, 0x4E2D});

	std::vector<std::vector<std::uint16_t>> third;
	third.resize(5);
	third[4] = Wide("Insufficient funds.");

	resources.push_back({PE_RESOURCE_STRING, "", 1, "", Build_String_Bundle(first)});
	resources.push_back({PE_RESOURCE_STRING, "", 3, "", Build_String_Bundle(third)});
	resources.push_back({PE_RESOURCE_DIALOG, "", 101, "", DIALOG_DATA});
	resources.push_back({PE_RESOURCE_VERSION, "", 1, "", Build_Version_Resource()});
	resources.push_back({0, "OPENTS", 0, "SETTINGS", CUSTOM_DATA});

	return resources;
}


std::vector<unsigned char> Build_Sample_Image(void)
{
	return Build_Image(Build_Resources(Sample_Resources(), RESOURCE_ADDRESS));
}


/*
 * The same resources as a directory that has not been placed in an image, which is what a
 * target with no module loader compiles its language script into. Nothing has moved the
 * addresses in it off the start of the directory, so the section address is zero.
 */
std::vector<unsigned char> Build_Sample_Directory(void)
{
	return Build_Resources(Sample_Resources(), 0);
}


void Check_Strings(PEResourceClass const & library)
{
	char buffer[64];

	Check(library.Fetch_String(1, buffer, sizeof(buffer)) == 22 &&
		std::strcmp(buffer, "Construction complete.") == 0, "string from the first bundle");

	Check(library.Fetch_String(2, buffer, sizeof(buffer)) == 21 &&
		std::strcmp(buffer, "%s has left the game.") == 0, "string with a format specifier");

	Check(library.Fetch_String(36, buffer, sizeof(buffer)) == 19 &&
		std::strcmp(buffer, "Insufficient funds.") == 0, "string from a later bundle");

	Check(library.Fetch_String(0, buffer, sizeof(buffer)) == 0 && buffer[0] == '\0',
		"an empty table entry reads as no string");

	Check(library.Fetch_String(9, buffer, sizeof(buffer)) == 0 && buffer[0] == '\0',
		"an absent entry of a present bundle reads as no string");

	Check(library.Fetch_String(20, buffer, sizeof(buffer)) == 0 && buffer[0] == '\0',
		"an identifier in an absent bundle reads as no string");

	Check(library.Fetch_String(65535, buffer, sizeof(buffer)) == 0,
		"the highest identifier reads as no string");

	/*
	 * The engine works in bytes, and the resource text is Windows-1252 once narrowed.
	 */
	Check(library.Fetch_String(3, buffer, sizeof(buffer)) == 6 &&
		(unsigned char)buffer[0] == 0xA9 && std::strcmp(buffer + 1, " 1999") == 0,
		"a Latin-1 character narrows to its own byte");

	Check(library.Fetch_String(4, buffer, sizeof(buffer)) == 2 &&
		(unsigned char)buffer[0] == 0x92 && buffer[1] == '?',
		"the upper code page narrows, and the unmappable becomes a question mark");

	char small[8];
	Check(library.Fetch_String(1, small, sizeof(small)) == 7 &&
		std::strcmp(small, "Constr") != 0 && std::strlen(small) == 7,
		"a long string is truncated to the buffer and terminated");

	Check(library.Fetch_String(1, small, 1) == 0 && small[0] == '\0',
		"a buffer with room for the terminator alone stays empty");
}


void Check_Resources(PEResourceClass const & library)
{
	std::size_t size = 0;

	void const * dialog = library.Fetch_Resource(PE_RESOURCE_DIALOG, 101u, &size);
	Check(dialog != nullptr && size == DIALOG_DATA.size() &&
		std::memcmp(dialog, DIALOG_DATA.data(), DIALOG_DATA.size()) == 0,
		"a numbered resource of another type comes back whole");

	void const * custom = library.Fetch_Resource("OPENTS", "SETTINGS", &size);
	Check(custom != nullptr && size == CUSTOM_DATA.size() &&
		std::memcmp(custom, CUSTOM_DATA.data(), CUSTOM_DATA.size()) == 0,
		"a named resource of a named type comes back whole");

	Check(custom != nullptr && library.Fetch_Resource("opents", "settings", &size) == custom,
		"a resource name matches without regard to case");

	size = 12345;
	Check(library.Fetch_Resource(PE_RESOURCE_DIALOG, 4242u, &size) == nullptr && size == 0,
		"an absent resource reports nothing rather than empty data");

	size = 12345;
	Check(library.Fetch_Resource(99u, 101u, &size) == nullptr && size == 0,
		"an absent resource type reports nothing");

	size = 12345;
	Check(library.Fetch_Resource("OPENTS", 101u, &size) == nullptr && size == 0,
		"a numbered lookup does not match a named resource");

	size = 12345;
	Check(library.Fetch_Resource(PE_RESOURCE_DIALOG, "101", &size) == nullptr && size == 0,
		"a named lookup does not match a numbered resource");
}


void Check_Version(PEResourceClass const & library)
{
	char buffer[64];

	Check(library.Fetch_Version_String("InternalName", buffer, sizeof(buffer)) &&
		std::strcmp(buffer, "Test Resources") == 0, "a version entry reads back");

	Check(library.Fetch_Version_String("fileversion", buffer, sizeof(buffer)) &&
		std::strcmp(buffer, "1.23") == 0, "a version entry key matches without regard to case");

	Check(!library.Fetch_Version_String("ProductName", buffer, sizeof(buffer)) && buffer[0] == '\0',
		"an absent version entry reports nothing");
}


/*
 * An image that is not one, or one that has been damaged, has to be turned away rather
 * than walked. Every case here is rejected by the reader loading nothing at all.
 */
void Check_Rejection(std::vector<unsigned char> const & good)
{
	PEResourceClass library;

	Check(!library.Load(nullptr, 0), "no image loads nothing");
	Check(!library.Load(good.data(), 4), "an image shorter than its headers loads nothing");

	std::vector<unsigned char> damaged = good;
	damaged[0] = 'Z';
	Check(!library.Load(damaged.data(), damaged.size()), "an image without the DOS mark loads nothing");

	damaged = good;
	Patch_Long(damaged, 0x3C, 0x10000000);
	Check(!library.Load(damaged.data(), damaged.size()), "a header offset past the end loads nothing");

	damaged = good;
	Patch_Long(damaged, 0x40, 0);
	Check(!library.Load(damaged.data(), damaged.size()), "an image without the PE mark loads nothing");

	damaged = good;
	Patch_Long(damaged, 0x40 + 4 + 20 + 96 + 2 * 8, 0);
	Check(!library.Load(damaged.data(), damaged.size()), "an image without resources loads nothing");

	damaged = good;
	Patch_Long(damaged, 0x40 + 4 + 20 + 96 + 2 * 8, 0x900000);
	Check(!library.Load(damaged.data(), damaged.size()),
		"a resource directory outside every section loads nothing");

	/*
	 * A directory that survives loading but points its data past the end of the file must
	 * still refuse to hand that data out.
	 */
	damaged = good;
	Patch_Short(damaged, FILE_ALIGNMENT + 14, 0xFFFF);
	Check(library.Load(damaged.data(), damaged.size()), "a damaged tree can still load");
	Check(library.Fetch_Resource(PE_RESOURCE_STRING, 1u) == nullptr,
		"a directory claiming more entries than fit fetches nothing");
}

}


int main(void)
{
	std::vector<unsigned char> image = Build_Sample_Image();
	std::printf("Synthetic image built: %u bytes\n\n", (unsigned int)image.size());

	PEResourceClass library;
	Check(library.Load(image.data(), image.size()), "the synthetic image loads");
	Check(library.Is_Loaded(), "the reader reports itself loaded");

	if (library.Is_Loaded()) {
		Check_Strings(library);
		Check_Resources(library);
		Check_Version(library);
	} else {
		Failures++;
	}

	library.Unload();
	Check(!library.Is_Loaded(), "the reader reports itself unloaded");

	char buffer[16];
	Check(library.Fetch_String(1, buffer, sizeof(buffer)) == 0,
		"an unloaded reader fetches no string");
	Check(library.Fetch_Resource(PE_RESOURCE_DIALOG, 101u) == nullptr,
		"an unloaded reader fetches no resource");

	Check_Rejection(image);

	/*
	 * The same resources reached through the other way in, which the WebAssembly target
	 * uses because it carries a directory rather than loading a library.
	 */
	std::vector<unsigned char> directory = Build_Sample_Directory();
	std::printf("\nBare directory built: %u bytes\n\n", (unsigned int)directory.size());

	PEResourceClass bare;
	Check(!bare.Load_Directory(nullptr, 0), "no directory loads nothing");
	Check(!bare.Load_Directory(directory.data(), 8), "a directory shorter than its head loads nothing");
	Check(bare.Load_Directory(directory.data(), directory.size()), "the bare directory loads");

	if (bare.Is_Loaded()) {
		Check_Strings(bare);
		Check_Resources(bare);
		Check_Version(bare);
	} else {
		Failures++;
	}

	std::printf("\n%s\n", (Failures == 0) ? "All checks passed." : "Checks FAILED.");
	return((Failures == 0) ? 0 : 1);
}
