/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the ISO9660 reader against volumes the test builds for itself. No game disc,
// game data or original executable is involved, and none may become one.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "iso9660.h"

#ifdef _WIN32
#include "isofile.h"
#endif

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
**	A minimal ISO9660 writer, just complete enough to be read back by the reader under
**	test: a primary descriptor, an optional Joliet marker, a root directory, one level of
**	subdirectories, and file data.
**	------------------------------------------------------------------------------------
*/

struct FileSpec {
	std::string Name;
	std::vector<unsigned char> Data;
	bool Split;                       // Written as two extents, exercising multi-extent files.
};


struct DirSpec {
	std::string Name;
	std::vector<FileSpec> Files;
};


struct RecordSpec {
	std::string Identifier;
	bool IsDirectory;
	bool MultiExtent;
	std::uint32_t Block;
	std::uint32_t Length;
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
	field[1] = (unsigned char)(value >> 8);
	field[2] = (unsigned char)(value >> 8);
	field[3] = (unsigned char)(value & 0xFF);
}


unsigned int Record_Size(std::string const & identifier)
{
	unsigned int size = 33 + (unsigned int)identifier.size();
	if ((size & 1) != 0) size++;
	return(size);
}


void Write_Record(unsigned char * record, RecordSpec const & spec)
{
	unsigned int size = Record_Size(spec.Identifier);

	std::memset(record, 0, size);
	record[0] = (unsigned char)size;
	Put_Both_32(record + 2, spec.Block);
	Put_Both_32(record + 10, spec.Length);

	record[18] = 99;      // 1999
	record[19] = 7;
	record[20] = 28;
	record[21] = 12;
	record[22] = 30;
	record[23] = 0;
	record[24] = 0;

	record[25] = (unsigned char)((spec.IsDirectory ? 0x02 : 0x00) | (spec.MultiExtent ? 0x80 : 0x00));
	Put_Both_16(record + 28, 1);
	record[32] = (unsigned char)spec.Identifier.size();
	std::memcpy(record + 33, spec.Identifier.data(), spec.Identifier.size());
}


/*
**	Packs records into sectors. A record never crosses a sector boundary, so a record that
**	does not fit in what is left of a sector starts the next one and the remainder becomes
**	zero padding.
*/
std::vector<unsigned char> Pack_Directory(std::vector<RecordSpec> const & records)
{
	std::vector<unsigned char> data;
	unsigned int offset = 0;

	for (RecordSpec const & spec : records) {
		unsigned int size = Record_Size(spec.Identifier);

		if (offset + size > 2048) {
			data.resize(data.size() + (2048 - offset), 0);
			offset = 0;
		}

		data.resize(data.size() + size);
		Write_Record(&data[data.size() - size], spec);
		offset += size;
	}

	if (offset != 0) {
		data.resize(data.size() + (2048 - offset), 0);
	}

	return(data);
}


std::string File_Identifier(std::string const & name)
{
	if (name.find('.') == std::string::npos) {
		return(name + ".;1");
	}
	return(name + ";1");
}


