/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Walks the resource directory of a Portable Executable image. The layout is the one
// described by the PE specification: a DOS stub pointing at the COFF header, an optional
// header naming the resource data directory, and a section table that turns the relative
// virtual addresses inside the tree back into offsets in the file.

#include "peresource.h"

#include <cstring>

namespace {

/*
 * Offsets within the headers. They are read one field at a time rather than through a
 * structure, so that the reader does not depend on the packing or the alignment any
 * particular compiler would give the Windows declarations.
  */
constexpr std::size_t DOS_PE_OFFSET_FIELD = 0x3C;
constexpr std::size_t DOS_HEADER_SIZE = 0x40;

constexpr std::size_t COFF_SECTION_COUNT = 2;
constexpr std::size_t COFF_OPTIONAL_SIZE = 16;
constexpr std::size_t COFF_HEADER_SIZE = 20;

constexpr std::size_t OPTIONAL_MAGIC = 0;
constexpr std::size_t OPTIONAL32_DIRECTORY_COUNT = 92;
constexpr std::size_t OPTIONAL64_DIRECTORY_COUNT = 108;

constexpr std::size_t SECTION_HEADER_SIZE = 40;
constexpr std::size_t SECTION_VIRTUAL_SIZE = 8;
constexpr std::size_t SECTION_VIRTUAL_ADDRESS = 12;
constexpr std::size_t SECTION_RAW_SIZE = 16;
constexpr std::size_t SECTION_RAW_OFFSET = 20;

constexpr std::size_t RESOURCE_DIRECTORY_SIZE = 16;
constexpr std::size_t RESOURCE_NAMED_COUNT = 12;
constexpr std::size_t RESOURCE_ID_COUNT = 14;
constexpr std::size_t RESOURCE_ENTRY_SIZE = 8;
constexpr std::size_t RESOURCE_DATA_SIZE = 16;

constexpr std::uint16_t DOS_SIGNATURE = 0x5A4D;
constexpr std::uint32_t PE_SIGNATURE = 0x00004550;
constexpr std::uint16_t OPTIONAL_MAGIC_32 = 0x010B;
constexpr std::uint16_t OPTIONAL_MAGIC_64 = 0x020B;

constexpr std::uint32_t RESOURCE_DIRECTORY_INDEX = 2;
constexpr std::uint32_t HIGH_BIT = 0x80000000u;

constexpr int STRINGS_PER_BUNDLE = 16;


std::uint16_t Fetch_Short_At(unsigned char const * data)
{
	return((std::uint16_t)(data[0] | (data[1] << 8)));
}


/*
 * The resource text is UTF-16, and the engine works in single bytes throughout. The
 * language libraries are Windows-1252, which is Latin-1 apart from the range 0x80 to
 * 0x9F; a character that code page has no byte for becomes a question mark rather than
 * silently losing its high half.
 */
char Narrow_Character(std::uint16_t code)
{
	constexpr std::uint16_t UNMAPPED = 0xFFFF;
	constexpr std::uint16_t HIGH_RANGE[32] = {
		0x20AC, UNMAPPED, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, UNMAPPED, 0x017D, UNMAPPED,
		UNMAPPED, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, UNMAPPED, 0x017E, 0x0178
	};

	if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
		return((char)code);
	}

	for (int index = 0; index < 32; index++) {
		if (HIGH_RANGE[index] == code) {
			return((char)(0x80 + index));
		}
	}

	return('?');
}


/*
 * Copies UTF-16 text into a byte buffer, truncating rather than overrunning. The result
 * is terminated whenever the buffer has room for a terminator at all.
 */
int Copy_Text(unsigned char const * text, std::size_t length, char * buffer, int size)
{
	if (buffer == nullptr || size <= 0) {
		return(0);
	}

	std::size_t room = (std::size_t)size - 1;
	if (length > room) {
		length = room;
	}

	for (std::size_t index = 0; index < length; index++) {
		buffer[index] = Narrow_Character(Fetch_Short_At(text + index * 2));
	}

	buffer[length] = '\0';
	return((int)length);
}


