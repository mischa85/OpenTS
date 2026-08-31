/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// This file deliberately uses nothing but the standard library so that the parser builds
// for the browser target and for a native test harness alongside the Win32 engine.

#include "iso9660.h"

#include <cstring>

namespace {

/*
 * An image can be larger than the two gigabytes a 32-bit long addresses, and the engine
 * builds with a 32-bit long on both supported targets.
 */
int Seek_64(std::FILE * file, std::int64_t offset, int origin)
{
#ifdef _MSC_VER
	return(_fseeki64(file, offset, origin));
#else
	return(fseeko(file, (off_t)offset, origin));
#endif
}


std::int64_t Tell_64(std::FILE * file)
{
#ifdef _MSC_VER
	return(_ftelli64(file));
#else
	return((std::int64_t)ftello(file));
#endif
}


/*
 * ECMA-119 records every multi-byte number twice, little endian first and big endian
 * second. Some masterers leave one half zero, so the little endian half is taken when it
 * carries a value and the big endian half only stands in when it does not.
 */
std::uint32_t Both_Endian_32(unsigned char const * field)
{
	std::uint32_t little = (std::uint32_t)field[0]
		| ((std::uint32_t)field[1] << 8)
		| ((std::uint32_t)field[2] << 16)
		| ((std::uint32_t)field[3] << 24);

	if (little != 0) return(little);

	return((std::uint32_t)field[7]
		| ((std::uint32_t)field[6] << 8)
		| ((std::uint32_t)field[5] << 16)
		| ((std::uint32_t)field[4] << 24));
}


std::uint32_t Both_Endian_16(unsigned char const * field)
{
	std::uint32_t little = (std::uint32_t)field[0] | ((std::uint32_t)field[1] << 8);

	if (little != 0) return(little);

	return((std::uint32_t)field[3] | ((std::uint32_t)field[2] << 8));
}


char Fold(char letter)
{
	if (letter >= 'a' && letter <= 'z') {
		return((char)(letter - ('a' - 'A')));
	}
	return(letter);
}


/*
 * Reduces a directory identifier to the name a caller would ask for: the version suffix
 * and the padding period that ECMA-119 requires on an extensionless name are dropped.
 */
std::string Plain_Name(unsigned char const * identifier, unsigned int length)
{
	std::string name;

	for (unsigned int index = 0; index < length; index++) {
		char letter = (char)identifier[index];
		if (letter == ';') break;
		name.push_back(letter);
	}

	if (!name.empty() && name.back() == '.') {
		name.pop_back();
	}

	return(name);
}


/*
 * Turns the seven byte recording timestamp into the packed form FileClass reports. A
 * timestamp the disc left unset, or one that predates the packed form's 1980 epoch, has
 * no representation and is reported as no date at all.
 */
unsigned int Record_Date_Time(unsigned char const * stamp)
{
	int year = 1900 + (int)stamp[0];
	int month = (int)stamp[1];
	int day = (int)stamp[2];
	int hour = (int)stamp[3];
	int minute = (int)stamp[4];
	int second = (int)stamp[5];

	if (year < 1980 || year > 2107) return(0);
	if (month < 1 || month > 12) return(0);
	if (day < 1 || day > 31) return(0);
	if (hour > 23 || minute > 59 || second > 59) return(0);

	return(((unsigned int)(year - 1980) << 25)
		| ((unsigned int)month << 21)
		| ((unsigned int)day << 16)
		| ((unsigned int)hour << 11)
		| ((unsigned int)minute << 5)
		| ((unsigned int)(second / 2)));
}

} // namespace


ISOLocalFileSourceClass::ISOLocalFileSourceClass(void) :
	File(nullptr),
	Length(0)
{
}


ISOLocalFileSourceClass::~ISOLocalFileSourceClass(void)
{
	Close();
}


/// <summary>Opens a local image file for reading.</summary>
/// <param name="filename">Path of the image file.</param>
/// <returns>bool; Was the file opened and its length determined?</returns>
bool ISOLocalFileSourceClass::Open(char const * filename)
{
	Close();

	if (filename == nullptr || *filename == '\0') return(false);

	File = std::fopen(filename, "rb");
	if (File == nullptr) return(false);

	if (Seek_64(File, 0, SEEK_END) != 0) {
		Close();
		return(false);
	}

	std::int64_t length = Tell_64(File);
	if (length < 0) {
		Close();
		return(false);
	}

	Length = (std::uint64_t)length;
	return(true);
}