/*
**	Lays out and writes a whole image. The layout is descriptors, then directories, then
**	file data, each piece starting on a sector boundary.
*/
std::vector<unsigned char> Build_Image(std::string const & label, std::vector<FileSpec> const & rootfiles, std::vector<DirSpec> const & dirs, bool joliet)
{
	unsigned int terminator = joliet ? 18 : 17;
	std::uint32_t next = terminator + 1;

	/*
	**	Size every directory first. The sizes do not depend on where anything lands, so one
	**	pass fixes the layout and a second fills the addresses in.
	*/
	std::vector<RecordSpec> rootrecords;
	rootrecords.push_back({std::string(1, '\0'), true, false, 0, 0});
	rootrecords.push_back({std::string(1, '\1'), true, false, 0, 0});
	for (DirSpec const & dir : dirs) {
		rootrecords.push_back({dir.Name, true, false, 0, 0});
	}
	for (FileSpec const & file : rootfiles) {
		if (file.Split) {
			rootrecords.push_back({File_Identifier(file.Name), false, true, 0, 0});
		}
		rootrecords.push_back({File_Identifier(file.Name), false, false, 0, 0});
	}

	std::vector<std::vector<RecordSpec>> dirrecords;
	for (DirSpec const & dir : dirs) {
		std::vector<RecordSpec> records;
		records.push_back({std::string(1, '\0'), true, false, 0, 0});
		records.push_back({std::string(1, '\1'), true, false, 0, 0});
		for (FileSpec const & file : dir.Files) {
			if (file.Split) {
				records.push_back({File_Identifier(file.Name), false, true, 0, 0});
			}
			records.push_back({File_Identifier(file.Name), false, false, 0, 0});
		}
		dirrecords.push_back(records);
	}

	std::uint32_t rootblock = next;
	std::uint32_t rootsize = (std::uint32_t)Pack_Directory(rootrecords).size();
	next += rootsize / 2048;

	std::vector<std::uint32_t> dirblocks;
	std::vector<std::uint32_t> dirsizes;
	for (std::vector<RecordSpec> const & records : dirrecords) {
		std::uint32_t size = (std::uint32_t)Pack_Directory(records).size();
		dirblocks.push_back(next);
		dirsizes.push_back(size);
		next += size / 2048;
	}

	/*
	**	Place the file data and fill the addresses into the records that name it. A split
	**	file gets one whole sector in its first extent so the second continues cleanly.
	*/
	std::vector<unsigned char> content;
	std::uint32_t contentblock = next;

	auto place = [&](FileSpec const & file, RecordSpec * records) {
		std::uint32_t block = contentblock + (std::uint32_t)(content.size() / 2048);

		if (file.Split) {
			records[0].Block = block;
			records[0].Length = 2048;
			records[1].Block = block + 1;
			records[1].Length = (std::uint32_t)file.Data.size() - 2048;
		} else {
			records[0].Block = block;
			records[0].Length = (std::uint32_t)file.Data.size();
		}

		content.insert(content.end(), file.Data.begin(), file.Data.end());
		while ((content.size() % 2048) != 0) content.push_back(0);
	};

	std::size_t cursor = 2 + dirs.size();
	for (FileSpec const & file : rootfiles) {
		place(file, &rootrecords[cursor]);
		cursor += file.Split ? 2 : 1;
	}

	for (std::size_t index = 0; index < dirs.size(); index++) {
		rootrecords[2 + index].Block = dirblocks[index];
		rootrecords[2 + index].Length = dirsizes[index];

		std::size_t inner = 2;
		for (FileSpec const & file : dirs[index].Files) {
			place(file, &dirrecords[index][inner]);
			inner += file.Split ? 2 : 1;
		}
	}

	rootrecords[0].Block = rootblock;
	rootrecords[0].Length = rootsize;
	rootrecords[1].Block = rootblock;
	rootrecords[1].Length = rootsize;

	for (std::size_t index = 0; index < dirs.size(); index++) {
		dirrecords[index][0].Block = dirblocks[index];
		dirrecords[index][0].Length = dirsizes[index];
		dirrecords[index][1].Block = rootblock;
		dirrecords[index][1].Length = rootsize;
	}

	/*
	**	Emit the image.
	*/
	std::vector<unsigned char> image((std::size_t)contentblock * 2048 + content.size(), 0);
	next = (std::uint32_t)(image.size() / 2048);

	RecordSpec rootspec = {std::string(1, '\0'), true, false, rootblock, rootsize};

	auto descriptor = [&](unsigned int sector, unsigned char type) {
		unsigned char * vd = &image[sector * 2048];
		vd[0] = type;
		std::memcpy(vd + 1, "CD001", 5);
		vd[6] = 1;
		std::memset(vd + 8, ' ', 32);
		std::memset(vd + 40, ' ', 32);
		std::memcpy(vd + 40, label.data(), label.size() < 32 ? label.size() : 32);
		Put_Both_32(vd + 80, next);
		Put_Both_16(vd + 120, 1);
		Put_Both_16(vd + 124, 1);
		Put_Both_16(vd + 128, 2048);
		Write_Record(vd + 156, rootspec);
		vd[881] = 1;
	};

	descriptor(16, 1);
	if (joliet) {
		descriptor(17, 2);
		unsigned char * svd = &image[17 * 2048];
		svd[88] = '%';
		svd[89] = '/';
		svd[90] = '@';
	}

	image[terminator * 2048] = 255;
	std::memcpy(&image[terminator * 2048 + 1], "CD001", 5);
	image[terminator * 2048 + 6] = 1;

	std::vector<unsigned char> packed = Pack_Directory(rootrecords);
	std::memcpy(&image[rootblock * 2048], packed.data(), packed.size());

	for (std::size_t index = 0; index < dirrecords.size(); index++) {
		std::vector<unsigned char> dirdata = Pack_Directory(dirrecords[index]);
		std::memcpy(&image[dirblocks[index] * 2048], dirdata.data(), dirdata.size());
	}

	if (!content.empty()) {
		std::memcpy(&image[contentblock * 2048], content.data(), content.size());
	}

	return(image);
}