/*
 * Compares a name held in the resource directory, which is UTF-16 and carries its own
 * length, against a byte string. Windows matches resource names without regard to case
 * and so does this.
 */
bool Name_Matches(unsigned char const * name, std::size_t length, char const * wanted)
{
	for (std::size_t index = 0; index < length; index++) {
		std::uint16_t code = Fetch_Short_At(name + index * 2);
		char letter = wanted[index];

		if (letter == '\0') {
			return(false);
		}

		if (code >= 'a' && code <= 'z') {
			code = (std::uint16_t)(code - ('a' - 'A'));
		}

		if (letter >= 'a' && letter <= 'z') {
			letter = (char)(letter - ('a' - 'A'));
		}

		if (code != (std::uint16_t)(unsigned char)letter) {
			return(false);
		}
	}

	return(wanted[length] == '\0');
}


std::size_t Align_Long(std::size_t offset)
{
	return((offset + 3) & ~(std::size_t)3);
}


/*
 * One node of a version resource. The format is a tree of length-prefixed nodes, each with
 * a UTF-16 key, an optional value, and any number of children packed after it. Every node
 * and every value starts on a four byte boundary measured from the start of the resource.
  */
struct VersionNodeType {
	std::size_t Value;
	std::size_t ValueBytes;
	std::size_t Children;
	std::size_t End;
	std::size_t KeyOffset;
	std::size_t KeyLength;
};


bool Parse_Version_Node(unsigned char const * block, std::size_t size, std::size_t offset, VersionNodeType & node)
{
	if (offset + 6 > size || (offset & 3) != 0) {
		return(false);
	}

	std::size_t length = Fetch_Short_At(block + offset);
	std::size_t words = Fetch_Short_At(block + offset + 2);
	std::uint16_t type = Fetch_Short_At(block + offset + 4);

	if (length < 6 || offset + length > size) {
		return(false);
	}

	node.End = offset + length;
	node.KeyOffset = offset + 6;

	std::size_t scan = node.KeyOffset;
	while (scan + 2 <= node.End && Fetch_Short_At(block + scan) != 0) {
		scan += 2;
	}

	if (scan + 2 > node.End) {
		return(false);
	}

	node.KeyLength = (scan - node.KeyOffset) / 2;

	/*
	 * A text value counts its length in characters; a binary one counts bytes.
	 */
	node.Value = Align_Long(scan + 2);
	node.ValueBytes = (type == 1) ? words * 2 : words;

	if (node.Value > node.End || node.ValueBytes > node.End - node.Value) {
		return(false);
	}

	node.Children = Align_Long(node.Value + node.ValueBytes);
	return(true);
}


/*
 * Finds the child of a version node with the given key, or the first child of any name
 * when no key is wanted.
 */
bool Find_Version_Child(unsigned char const * block, std::size_t size, VersionNodeType const & parent,
	char const * key, VersionNodeType & child)
{
	std::size_t offset = parent.Children;

	while (offset < parent.End) {
		if (!Parse_Version_Node(block, size, offset, child)) {
			return(false);
		}

		if (key == nullptr || Name_Matches(block + child.KeyOffset, child.KeyLength, key)) {
			return(true);
		}

		std::size_t next = Align_Long(child.End);
		if (next <= offset) {
			return(false);
		}

		offset = next;
	}

	return(false);
}

}


PEResourceClass::PEResourceClass(void) :
	Image(nullptr),
	Size(0),
	SectionOffset(0),
	SectionCount(0),
	DirectoryOffset(0)
{
}


PEResourceClass::~PEResourceClass(void)
{
	Unload();
}


void PEResourceClass::Unload(void)
{
	delete [] Image;
	Image = nullptr;
	Size = 0;
	SectionOffset = 0;
	SectionCount = 0;
	DirectoryOffset = 0;
}