void ISOLocalFileSourceClass::Close(void)
{
	if (File != nullptr) {
		std::fclose(File);
		File = nullptr;
	}
	Length = 0;
}


bool ISOLocalFileSourceClass::Read_At(std::uint64_t offset, void * buffer, unsigned int length)
{
	if (File == nullptr || buffer == nullptr) return(false);
	if (length == 0) return(true);
	if (offset > Length || Length - offset < length) return(false);

	if (Seek_64(File, (std::int64_t)offset, SEEK_SET) != 0) return(false);

	return(std::fread(buffer, 1, length, File) == length);
}


ISOVolumeClass::ISOVolumeClass(void) :
	HasJoliet(false)
{
}


ISOVolumeClass::~ISOVolumeClass(void)
{
	Close();
}


/// <summary>Mounts an image held in a local file.</summary>
/// <param name="filename">Path of the image file.</param>
/// <returns>bool; Does the file hold a volume this reader understands?</returns>
bool ISOVolumeClass::Open(char const * filename)
{
	std::unique_ptr<ISOLocalFileSourceClass> source(new ISOLocalFileSourceClass);

	if (!source->Open(filename)) return(false);

	return(Attach(std::move(source)));
}


/// <summary>Mounts an image served by the block source supplied.</summary>
/// <param name="source">The block source, whose ownership passes to the volume.</param>
/// <returns>bool; Does the source hold a volume this reader understands?</returns>
bool ISOVolumeClass::Attach(std::unique_ptr<ISOBlockSourceClass> source)
{
	Close();

	if (!source) return(false);

	Source = std::move(source);
	Cache.resize(ISO_SECTOR_CACHE_COUNT);
	for (CacheEntryType & entry : Cache) {
		entry.Block = 0;
		entry.IsValid = false;
	}

	if (!Parse_Descriptors()) {
		Close();
		return(false);
	}

	return(true);
}


void ISOVolumeClass::Close(void)
{
	Source.reset();
	Cache.clear();
	RootEntry.Reset();
	VolumeName.clear();
	HasJoliet = false;
}


/*
 * Walks the volume descriptor set, taking the root directory from the primary descriptor
 * and noting whether a Joliet supplementary descriptor is present.
 */
bool ISOVolumeClass::Parse_Descriptors(void)
{
	bool found = false;

	for (unsigned int index = 0; index < ISO_MAX_DESCRIPTORS; index++) {
		unsigned char const * sector = Sector(ISO_FIRST_DESCRIPTOR + index);
		if (sector == nullptr) break;

		if (std::memcmp(sector + 1, "CD001", 5) != 0) break;

		unsigned int type = sector[0];
		if (type == ISO_DESCRIPTOR_TERMINATOR) break;

		if (type == ISO_DESCRIPTOR_SUPPLEMENTARY) {
			/*
			**	Joliet announces itself with one of three escape sequences naming a UCS-2
			**	level. The names it carries are not used, but a caller may want to know the
			**	disc offers them.
			*/
			unsigned char const * escape = sector + 88;
			if (escape[0] == '%' && escape[1] == '/'
				&& (escape[2] == '@' || escape[2] == 'C' || escape[2] == 'E')) {
				HasJoliet = true;
			}
			continue;
		}

		if (type != ISO_DESCRIPTOR_PRIMARY || found) continue;

		/*
		**	Only the 2048 byte logical block is supported. Anything else would misplace
		**	every extent, so the volume is refused rather than misread.
		*/
		if (Both_Endian_16(sector + 128) != ISO_SECTOR_SIZE) return(false);

		unsigned char const * record = sector + 156;
		if (record[0] < ISO_MIN_RECORD_SIZE) return(false);

		RootEntry.Reset();
		RootEntry.IsDirectory = true;
		RootEntry.Size = Both_Endian_32(record + 10);
		RootEntry.DateTime = Record_Date_Time(record + 18);
		if (RootEntry.Size == 0) return(false);

		ISOExtentType extent;
		extent.Start = Both_Endian_32(record + 2);
		extent.Length = RootEntry.Size;
		RootEntry.Extents.push_back(extent);

		VolumeName.assign((char const *)sector + 40, 32);
		while (!VolumeName.empty() && VolumeName.back() == ' ') {
			VolumeName.pop_back();
		}

		found = true;
	}

	return(found);
}