bool Write_File(std::string const & path, std::vector<unsigned char> const & data)
{
	std::FILE * file = std::fopen(path.c_str(), "wb");
	if (file == nullptr) return(false);

	bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
	std::fclose(file);
	return(ok);
}


/*
**	A block source over a buffer already in memory, standing in for the range-request
**	transport the browser build supplies.
*/
class MemorySourceClass : public ISOBlockSourceClass
{
	public:
		MemorySourceClass(std::vector<unsigned char> const & data) : Reads(0), Data(data) {}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override
		{
			if (offset > Data.size() || Data.size() - offset < length) return(false);
			std::memcpy(buffer, Data.data() + offset, length);
			Reads++;
			return(true);
		}

		virtual std::uint64_t Total_Size(void) override {return(Data.size());}

		int Reads;

	private:

		std::vector<unsigned char> Data;
};


bool Read_Whole(ISOVolumeClass const & volume, ISOEntryClass const & entry, std::vector<unsigned char> & out)
{
	out.assign(entry.Size, 0);
	if (entry.Size == 0) return(true);

	return(volume.Read(entry, 0, out.data(), entry.Size) == (int)entry.Size);
}


/*
**	Mirrors what the search path does with an ordered list of mounted images: every image
**	offers its data directory ahead of its root, and the images are tried in the order the
**	caller supplied them.
*/
bool Search(std::vector<ISOVolumeClass *> const & volumes, char const * filename, ISOVolumeClass ** which, ISOEntryClass & entry)
{
	for (ISOVolumeClass * volume : volumes) {
		std::vector<std::string> directories;
		ISO_Search_Directories(*volume, directories);

		for (std::string const & directory : directories) {
			std::string path = directory;
			if (!path.empty()) path += '\\';
			path += filename;

			if (volume->Find(path.c_str(), entry) && !entry.IsDirectory) {
				*which = volume;
				return(true);
			}
		}
	}

	return(false);
}

} // namespace