bool PEResourceClass::Load(void const * image, std::size_t size)
{
	Unload();

	if (image == nullptr || size < DOS_HEADER_SIZE) {
		return(false);
	}

	Image = new unsigned char[size];
	std::memcpy(Image, image, size);
	Size = size;

	if (!Locate_Directory()) {
		Unload();
		return(false);
	}

	return(true);
}


/*
 * Walks the headers far enough to find the resource directory and the section table that
 * the addresses inside it are resolved against.
 */
bool PEResourceClass::Locate_Directory(void)
{
	if (Fetch_Short(0) != DOS_SIGNATURE) {
		return(false);
	}

	std::size_t header = Fetch_Long(DOS_PE_OFFSET_FIELD);
	if (!Is_Within(header, COFF_HEADER_SIZE + 4) || Fetch_Long(header) != PE_SIGNATURE) {
		return(false);
	}

	std::size_t coff = header + 4;
	SectionCount = Fetch_Short(coff + COFF_SECTION_COUNT);

	std::size_t optionalsize = Fetch_Short(coff + COFF_OPTIONAL_SIZE);
	std::size_t optional = coff + COFF_HEADER_SIZE;

	SectionOffset = optional + optionalsize;
	if (!Is_Within(SectionOffset, (std::size_t)SectionCount * SECTION_HEADER_SIZE)) {
		return(false);
	}

	std::size_t countfield = 0;
	std::uint16_t magic = Fetch_Short(optional + OPTIONAL_MAGIC);

	if (magic == OPTIONAL_MAGIC_32) {
		countfield = OPTIONAL32_DIRECTORY_COUNT;
	} else if (magic == OPTIONAL_MAGIC_64) {
		countfield = OPTIONAL64_DIRECTORY_COUNT;
	} else {
		return(false);
	}

	std::size_t directories = Fetch_Long(optional + countfield);
	if (directories <= RESOURCE_DIRECTORY_INDEX) {
		return(false);
	}

	std::size_t entry = optional + countfield + 4 + RESOURCE_DIRECTORY_INDEX * 8;
	if (!Is_Within(entry, 8) || entry + 8 > SectionOffset) {
		return(false);
	}

	std::uint32_t rva = Fetch_Long(entry);
	if (rva == 0) {
		return(false);
	}

	DirectoryOffset = Offset_For_RVA(rva);
	return(Is_Within(DirectoryOffset, RESOURCE_DIRECTORY_SIZE));
}


/*
 * Turns a relative virtual address into an offset in the file by finding the section that
 * covers it. Zero means the address falls outside every section.
 */
std::size_t PEResourceClass::Offset_For_RVA(std::uint32_t rva) const
{
	for (unsigned int index = 0; index < SectionCount; index++) {
		std::size_t section = SectionOffset + (std::size_t)index * SECTION_HEADER_SIZE;

		std::uint32_t address = Fetch_Long(section + SECTION_VIRTUAL_ADDRESS);
		std::uint32_t virtualsize = Fetch_Long(section + SECTION_VIRTUAL_SIZE);
		std::uint32_t rawsize = Fetch_Long(section + SECTION_RAW_SIZE);
		std::uint32_t rawoffset = Fetch_Long(section + SECTION_RAW_OFFSET);

		std::uint32_t span = (virtualsize > rawsize) ? virtualsize : rawsize;

		if (rva >= address && rva - address < span) {
			std::uint32_t within = rva - address;

			/*
			 * A section can be longer in memory than on disk, and the tail that is not in
			 * the file reads as zeroes rather than as bytes of the next section.
			 */
			if (within >= rawsize) {
				return(0);
			}

			return((std::size_t)rawoffset + within);
		}
	}

	return(0);
}