/*
 * Returns a cached sector, fetching it through the block source on a miss. The cache holds
 * the directory sectors a lookup revisits; file content bypasses it entirely.
 */
unsigned char const * ISOVolumeClass::Sector(std::uint32_t block) const
{
	if (!Source || Cache.empty()) return(nullptr);

	for (std::size_t index = 0; index < Cache.size(); index++) {
		if (Cache[index].IsValid && Cache[index].Block == block) {
			if (index != 0) {
				CacheEntryType hit = Cache[index];
				Cache.erase(Cache.begin() + (std::ptrdiff_t)index);
				Cache.insert(Cache.begin(), hit);
			}
			return(Cache.front().Data);
		}
	}

	CacheEntryType fetched;
	fetched.Block = block;
	fetched.IsValid = true;
	if (!Source->Read_At((std::uint64_t)block * ISO_SECTOR_SIZE, fetched.Data, ISO_SECTOR_SIZE)) {
		return(nullptr);
	}

	Cache.pop_back();
	Cache.insert(Cache.begin(), fetched);
	return(Cache.front().Data);
}


/*
 * Reads one directory, either looking for a name or collecting the file names it holds.
 */
bool ISOVolumeClass::Scan(ISOEntryClass const & directory, char const * wanted, ISOEntryClass * found, std::vector<std::string> * names) const
{
	if (!Source || !directory.IsDirectory) return(false);

	ISOEntryClass building;
	std::string buildingname;
	bool pending = false;

	for (ISOExtentType const & extent : directory.Extents) {
		std::uint32_t sectors = (extent.Length + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;

		for (std::uint32_t index = 0; index < sectors; index++) {
			unsigned char const * sector = Sector(extent.Start + index);
			if (sector == nullptr) return(false);

			unsigned int offset = 0;
			while (offset + ISO_MIN_RECORD_SIZE <= ISO_SECTOR_SIZE) {

				/*
				**	A record never straddles a sector boundary. The remainder of a sector
				**	that cannot hold another one is zero padding, which ends the sector.
				*/
				unsigned int length = sector[offset];
				if (length == 0) break;
				if (length < ISO_MIN_RECORD_SIZE || offset + length > ISO_SECTOR_SIZE) break;

				unsigned char const * record = sector + offset;
				offset += length;

				unsigned int namelength = record[32];
				if (namelength == 0 || 33 + namelength > length) continue;

				/*
				**	The two entries naming the directory itself and its parent are recorded
				**	with a single zero or one byte identifier.
				*/
				if (namelength == 1 && (record[33] == 0 || record[33] == 1)) continue;

				unsigned int flags = record[25];
				if ((flags & ISO_RECORD_ASSOCIATED) != 0) continue;

				std::string name = Plain_Name(record + 33, namelength);
				if (name.empty()) continue;

				ISOExtentType piece;
				piece.Start = Both_Endian_32(record + 2) + record[1];
				piece.Length = Both_Endian_32(record + 10);

				if (pending && name == buildingname) {
					building.Size += piece.Length;
					building.Extents.push_back(piece);
				} else {
					building.Reset();
					building.IsDirectory = (flags & ISO_RECORD_DIRECTORY) != 0;
					building.Size = piece.Length;
					building.DateTime = Record_Date_Time(record + 18);
					building.Extents.push_back(piece);
					buildingname = name;
				}

				pending = (flags & ISO_RECORD_MULTI_EXTENT) != 0;
				if (pending) continue;

				if (names != nullptr && !building.IsDirectory) {
					names->push_back(buildingname);
				}

				if (wanted != nullptr && ISO_Compare_Names(buildingname.c_str(), wanted)) {
					if (found != nullptr) {
						*found = building;
					}
					return(true);
				}
			}
		}
	}

	return(wanted == nullptr);
}


/// <summary>Looks up one name within a directory of this volume.</summary>
/// <param name="directory">The directory entry to search.</param>
/// <param name="name">The name wanted, matched without regard to case.</param>
/// <param name="entry">Filled in with the entry found.</param>
/// <returns>bool; Was the name found?</returns>
bool ISOVolumeClass::Find_In(ISOEntryClass const & directory, char const * name, ISOEntryClass & entry) const
{
	if (name == nullptr || *name == '\0') return(false);

	return(Scan(directory, name, &entry, nullptr));
}


/// <summary>Looks up a path within the volume.</summary>
/// <param name="path">Path relative to the volume root. Either separator is accepted and
/// every component is matched without regard to case.</param>
/// <param name="entry">Filled in with the entry found.</param>
/// <returns>bool; Was the path found?</returns>
bool ISOVolumeClass::Find(char const * path, ISOEntryClass & entry) const
{
	if (!Source || path == nullptr) return(false);

	ISOEntryClass current = RootEntry;
	char const * cursor = path;
	bool descended = false;

	while (*cursor != '\0') {
		while (*cursor == '/' || *cursor == '\\') cursor++;
		if (*cursor == '\0') break;

		std::string component;
		while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
			component.push_back(*cursor);
			cursor++;
		}

		if (component == ".") continue;

		if (!current.IsDirectory) return(false);

		ISOEntryClass next;
		if (!Find_In(current, component.c_str(), next)) return(false);

		current = next;
		descended = true;
	}

	if (!descended) {
		entry = RootEntry;
		return(true);
	}

	entry = current;
	return(true);
}