int main(int argc, char ** argv)
{
	std::string directory = argc > 1 ? argv[1] : ".";

	/*
	**	The first disc: files in the root and in an uppercase INSTALL directory. The
	**	padding files push that directory past a single sector, and the file listed after
	**	them proves a record in the second sector is still found.
	*/
	std::vector<FileSpec> rootfiles;
	rootfiles.push_back({"ROOT.TXT", Pattern(1, 37), false});
	rootfiles.push_back({"MULTI.MIX", Pattern(2, 5000), false});
	rootfiles.push_back({"NOEXT", Pattern(3, 11), false});

	DirSpec install;
	install.Name = "INSTALL";
	install.Files.push_back({"TIBSUN.MIX", Pattern(4, 3000), false});
	install.Files.push_back({"SPLIT.BIN", Pattern(5, 3000), true});
	for (int index = 0; index < 60; index++) {
		char name[16];
		std::snprintf(name, sizeof(name), "PAD%02d.DAT", index);
		install.Files.push_back({name, Pattern(100 + index, 10), false});
	}
	install.Files.push_back({"LAST.DAT", Pattern(6, 16), false});

	std::vector<DirSpec> dirs;
	dirs.push_back(install);

	std::vector<unsigned char> first = Build_Image("FIRST", rootfiles, dirs, false);

	/*
	**	The second disc spells its data directory in mixed case and carries a Joliet
	**	supplementary descriptor, the way the expansion disc does.
	*/
	std::vector<FileSpec> secondroot;
	secondroot.push_back({"MULTI.MIX", Pattern(7, 64), false});

	DirSpec mixedcase;
	mixedcase.Name = "Install";
	mixedcase.Files.push_back({"multi.mix", Pattern(8, 96), false});
	mixedcase.Files.push_back({"expand01.mix", Pattern(9, 48), false});

	std::vector<DirSpec> seconddirs;
	seconddirs.push_back(mixedcase);

	std::vector<unsigned char> second = Build_Image("SECOND", secondroot, seconddirs, true);

	std::string firstpath = directory + "/iso9660test1.iso";
	std::string secondpath = directory + "/iso9660test2.iso";

	Check(Write_File(firstpath, first), "wrote the first synthetic image");
	Check(Write_File(secondpath, second), "wrote the second synthetic image");

	/*
	**	Mounting.
	*/
	ISOVolumeClass volume1;
	ISOVolumeClass volume2;

	Check(volume1.Open(firstpath.c_str()), "mounted the first image from a local file");
	Check(volume2.Open(secondpath.c_str()), "mounted the second image from a local file");
	Check(std::strcmp(volume1.Volume_Name(), "FIRST") == 0, "read the volume identifier");
	Check(!volume1.Has_Joliet(), "reported no Joliet on the plain image");
	Check(volume2.Has_Joliet(), "reported Joliet on the supplementary image");

	{
		ISOVolumeClass rejected;
		Check(!rejected.Open("this-file-does-not-exist.iso"), "refused a missing image");
	}

	/*
	**	Lookup and case folding.
	*/
	ISOEntryClass entry;
	Check(volume1.Find("ROOT.TXT", entry), "found a root file");
	Check(volume1.Find("root.txt", entry), "found a root file spelled in lower case");
	Check(volume1.Find("RoOt.TxT", entry), "found a root file spelled in mixed case");
	Check(volume1.Find("NOEXT", entry), "found a file with no extension");
	Check(!volume1.Find("MISSING.MIX", entry), "reported a missing file as missing");
	Check(volume1.Find("INSTALL/TIBSUN.MIX", entry), "found a file through a forward slash path");
	Check(volume1.Find("install\\tibsun.mix", entry), "found a file through a backslash path, folded");
	Check(volume1.Find("/INSTALL/TIBSUN.MIX", entry), "found a file through a leading separator");
	Check(!volume1.Find("INSTALL/MISSING.MIX", entry), "reported a missing file in a subdirectory");
	Check(volume1.Find("INSTALL", entry) && entry.IsDirectory, "reported a directory as a directory");
	Check(!volume1.Find("TIBSUN.MIX", entry), "did not find a subdirectory file at the root");

	/*
	**	A directory that spans more than one sector.
	*/
	{
		ISOEntryClass directory1;
		std::vector<std::string> names;
		Check(volume1.Find("INSTALL", directory1), "located the multi-sector directory");
		Check(directory1.Size > 2048, "the test directory really does span sectors");
		Check(volume1.Enumerate(directory1, names), "listed the multi-sector directory");
		Check(names.size() == 63, "listed every file in the multi-sector directory");
		Check(volume1.Find("INSTALL/LAST.DAT", entry), "found a file recorded past the first sector");
	}

	/*
	**	Byte exact reads, including a file that spans sectors and one stored as two extents.
	*/
	{
		std::vector<unsigned char> expected = Pattern(2, 5000);
		std::vector<unsigned char> actual;
		Check(volume1.Find("MULTI.MIX", entry) && entry.Size == 5000, "sized a multi-sector file");
		Check(Read_Whole(volume1, entry, actual), "read a multi-sector file in full");
		Check(actual == expected, "a multi-sector file read back byte for byte");

		unsigned char partial[300];
		Check(volume1.Read(entry, 2000, partial, sizeof(partial)) == (int)sizeof(partial), "read across a sector boundary");
		Check(std::memcmp(partial, expected.data() + 2000, sizeof(partial)) == 0, "the read across a sector boundary matched");

		Check(volume1.Read(entry, 4990, partial, sizeof(partial)) == 10, "a read past the end was cut short");
		Check(std::memcmp(partial, expected.data() + 4990, 10) == 0, "the short read at the end matched");
		Check(volume1.Read(entry, 5000, partial, sizeof(partial)) == 0, "a read starting at the end returned nothing");
	}

	{
		std::vector<unsigned char> expected = Pattern(5, 3000);
		std::vector<unsigned char> actual;
		Check(volume1.Find("INSTALL/SPLIT.BIN", entry), "found the multi-extent file");
		Check(entry.Extents.size() == 2, "the multi-extent file kept both extents");
		Check(entry.Size == 3000, "the multi-extent file reported its combined size");
		Check(Read_Whole(volume1, entry, actual), "read the multi-extent file in full");
		Check(actual == expected, "the multi-extent file read back byte for byte across the join");

		unsigned char partial[200];
		Check(volume1.Read(entry, 1980, partial, sizeof(partial)) == (int)sizeof(partial), "read across the extent join");
		Check(std::memcmp(partial, expected.data() + 1980, sizeof(partial)) == 0, "the read across the extent join matched");
	}

	{
		ISOEntryClass directory1;
		unsigned char scratch[16];
		Check(volume1.Find("INSTALL", directory1), "located a directory for the read check");
		Check(volume1.Read(directory1, 0, scratch, sizeof(scratch)) == 0, "refused to read a directory as a file");
	}

	/*
	**	Search precedence.
	*/
	{
		std::vector<std::string> directories;
		ISO_Search_Directories(volume1, directories);
		Check(directories.size() == 2 && directories[0] == "INSTALL" && directories[1].empty(),
			"put the data directory ahead of the root");

		ISO_Search_Directories(volume2, directories);
		Check(directories.size() == 2 && directories[0] == "INSTALL" && directories[1].empty(),
			"found the data directory spelled in mixed case");
	}

	{
		std::vector<ISOVolumeClass *> order;
		ISOVolumeClass * which = nullptr;
		std::vector<unsigned char> actual;

		order.push_back(&volume2);
		order.push_back(&volume1);
		Check(Search(order, "MULTI.MIX", &which, entry), "resolved a name present on both images");
		Check(which == &volume2, "the image listed first supplied the file");
		Check(Read_Whole(volume2, entry, actual) && actual == Pattern(8, 96),
			"the data directory of the first image won over its own root");

		order.clear();
		order.push_back(&volume1);
		order.push_back(&volume2);
		Check(Search(order, "MULTI.MIX", &which, entry), "resolved the name with the order reversed");
		Check(which == &volume1, "reversing the order changed which image supplied the file");
		Check(Read_Whole(volume1, entry, actual) && actual == Pattern(2, 5000),
			"the root of the first image supplied the file when its data directory had none");

		order.clear();
		order.push_back(&volume1);
		order.push_back(&volume2);
		Check(Search(order, "EXPAND01.MIX", &which, entry) && which == &volume2,
			"a file only the second image carries still resolved");
	}

	/*
	**	Wildcards, for the archive scans the engine performs.
	*/
	Check(ISO_Match_Wildcard("ECACHE*.MIX", "ECACHE01.MIX"), "matched a trailing wildcard");
	Check(ISO_Match_Wildcard("ecache*.mix", "ECACHE01.MIX"), "matched a wildcard without regard to case");
	Check(!ISO_Match_Wildcard("ECACHE*.MIX", "EXPAND01.MIX"), "rejected a name the wildcard does not cover");
	Check(ISO_Match_Wildcard("*.MIX", "A.MIX"), "matched a leading wildcard");
	Check(ISO_Match_Wildcard("*", "ANYTHING"), "matched a bare wildcard");
	Check(ISO_Match_Wildcard("PAD??.DAT", "PAD07.DAT"), "matched single character wildcards");
	Check(!ISO_Match_Wildcard("PAD?.DAT", "PAD07.DAT"), "rejected a single character wildcard that is too short");
	Check(!ISO_Match_Wildcard("ECACHE*.MIX", "ECACHE01.MIXX"), "rejected a name with trailing characters");

	{
		ISOEntryClass directory1;
		std::vector<std::string> names;
		int matched = 0;

		volume1.Find("INSTALL", directory1);
		volume1.Enumerate(directory1, names);
		for (std::string const & name : names) {
			if (ISO_Match_Wildcard("PAD*.DAT", name.c_str())) matched++;
		}
		Check(matched == 60, "a wildcard scan over a directory found every match");
	}

	Check(ISO_Compare_Names("TIBSUN.MIX", "tibsun.mix"), "folded case in a name comparison");
	Check(!ISO_Compare_Names("TIBSUN.MIX", "TIBSUN.MI"), "rejected a name that is merely a prefix");

	/*
	**	The same volume served by a different block source, which is what the browser build
	**	substitutes.
	*/
	{
		ISOVolumeClass memory;
		std::unique_ptr<MemorySourceClass> source(new MemorySourceClass(first));
		MemorySourceClass * watch = source.get();

		Check(memory.Attach(std::move(source)), "mounted an image through a supplied block source");
		Check(memory.Find("INSTALL/TIBSUN.MIX", entry), "found a file through the supplied block source");

		std::vector<unsigned char> actual;
		Check(Read_Whole(memory, entry, actual) && actual == Pattern(4, 3000),
			"read a file byte for byte through the supplied block source");

		/*
		**	Repeating a lookup must not go back to the source, which is what the sector
		**	cache exists for.
		*/
		int before = watch->Reads;
		for (int index = 0; index < 20; index++) {
			memory.Find("INSTALL/TIBSUN.MIX", entry);
		}
		Check(watch->Reads == before, "the sector cache served a repeated lookup without a fetch");
	}

#ifdef _WIN32
	/*
	**	A disc is read only. These live with the file class rather than the reader, so they
	**	run only where the file class builds.
	*/
	{
		ISOVolumeClass volume;
		ISOFileClass file;

		volume.Open(firstpath.c_str());

		std::shared_ptr<ISOVolumeClass> shared = std::make_shared<ISOVolumeClass>();
		shared->Open(firstpath.c_str());
		shared->Find("INSTALL/TIBSUN.MIX", entry);
		file.Attach(shared, "TIBSUN.MIX", entry);

		Check(file.Is_Available(), "an attached image file reported itself available");
		Check(file.Open(FileClass::READ) != 0, "opened an image file for reading");
		Check(file.Size() == 3000, "an image file reported its size");

		std::vector<unsigned char> actual(3000, 0);
		Check(file.Read(actual.data(), 3000) == 3000, "read an image file through the file class");
		Check(actual == Pattern(4, 3000), "the file class read back byte for byte");

		Check(file.Write(actual.data(), 10) == 0, "writing to an image file wrote nothing");
		Check(file.Create() == 0, "creating an image file failed");
		Check(file.Delete() == 0, "deleting an image file failed");
		Check(file.Set_Date_Time(0) == false, "stamping an image file failed");

		file.Close();
		Check(file.Open(FileClass::WRITE) == 0, "opening an image file for writing was refused");
		Check(!file.Is_Open(), "the refused write open left the file closed");
	}
#endif

	std::remove(firstpath.c_str());
	std::remove(secondpath.c_str());

	std::printf("\n%s\n", Failures == 0 ? "iso9660: all checks passed" : "iso9660: FAILURES");
	return(Failures == 0 ? 0 : 1);
}