bool PEResourceClass::Is_Within(std::size_t offset, std::size_t length) const
{
	if (Image == nullptr || offset == 0 || offset > Size) {
		return(false);
	}

	return(length <= Size - offset);
}


std::uint16_t PEResourceClass::Fetch_Short(std::size_t offset) const
{
	if (Image == nullptr || offset + 2 > Size) {
		return(0);
	}

	return(Fetch_Short_At(Image + offset));
}


std::uint32_t PEResourceClass::Fetch_Long(std::size_t offset) const
{
	if (Image == nullptr || offset + 4 > Size) {
		return(0);
	}

	return((std::uint32_t)Fetch_Short_At(Image + offset) | ((std::uint32_t)Fetch_Short_At(Image + offset + 2) << 16));
}


/*
 * Locates the entry of a resource directory that carries the wanted name or number. The
 * named entries come first and the numbered ones after them, which is what tells the two
 * kinds apart.
 */
std::size_t PEResourceClass::Find_Entry(std::size_t directory, PEResourceNameClass const & wanted) const
{
	if (!Is_Within(directory, RESOURCE_DIRECTORY_SIZE)) {
		return(0);
	}

	unsigned int named = Fetch_Short(directory + RESOURCE_NAMED_COUNT);
	unsigned int numbered = Fetch_Short(directory + RESOURCE_ID_COUNT);

	std::size_t entries = directory + RESOURCE_DIRECTORY_SIZE;
	if (!Is_Within(entries, ((std::size_t)named + numbered) * RESOURCE_ENTRY_SIZE)) {
		return(0);
	}

	for (unsigned int index = 0; index < named + numbered; index++) {
		std::size_t entry = entries + (std::size_t)index * RESOURCE_ENTRY_SIZE;
		std::uint32_t name = Fetch_Long(entry);

		if (index < named) {
			if (!wanted.Is_Named() || (name & HIGH_BIT) == 0) continue;

			std::size_t text = DirectoryOffset + (name & ~HIGH_BIT);
			if (!Is_Within(text, 2)) continue;

			std::size_t length = Fetch_Short(text);
			if (!Is_Within(text + 2, length * 2)) continue;

			if (Name_Matches(Image + text + 2, length, wanted.Get_Name())) {
				return(entry);
			}

		} else {

			if (wanted.Is_Named()) continue;

			if (name == wanted.Get_ID()) {
				return(entry);
			}
		}
	}

	return(0);
}


std::size_t PEResourceClass::First_Entry(std::size_t directory) const
{
	if (!Is_Within(directory, RESOURCE_DIRECTORY_SIZE)) {
		return(0);
	}

	unsigned int count = Fetch_Short(directory + RESOURCE_NAMED_COUNT);
	count += Fetch_Short(directory + RESOURCE_ID_COUNT);

	if (count == 0) {
		return(0);
	}

	std::size_t entry = directory + RESOURCE_DIRECTORY_SIZE;
	return(Is_Within(entry, RESOURCE_ENTRY_SIZE) ? entry : 0);
}


std::size_t PEResourceClass::Sub_Directory(std::size_t entry) const
{
	std::uint32_t link = Fetch_Long(entry + 4);
	if ((link & HIGH_BIT) == 0) {
		return(0);
	}

	std::size_t directory = DirectoryOffset + (link & ~HIGH_BIT);
	return(Is_Within(directory, RESOURCE_DIRECTORY_SIZE) ? directory : 0);
}


std::size_t PEResourceClass::Fetch_Leaf(std::size_t entry, std::size_t * size) const
{
	std::uint32_t link = Fetch_Long(entry + 4);
	if ((link & HIGH_BIT) != 0) {
		return(0);
	}

	std::size_t data = DirectoryOffset + link;
	if (!Is_Within(data, RESOURCE_DATA_SIZE)) {
		return(0);
	}

	std::size_t offset = Offset_For_RVA(Fetch_Long(data));
	std::size_t length = Fetch_Long(data + 4);

	if (!Is_Within(offset, length)) {
		return(0);
	}

	if (size != nullptr) {
		*size = length;
	}

	return(offset);
}