/// <summary>Collects the file names a directory holds.</summary>
/// <param name="directory">The directory entry to list.</param>
/// <param name="names">Receives the plain names, without their version suffix.
/// Subdirectories are left out, matching what the engine's file scan reports.</param>
/// <returns>bool; Was the directory read?</returns>
bool ISOVolumeClass::Enumerate(ISOEntryClass const & directory, std::vector<std::string> & names) const
{
	return(Scan(directory, nullptr, nullptr, &names));
}


/// <summary>Reads part of a file out of the volume.</summary>
/// <param name="entry">The file to read from.</param>
/// <param name="offset">Byte offset within the file.</param>
/// <param name="buffer">Destination for the bytes read.</param>
/// <param name="length">Number of bytes wanted.</param>
/// <returns>The number of bytes delivered, which is short at the end of the file.</returns>
int ISOVolumeClass::Read(ISOEntryClass const & entry, std::uint32_t offset, void * buffer, unsigned int length) const
{
	if (!Source || buffer == nullptr) return(0);
	if (entry.IsDirectory || offset >= entry.Size) return(0);

	if (length > entry.Size - offset) {
		length = entry.Size - offset;
	}

	unsigned int total = 0;
	std::uint32_t skip = offset;

	for (ISOExtentType const & extent : entry.Extents) {
		if (length == 0) break;

		if (skip >= extent.Length) {
			skip -= extent.Length;
			continue;
		}

		unsigned int available = extent.Length - skip;
		unsigned int chunk = length < available ? length : available;

		std::uint64_t at = (std::uint64_t)extent.Start * ISO_SECTOR_SIZE + skip;

		if (!Source->Read_At(at, (char *)buffer + total, chunk)) {

			/*
			**	A read that declined delivers nothing at all, not the extents that came
			**	before it. The caller's position then does not move and asking again reads
			**	the same run over, which is what lets it come back for it.
			*/
			if (ISODeferredReadClass::Declined_Now()) return(0);
			break;
		}

		total += chunk;
		length -= chunk;
		skip = 0;
	}

	return((int)total);
}