void const * PEResourceClass::Fetch_Resource(PEResourceNameClass const & type, PEResourceNameClass const & name,
	std::size_t * size) const
{
	if (size != nullptr) {
		*size = 0;
	}

	if (!Is_Loaded()) {
		return(nullptr);
	}

	std::size_t typeentry = Find_Entry(DirectoryOffset, type);
	if (typeentry == 0) {
		return(nullptr);
	}

	std::size_t names = Sub_Directory(typeentry);
	if (names == 0) {
		return(nullptr);
	}

	std::size_t nameentry = Find_Entry(names, name);
	if (nameentry == 0) {
		return(nullptr);
	}

	/*
	 * The third level selects a language. The shipped language libraries hold one
	 * translation apiece and the engine picks the library rather than the locale, so the
	 * first entry is the wanted one.
	 */
	std::size_t languages = Sub_Directory(nameentry);
	std::size_t leaf = (languages != 0) ? First_Entry(languages) : nameentry;

	if (leaf == 0) {
		return(nullptr);
	}

	std::size_t offset = Fetch_Leaf(leaf, size);
	return((offset != 0) ? (void const *)(Image + offset) : nullptr);
}


/*
 * The string table is stored in bundles of sixteen. The bundle holding a string is
 * numbered from the identifier, and within it each string is a length in characters
 * followed by that many characters, present or not.
 */
int PEResourceClass::Fetch_String(unsigned int id, char * buffer, int size) const
{
	if (buffer == nullptr || size <= 0) {
		return(0);
	}

	buffer[0] = '\0';

	std::size_t bundlesize = 0;
	unsigned char const * bundle = (unsigned char const *)Fetch_Resource(PE_RESOURCE_STRING,
		id / STRINGS_PER_BUNDLE + 1, &bundlesize);

	if (bundle == nullptr) {
		return(0);
	}

	std::size_t offset = 0;

	for (int index = 0; index < STRINGS_PER_BUNDLE; index++) {
		if (offset + 2 > bundlesize) {
			return(0);
		}

		std::size_t length = Fetch_Short_At(bundle + offset);
		offset += 2;

		if (length > (bundlesize - offset) / 2) {
			return(0);
		}

		if (index == (int)(id % STRINGS_PER_BUNDLE)) {
			return(Copy_Text(bundle + offset, length, buffer, size));
		}

		offset += length * 2;
	}

	return(0);
}


bool PEResourceClass::Fetch_Version_String(char const * key, char * buffer, int size) const
{
	if (key == nullptr || buffer == nullptr || size <= 0) {
		return(false);
	}

	buffer[0] = '\0';

	std::size_t blocksize = 0;
	unsigned char const * block = (unsigned char const *)Fetch_Resource(PE_RESOURCE_VERSION, 1u, &blocksize);

	if (block == nullptr) {
		return(false);
	}

	VersionNodeType root;
	VersionNodeType info;
	VersionNodeType translation;
	VersionNodeType entry;

	if (!Parse_Version_Node(block, blocksize, 0, root)) {
		return(false);
	}

	/*
	 * The translation the engine reports is the first one the library carries, which is
	 * the one the Windows build asks for after reading it out of the variable file
	 * information.
	 */
	if (!Find_Version_Child(block, blocksize, root, "StringFileInfo", info)) {
		return(false);
	}

	if (!Find_Version_Child(block, blocksize, info, nullptr, translation)) {
		return(false);
	}

	if (!Find_Version_Child(block, blocksize, translation, key, entry)) {
		return(false);
	}

	/*
	 * A text value counts its own terminator, which lands in the buffer as an early stop
	 * and leaves the string the caller wanted.
	 */
	Copy_Text(block + entry.Value, entry.ValueBytes / 2, buffer, size);
	return(true);
}