/// <summary>Tells the block source what a run of a file is about to be used for.</summary>
/// <param name="entry">The file the run belongs to.</param>
/// <param name="kind">Whether the run is being read now or may be wanted later.</param>
/// <param name="offset">Byte offset within the file.</param>
/// <param name="length">How many bytes the run covers.</param>
/// <remarks>The walk is the one Read does, so a hint names exactly the bytes a read of the
/// same span would ask for. Each extent is named separately, because ECMA-119 does not
/// require two runs of one file to sit next to each other on the disc.</remarks>
void ISOVolumeClass::Hint(ISOEntryClass const & entry, ISOHintType kind, std::uint32_t offset, std::uint32_t length) const
{
	if (!Source) return;
	if (entry.IsDirectory || offset >= entry.Size) return;

	if (length > entry.Size - offset) {
		length = entry.Size - offset;
	}

	std::uint32_t skip = offset;

	for (ISOExtentType const & extent : entry.Extents) {
		if (length == 0) break;

		if (skip >= extent.Length) {
			skip -= extent.Length;
			continue;
		}

		std::uint32_t const available = extent.Length - skip;
		std::uint32_t const chunk = length < available ? length : available;

		Source->Hint(kind, (std::uint64_t)extent.Start * ISO_SECTOR_SIZE + skip, chunk);

		length -= chunk;
		skip = 0;
	}
}


/*
**	The scope a read is inside, per thread. A block source consults it as it decides how to
**	answer, and a caller that entered one reads back whether it declined.
*/
thread_local ISODeferredReadClass * ISODeferredReadClass::Innermost = nullptr;


ISODeferredReadClass::ISODeferredReadClass(bool defer) :
	IsDeferring(defer),
	IsDeclined(false),
	Outer(Innermost)
{
	Innermost = this;
}


ISODeferredReadClass::~ISODeferredReadClass(void)
{
	Innermost = Outer;
}


bool ISODeferredReadClass::Deferring(void)
{
	return(Innermost != nullptr && Innermost->IsDeferring);
}


bool ISODeferredReadClass::Declined_Now(void)
{
	return(Innermost != nullptr && Innermost->IsDeclined);
}


void ISODeferredReadClass::Decline(void)
{
	if (Innermost != nullptr) Innermost->IsDeclined = true;
}


/// <summary>Compares two names the way a disc lookup should.</summary>
/// <param name="left">First name.</param>
/// <param name="right">Second name.</param>
/// <returns>bool; Do the names match?</returns>
/// <remarks>The fold is done here rather than by the host, because the case rules the
/// engine relies on must be the same on a case-insensitive host filesystem and in the
/// browser's case-sensitive one.</remarks>
bool ISO_Compare_Names(char const * left, char const * right)
{
	if (left == nullptr || right == nullptr) return(false);

	while (*left != '\0' && *right != '\0') {
		if (Fold(*left) != Fold(*right)) return(false);
		left++;
		right++;
	}

	return(*left == *right);
}


/// <summary>Matches a name against a DOS style wildcard.</summary>
/// <param name="pattern">Wildcard, where '*' spans any run of characters and '?' one.</param>
/// <param name="name">The name to test, compared without regard to case.</param>
/// <returns>bool; Does the name match?</returns>
bool ISO_Match_Wildcard(char const * pattern, char const * name)
{
	if (pattern == nullptr || name == nullptr) return(false);

	char const * star = nullptr;
	char const * retry = nullptr;

	while (*name != '\0') {
		if (*pattern == '?' || (*pattern != '\0' && Fold(*pattern) == Fold(*name))) {
			pattern++;
			name++;
			continue;
		}

		if (*pattern == '*') {
			star = pattern;
			retry = name;
			pattern++;
			continue;
		}

		if (star == nullptr) return(false);

		/*
		**	The last star swallows one more character and the match resumes after it.
		*/
		pattern = star + 1;
		retry++;
		name = retry;
	}

	while (*pattern == '*') pattern++;

	return(*pattern == '\0');
}


/// <summary>Lists the directories of a disc that hold game data, in search order.</summary>
/// <param name="volume">The mounted volume to examine.</param>
/// <param name="directories">Receives the paths to search, most specific first. The
/// installed data directory is listed ahead of the disc root so that a file present in
/// both resolves to the installed copy.</param>
/// <remarks>The directory is matched without regard to case, since the discs disagree on
/// how they spell it.</remarks>
void ISO_Search_Directories(ISOVolumeClass const & volume, std::vector<std::string> & directories)
{
	directories.clear();

	if (!volume.Is_Open()) return;

	ISOEntryClass install;
	if (volume.Find_In(volume.Root(), "INSTALL", install) && install.IsDirectory) {
		directories.push_back("INSTALL");
	}

	directories.push_back(std::string());
}
