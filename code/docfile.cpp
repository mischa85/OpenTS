/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A compound file reader and writer, in the layout Microsoft publishes as MS-CFB. docfile.h
// says what it is for and what it deliberately leaves out.

#include "docfile.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

/*
** The version 3 geometry, which is what a save file is written in. The reader accepts the
** version 4 sector size as well, because a file written elsewhere may carry it.
*/
constexpr std::uint32_t SECTOR_SIZE = 512;
constexpr std::uint32_t MINI_SECTOR_SIZE = 64;
constexpr std::uint32_t MINI_CUTOFF = 4096;
constexpr std::uint32_t DIRECTORY_ENTRY_SIZE = 128;
constexpr std::uint32_t HEADER_SIZE = 512;

constexpr std::uint32_t FAT_PER_SECTOR = SECTOR_SIZE / 4;
constexpr std::uint32_t DIRECTORY_PER_SECTOR = SECTOR_SIZE / DIRECTORY_ENTRY_SIZE;
constexpr std::uint32_t DIFAT_IN_HEADER = 109;
constexpr std::uint32_t DIFAT_PER_SECTOR = FAT_PER_SECTOR - 1;

constexpr std::uint32_t MAXREGSECT = 0xFFFFFFFAu;
constexpr std::uint32_t DIFSECT = 0xFFFFFFFCu;
constexpr std::uint32_t FATSECT = 0xFFFFFFFDu;
constexpr std::uint32_t ENDOFCHAIN = 0xFFFFFFFEu;
constexpr std::uint32_t FREESECT = 0xFFFFFFFFu;
constexpr std::uint32_t NOSTREAM = 0xFFFFFFFFu;

constexpr std::uint8_t OBJECT_UNALLOCATED = 0;
constexpr std::uint8_t OBJECT_STREAM = 2;
constexpr std::uint8_t OBJECT_ROOT = 5;

constexpr std::uint8_t COLOR_RED = 0;
constexpr std::uint8_t COLOR_BLACK = 1;

/*
** A directory entry holds 32 UTF-16 characters at most, the last of which is the
** terminating null.
*/
constexpr std::size_t NAME_MAX_CHARS = 31;

unsigned char const DocFileSignature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

wchar_t const RootEntryName[] = L"Root Entry";


std::uint16_t Get_U16(unsigned char const * from)
{
	return((std::uint16_t)(from[0] | (from[1] << 8)));
}


std::uint32_t Get_U32(unsigned char const * from)
{
	return((std::uint32_t)from[0] | ((std::uint32_t)from[1] << 8)
		| ((std::uint32_t)from[2] << 16) | ((std::uint32_t)from[3] << 24));
}


std::uint64_t Get_U64(unsigned char const * from)
{
	return((std::uint64_t)Get_U32(from) | ((std::uint64_t)Get_U32(from + 4) << 32));
}


void Put_U16(unsigned char * into, std::uint16_t value)
{
	into[0] = (unsigned char)(value & 0xFF);
	into[1] = (unsigned char)((value >> 8) & 0xFF);
}


void Put_U32(unsigned char * into, std::uint32_t value)
{
	into[0] = (unsigned char)(value & 0xFF);
	into[1] = (unsigned char)((value >> 8) & 0xFF);
	into[2] = (unsigned char)((value >> 16) & 0xFF);
	into[3] = (unsigned char)((value >> 24) & 0xFF);
}


void Put_U64(unsigned char * into, std::uint64_t value)
{
	Put_U32(into, (std::uint32_t)(value & 0xFFFFFFFFu));
	Put_U32(into + 4, (std::uint32_t)(value >> 32));
}


std::uint32_t Round_Up(std::uint64_t value, std::uint32_t unit)
{
	return((std::uint32_t)((value + unit - 1) / unit));
}


/*
** Directory entries are ordered by name length first and then by the uppercased characters,
** which is the comparison a reader walks the sibling tree with. Only ASCII is folded: a save
** file names its streams in ASCII, and folding beyond it would need the same case table
** Windows uses rather than a plausible substitute.
*/
int Compare_Names(std::wstring const & left, std::wstring const & right)
{
	if (left.size() != right.size()) return(left.size() < right.size() ? -1 : 1);

	for (std::size_t index = 0; index < left.size(); index++) {
		std::uint16_t first = (std::uint16_t)left[index];
		std::uint16_t second = (std::uint16_t)right[index];

		if (first >= 'a' && first <= 'z') first = (std::uint16_t)(first - 'a' + 'A');
		if (second >= 'a' && second <= 'z') second = (std::uint16_t)(second - 'a' + 'A');

		if (first != second) return(first < second ? -1 : 1);
	}

	return(0);
}


/*
** Measures and copies a UTF-16 name without going through the C library. The engine builds
** with a two byte wchar_t while the library it links was built with a four byte one, so
** wcslen -- which is what constructing a std::wstring from a pointer reaches for -- reads
** the name two characters at a time and answers nonsense.
*/
std::wstring Wide_String(OLECHAR const * name)
{
	std::wstring result;

	if (name != nullptr) {
		for (OLECHAR const * ptr = name; *ptr != 0; ptr++) result.push_back((wchar_t)*ptr);
	}

	return(result);
}


struct DocFileElementType
{
	std::wstring Name;
	std::vector<unsigned char> Data;
};


class DocFileClass;


/*
** One stream inside the compound file. The bytes live on the element the storage owns, so a
** stream that is released before the storage is committed does not take its own data with
** it -- which is the order saveload.cpp writes a save game in.
*/
class DocFileStreamClass : public IStream
{
	public:
		DocFileStreamClass(DocFileClass * owner, std::shared_ptr<DocFileElementType> element, bool writable);
		virtual ~DocFileStreamClass(void);

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) override;
		virtual ULONG STDMETHODCALLTYPE AddRef(void) override;
		virtual ULONG STDMETHODCALLTYPE Release(void) override;

		virtual HRESULT STDMETHODCALLTYPE Read(void * data, ULONG length, ULONG * read) override;
		virtual HRESULT STDMETHODCALLTYPE Write(void const * data, ULONG length, ULONG * written) override;
		virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER * position) override;
		virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override;
		virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream * target, ULARGE_INTEGER length, ULARGE_INTEGER * read, ULARGE_INTEGER * written) override;
		virtual HRESULT STDMETHODCALLTYPE Commit(DWORD flags) override;
		virtual HRESULT STDMETHODCALLTYPE Revert(void) override;
		virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER length, DWORD locktype) override;
		virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER length, DWORD locktype) override;
		virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG * statstg, DWORD statflag) override;
		virtual HRESULT STDMETHODCALLTYPE Clone(IStream ** stream) override;

	private:
		DocFileClass * Owner;
		std::shared_ptr<DocFileElementType> Element;
		std::uint64_t Cursor;
		bool Writable;
		LONG RefCount;
};


/*
** The root storage. The whole file is held in memory between opening and committing: a save
** is a few megabytes and the engine is already holding the game it came from, and reading and
** writing the container in one piece is what keeps the sector bookkeeping in one place.
*/
class DocFileClass : public IStorage
{
	public:
		DocFileClass(char const * path, bool writable);
		virtual ~DocFileClass(void);

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) override;
		virtual ULONG STDMETHODCALLTYPE AddRef(void) override;
		virtual ULONG STDMETHODCALLTYPE Release(void) override;

		virtual HRESULT STDMETHODCALLTYPE CreateStream(OLECHAR const * name, DWORD mode, DWORD reserved1, DWORD reserved2, IStream ** stream) override;
		virtual HRESULT STDMETHODCALLTYPE OpenStream(OLECHAR const * name, void * reserved1, DWORD mode, DWORD reserved2, IStream ** stream) override;
		virtual HRESULT STDMETHODCALLTYPE CreateStorage(OLECHAR const * name, DWORD mode, DWORD reserved1, DWORD reserved2, IStorage ** storage) override;
		virtual HRESULT STDMETHODCALLTYPE OpenStorage(OLECHAR const * name, IStorage * priority, DWORD mode, SNB exclude, DWORD reserved, IStorage ** storage) override;
#if defined(_WIN32)
		/*
		** Three methods the Windows declaration of IStorage carries and win32compat.h does
		** not, so that the same class satisfies both.
		*/
		virtual HRESULT STDMETHODCALLTYPE CopyTo(DWORD count, IID const * exclude, SNB excludenames, IStorage * target) override;
		virtual HRESULT STDMETHODCALLTYPE MoveElementTo(OLECHAR const * name, IStorage * target, LPOLESTR newname, DWORD flags) override;
		virtual HRESULT STDMETHODCALLTYPE EnumElements(DWORD reserved1, void * reserved2, DWORD reserved3, IEnumSTATSTG ** enumerator) override;
#endif
		virtual HRESULT STDMETHODCALLTYPE Commit(DWORD flags) override;
		virtual HRESULT STDMETHODCALLTYPE Revert(void) override;
		virtual HRESULT STDMETHODCALLTYPE DestroyElement(OLECHAR const * name) override;
		virtual HRESULT STDMETHODCALLTYPE RenameElement(OLECHAR const * from, OLECHAR const * to) override;
		virtual HRESULT STDMETHODCALLTYPE SetElementTimes(OLECHAR const * name, FILETIME const * creation, FILETIME const * access, FILETIME const * modify) override;
		virtual HRESULT STDMETHODCALLTYPE SetClass(REFCLSID classid) override;
		virtual HRESULT STDMETHODCALLTYPE SetStateBits(DWORD statebits, DWORD mask) override;
		virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG * statstg, DWORD statflag) override;

		bool Read_File(void);
		bool Parse(std::vector<unsigned char> const & image);
		void Serialize(std::vector<unsigned char> & image) const;
		bool Write_File(void);

		void Touch(void) {IsDirty = true;}
		bool Is_Writable(void) const {return(Writable);}

	private:
		std::shared_ptr<DocFileElementType> Find(std::wstring const & name) const;

		std::string Path;
		std::vector<std::shared_ptr<DocFileElementType>> Elements;
		CLSID Class;
		bool Writable;
		bool IsDirty;
		LONG RefCount;
};


DocFileStreamClass::DocFileStreamClass(DocFileClass * owner, std::shared_ptr<DocFileElementType> element, bool writable) :
	Owner(owner),
	Element(std::move(element)),
	Cursor(0),
	Writable(writable),
	RefCount(1)
{
	Owner->AddRef();
}


DocFileStreamClass::~DocFileStreamClass(void)
{
	Owner->Release();
}


HRESULT DocFileStreamClass::QueryInterface(REFIID riid, void ** object)
{
	if (object == nullptr) return(E_POINTER);

	*object = nullptr;
	if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream) {
		*object = (IStream *)this;
		AddRef();
		return(S_OK);
	}

	return(E_NOINTERFACE);
}


ULONG DocFileStreamClass::AddRef(void)
{
	return((ULONG)InterlockedIncrement(&RefCount));
}


ULONG DocFileStreamClass::Release(void)
{
	LONG const remaining = InterlockedDecrement(&RefCount);

	if (remaining == 0) {
		delete this;
	}

	return((ULONG)remaining);
}


HRESULT DocFileStreamClass::Read(void * data, ULONG length, ULONG * read)
{
	if (read != nullptr) *read = 0;
	if (data == nullptr && length != 0) return(E_POINTER);

	std::uint64_t const size = Element->Data.size();
	std::uint64_t available = (Cursor < size) ? (size - Cursor) : 0;

	if (available > length) available = length;

	if (available > 0) {
		memcpy(data, &Element->Data[(std::size_t)Cursor], (std::size_t)available);
		Cursor += available;
	}

	if (read != nullptr) *read = (ULONG)available;

	return(available == length ? S_OK : S_FALSE);
}


HRESULT DocFileStreamClass::Write(void const * data, ULONG length, ULONG * written)
{
	if (written != nullptr) *written = 0;
	if (!Writable) return(STG_E_ACCESSDENIED);
	if (data == nullptr && length != 0) return(E_POINTER);

	if (length > 0) {
		if (Cursor + length > Element->Data.size()) {
			Element->Data.resize((std::size_t)(Cursor + length), 0);
		}
		memcpy(&Element->Data[(std::size_t)Cursor], data, length);
		Cursor += length;
		Owner->Touch();
	}

	if (written != nullptr) *written = length;

	return(S_OK);
}


HRESULT DocFileStreamClass::Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER * position)
{
	std::int64_t base = 0;

	switch (origin) {
		case STREAM_SEEK_SET:
			base = 0;
			break;

		case STREAM_SEEK_CUR:
			base = (std::int64_t)Cursor;
			break;

		case STREAM_SEEK_END:
			base = (std::int64_t)Element->Data.size();
			break;

		default:
			return(STG_E_INVALIDFUNCTION);
	}

	std::int64_t const target = base + (std::int64_t)move.QuadPart;
	if (target < 0) return(STG_E_INVALIDFUNCTION);

	Cursor = (std::uint64_t)target;
	if (position != nullptr) position->QuadPart = Cursor;

	return(S_OK);
}


HRESULT DocFileStreamClass::SetSize(ULARGE_INTEGER size)
{
	if (!Writable) return(STG_E_ACCESSDENIED);

	Element->Data.resize((std::size_t)size.QuadPart, 0);
	Owner->Touch();

	return(S_OK);
}


HRESULT DocFileStreamClass::CopyTo(IStream * target, ULARGE_INTEGER length, ULARGE_INTEGER * read, ULARGE_INTEGER * written)
{
	if (read != nullptr) read->QuadPart = 0;
	if (written != nullptr) written->QuadPart = 0;
	if (target == nullptr) return(E_POINTER);

	std::uint64_t remaining = length.QuadPart;
	std::uint64_t moved = 0;

	while (remaining > 0 && Cursor < Element->Data.size()) {
		std::uint64_t block = Element->Data.size() - Cursor;
		if (block > remaining) block = remaining;
		if (block > 0x10000) block = 0x10000;

		ULONG out = 0;
		HRESULT const result = target->Write(&Element->Data[(std::size_t)Cursor], (ULONG)block, &out);
		if (FAILED(result)) return(result);

		Cursor += out;
		moved += out;
		remaining -= out;

		if (out < block) break;
	}

	if (read != nullptr) read->QuadPart = moved;
	if (written != nullptr) written->QuadPart = moved;

	return(S_OK);
}


HRESULT DocFileStreamClass::Commit(DWORD)
{
	return(S_OK);
}


HRESULT DocFileStreamClass::Revert(void)
{
	return(S_OK);
}


HRESULT DocFileStreamClass::LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
{
	return(STG_E_INVALIDFUNCTION);
}


HRESULT DocFileStreamClass::UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
{
	return(STG_E_INVALIDFUNCTION);
}


HRESULT DocFileStreamClass::Stat(STATSTG * statstg, DWORD statflag)
{
	if (statstg == nullptr) return(E_POINTER);

	memset(statstg, 0, sizeof(*statstg));
	statstg->type = STGTY_STREAM;
	statstg->cbSize.QuadPart = Element->Data.size();

	if (statflag != STATFLAG_NONAME) {
		std::size_t const bytes = (Element->Name.size() + 1) * sizeof(OLECHAR);
		statstg->pwcsName = (LPOLESTR)CoTaskMemAlloc(bytes);
		if (statstg->pwcsName == nullptr) return(E_OUTOFMEMORY);
		memcpy(statstg->pwcsName, Element->Name.c_str(), bytes);
	}

	return(S_OK);
}


HRESULT DocFileStreamClass::Clone(IStream ** stream)
{
	if (stream == nullptr) return(E_POINTER);

	DocFileStreamClass * const copy = new DocFileStreamClass(Owner, Element, Writable);
	copy->Cursor = Cursor;
	*stream = copy;

	return(S_OK);
}


DocFileClass::DocFileClass(char const * path, bool writable) :
	Path(path != nullptr ? path : ""),
	Writable(writable),
	IsDirty(false),
	RefCount(1)
{
	memset(&Class, 0, sizeof(Class));
}


DocFileClass::~DocFileClass(void)
{
	/*
	** A storage opened in direct mode is written as it goes on Windows, so a caller that
	** never commits still leaves its work on disk. Nothing is rewritten when the storage was
	** only read from.
	*/
	if (Writable && IsDirty) {
		Write_File();
	}
}


HRESULT DocFileClass::QueryInterface(REFIID riid, void ** object)
{
	if (object == nullptr) return(E_POINTER);

	*object = nullptr;
	if (riid == IID_IUnknown || riid == IID_IStorage) {
		*object = (IStorage *)this;
		AddRef();
		return(S_OK);
	}

	return(E_NOINTERFACE);
}


ULONG DocFileClass::AddRef(void)
{
	return((ULONG)InterlockedIncrement(&RefCount));
}


ULONG DocFileClass::Release(void)
{
	LONG const remaining = InterlockedDecrement(&RefCount);

	if (remaining == 0) {
		delete this;
	}

	return((ULONG)remaining);
}


std::shared_ptr<DocFileElementType> DocFileClass::Find(std::wstring const & name) const
{
	for (std::shared_ptr<DocFileElementType> const & element : Elements) {
		if (Compare_Names(element->Name, name) == 0) return(element);
	}

	return(nullptr);
}


HRESULT DocFileClass::CreateStream(OLECHAR const * name, DWORD mode, DWORD, DWORD, IStream ** stream)
{
	if (stream != nullptr) *stream = nullptr;
	if (name == nullptr || stream == nullptr) return(E_POINTER);
	if (!Writable) return(STG_E_ACCESSDENIED);

	std::wstring const wanted = Wide_String(name);
	if (wanted.empty() || wanted.size() > NAME_MAX_CHARS) return(STG_E_INVALIDNAME);

	std::shared_ptr<DocFileElementType> element = Find(wanted);

	if (element != nullptr) {
		if ((mode & STGM_CREATE) == 0) return(STG_E_FILEALREADYEXISTS);
		element->Data.clear();
	} else {
		element = std::make_shared<DocFileElementType>();
		element->Name = wanted;
		Elements.push_back(element);
	}

	IsDirty = true;
	*stream = new DocFileStreamClass(this, element, true);

	return(S_OK);
}


HRESULT DocFileClass::OpenStream(OLECHAR const * name, void *, DWORD mode, DWORD, IStream ** stream)
{
	if (stream != nullptr) *stream = nullptr;
	if (name == nullptr || stream == nullptr) return(E_POINTER);

	std::shared_ptr<DocFileElementType> const element = Find(Wide_String(name));
	if (element == nullptr) return(STG_E_FILENOTFOUND);

	bool const wantswrite = ((mode & (STGM_WRITE | STGM_READWRITE)) != 0);
	if (wantswrite && !Writable) return(STG_E_ACCESSDENIED);

	*stream = new DocFileStreamClass(this, element, wantswrite);

	return(S_OK);
}


HRESULT DocFileClass::CreateStorage(OLECHAR const *, DWORD, DWORD, DWORD, IStorage ** storage)
{
	if (storage != nullptr) *storage = nullptr;

	return(STG_E_UNIMPLEMENTEDFUNCTION);
}


HRESULT DocFileClass::OpenStorage(OLECHAR const *, IStorage *, DWORD, SNB, DWORD, IStorage ** storage)
{
	if (storage != nullptr) *storage = nullptr;

	return(STG_E_UNIMPLEMENTEDFUNCTION);
}


#if defined(_WIN32)
HRESULT DocFileClass::CopyTo(DWORD, IID const *, SNB, IStorage *)
{
	return(STG_E_UNIMPLEMENTEDFUNCTION);
}


HRESULT DocFileClass::MoveElementTo(OLECHAR const *, IStorage *, LPOLESTR, DWORD)
{
	return(STG_E_UNIMPLEMENTEDFUNCTION);
}


HRESULT DocFileClass::EnumElements(DWORD, void *, DWORD, IEnumSTATSTG ** enumerator)
{
	if (enumerator != nullptr) *enumerator = nullptr;

	return(STG_E_UNIMPLEMENTEDFUNCTION);
}
#endif


HRESULT DocFileClass::Commit(DWORD)
{
	if (!Writable) return(STG_E_ACCESSDENIED);
	if (!IsDirty) return(S_OK);

	if (!Write_File()) return(STG_E_WRITEFAULT);

	IsDirty = false;
	return(S_OK);
}


HRESULT DocFileClass::Revert(void)
{
	return(S_OK);
}


HRESULT DocFileClass::DestroyElement(OLECHAR const * name)
{
	if (name == nullptr) return(E_POINTER);
	if (!Writable) return(STG_E_ACCESSDENIED);

	std::wstring const wanted = Wide_String(name);

	for (std::size_t index = 0; index < Elements.size(); index++) {
		if (Compare_Names(Elements[index]->Name, wanted) != 0) continue;

		Elements.erase(Elements.begin() + index);
		IsDirty = true;
		return(S_OK);
	}

	return(STG_E_FILENOTFOUND);
}


HRESULT DocFileClass::RenameElement(OLECHAR const * from, OLECHAR const * to)
{
	if (from == nullptr || to == nullptr) return(E_POINTER);
	if (!Writable) return(STG_E_ACCESSDENIED);

	std::wstring const wanted = Wide_String(to);
	if (wanted.empty() || wanted.size() > NAME_MAX_CHARS) return(STG_E_INVALIDNAME);
	if (Find(wanted) != nullptr) return(STG_E_FILEALREADYEXISTS);

	std::shared_ptr<DocFileElementType> const element = Find(Wide_String(from));
	if (element == nullptr) return(STG_E_FILENOTFOUND);

	element->Name = wanted;
	IsDirty = true;

	return(S_OK);
}


HRESULT DocFileClass::SetElementTimes(OLECHAR const *, FILETIME const *, FILETIME const *, FILETIME const *)
{
	return(S_OK);
}


HRESULT DocFileClass::SetClass(REFCLSID classid)
{
	Class = classid;
	IsDirty = true;

	return(S_OK);
}


HRESULT DocFileClass::SetStateBits(DWORD, DWORD)
{
	return(S_OK);
}


HRESULT DocFileClass::Stat(STATSTG * statstg, DWORD statflag)
{
	if (statstg == nullptr) return(E_POINTER);

	memset(statstg, 0, sizeof(*statstg));
	statstg->type = STGTY_STORAGE;
	statstg->clsid = Class;

	if (statflag != STATFLAG_NONAME) {
		std::size_t const bytes = (sizeof(RootEntryName) / sizeof(wchar_t)) * sizeof(OLECHAR);
		statstg->pwcsName = (LPOLESTR)CoTaskMemAlloc(bytes);
		if (statstg->pwcsName == nullptr) return(E_OUTOFMEMORY);
		memcpy(statstg->pwcsName, RootEntryName, bytes);
	}

	return(S_OK);
}


/*
** Reads the whole file into memory and hands it to the parser.
*/
bool DocFileClass::Read_File(void)
{
	HANDLE const file = CreateFileA(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return(false);

	DWORD const size = GetFileSize(file, nullptr);
	std::vector<unsigned char> image;

	if (size != INVALID_FILE_SIZE && size > 0) {
		image.resize(size);

		DWORD got = 0;
		if (!ReadFile(file, image.data(), size, &got, nullptr) || got != size) {
			CloseHandle(file);
			return(false);
		}
	}

	CloseHandle(file);

	return(Parse(image));
}


bool DocFileClass::Write_File(void)
{
	std::vector<unsigned char> image;
	Serialize(image);

	HANDLE const file = CreateFileA(Path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return(false);

	std::size_t offset = 0;
	bool ok = true;

	while (offset < image.size()) {
		DWORD const block = (DWORD)((image.size() - offset > 0x100000) ? 0x100000 : (image.size() - offset));
		DWORD written = 0;

		if (!WriteFile(file, image.data() + offset, block, &written, nullptr) || written != block) {
			ok = false;
			break;
		}

		offset += written;
	}

	if (!CloseHandle(file)) ok = false;

	return(ok);
}


/*
** Builds the sibling tree the directory entries are read through.
**
** The entries are handed over already in name order, so the tree is the balanced one that
** repeated halving produces. Every node is black except those on the deepest level, which
** are leaves whose parents are black: that is a valid red-black colouring for a tree whose
** leaves sit on the bottom two levels, which halving always gives.
*/
std::uint32_t Build_Sibling_Tree(std::vector<std::uint32_t> const & order, std::size_t first, std::size_t last,
	unsigned depth, std::vector<unsigned> & depths, std::vector<std::uint32_t> & left, std::vector<std::uint32_t> & right)
{
	if (first >= last) return(NOSTREAM);

	std::size_t const middle = first + (last - first) / 2;
	std::uint32_t const node = order[middle];

	depths[node] = depth;
	left[node] = Build_Sibling_Tree(order, first, middle, depth + 1, depths, left, right);
	right[node] = Build_Sibling_Tree(order, middle + 1, last, depth + 1, depths, left, right);

	return(node);
}


void DocFileClass::Serialize(std::vector<unsigned char> & image) const
{
	std::size_t const count = Elements.size();

	/*
	** Place every stream. A stream shorter than the cutoff belongs to the mini stream, which
	** the root entry owns; anything at or above it takes whole sectors of its own. A reader
	** decides which of the two to follow from the recorded size alone, so the cutoff has to
	** be honoured rather than merely recorded.
	*/
	struct PlacementType {
		std::uint32_t Start;
		std::uint32_t Sectors;
		bool Mini;
	};

	std::vector<PlacementType> placement(count);
	std::uint64_t ministreamsize = 0;
	std::uint32_t datasectors = 0;

	for (std::size_t index = 0; index < count; index++) {
		std::uint64_t const size = Elements[index]->Data.size();

		if (size == 0) {
			placement[index] = { ENDOFCHAIN, 0, false };
		} else if (size < MINI_CUTOFF) {
			std::uint32_t const sectors = Round_Up(size, MINI_SECTOR_SIZE);
			placement[index] = { (std::uint32_t)(ministreamsize / MINI_SECTOR_SIZE), sectors, true };
			ministreamsize += (std::uint64_t)sectors * MINI_SECTOR_SIZE;
		} else {
			std::uint32_t const sectors = Round_Up(size, SECTOR_SIZE);
			placement[index] = { datasectors, sectors, false };
			datasectors += sectors;
		}
	}

	std::uint32_t const minisectors = (std::uint32_t)(ministreamsize / MINI_SECTOR_SIZE);
	std::uint32_t const ministreamsectors = Round_Up(ministreamsize, SECTOR_SIZE);
	std::uint32_t const minifatsectors = Round_Up(minisectors, FAT_PER_SECTOR);
	std::uint32_t const directorysectors = Round_Up((std::uint64_t)count + 1, DIRECTORY_PER_SECTOR);

	std::uint32_t const ministreamstart = datasectors;
	std::uint32_t const minifatstart = ministreamstart + ministreamsectors;
	std::uint32_t const directorystart = minifatstart + minifatsectors;
	std::uint32_t const allocated = directorystart + directorysectors;

	/*
	** The allocation table has to describe itself and the table that locates it, so the two
	** counts are settled together before either is placed.
	*/
	std::uint32_t fatsectors = 1;
	std::uint32_t difatsectors = 0;

	for (int guard = 0; guard < 64; guard++) {
		std::uint32_t const total = allocated + fatsectors + difatsectors;
		std::uint32_t neededfat = Round_Up(total, FAT_PER_SECTOR);
		if (neededfat < 1) neededfat = 1;

		std::uint32_t const neededdifat = (neededfat > DIFAT_IN_HEADER)
			? Round_Up(neededfat - DIFAT_IN_HEADER, DIFAT_PER_SECTOR) : 0;

		if (neededfat <= fatsectors && neededdifat <= difatsectors) break;

		if (neededfat > fatsectors) fatsectors = neededfat;
		if (neededdifat > difatsectors) difatsectors = neededdifat;
	}

	std::uint32_t const fatstart = allocated;
	std::uint32_t const difatstart = fatstart + fatsectors;
	std::uint32_t const sectorcount = difatstart + difatsectors;

	/*
	** Chain everything through the allocation table.
	*/
	std::vector<std::uint32_t> fat((std::size_t)fatsectors * FAT_PER_SECTOR, FREESECT);

	auto chain = [&fat](std::uint32_t start, std::uint32_t length) {
		for (std::uint32_t step = 0; step < length; step++) {
			fat[start + step] = (step + 1 < length) ? (start + step + 1) : ENDOFCHAIN;
		}
	};

	for (std::size_t index = 0; index < count; index++) {
		if (!placement[index].Mini && placement[index].Sectors > 0) {
			chain(placement[index].Start, placement[index].Sectors);
		}
	}

	chain(ministreamstart, ministreamsectors);
	chain(minifatstart, minifatsectors);
	chain(directorystart, directorysectors);

	for (std::uint32_t step = 0; step < fatsectors; step++) fat[fatstart + step] = FATSECT;
	for (std::uint32_t step = 0; step < difatsectors; step++) fat[difatstart + step] = DIFSECT;

	std::vector<std::uint32_t> minifat((std::size_t)minifatsectors * FAT_PER_SECTOR, FREESECT);

	for (std::size_t index = 0; index < count; index++) {
		if (!placement[index].Mini) continue;

		std::uint32_t const start = placement[index].Start;
		std::uint32_t const length = placement[index].Sectors;

		for (std::uint32_t step = 0; step < length; step++) {
			minifat[start + step] = (step + 1 < length) ? (start + step + 1) : ENDOFCHAIN;
		}
	}

	/*
	** The directory. Entry zero is the root, which owns the mini stream; the streams follow
	** in the order they were created, linked into a tree ordered by name.
	*/
	std::vector<std::uint32_t> order(count);
	for (std::size_t index = 0; index < count; index++) order[index] = (std::uint32_t)(index + 1);

	for (std::size_t outer = 1; outer < count; outer++) {
		std::uint32_t const held = order[outer];
		std::size_t inner = outer;

		while (inner > 0 && Compare_Names(Elements[order[inner - 1] - 1]->Name, Elements[held - 1]->Name) > 0) {
			order[inner] = order[inner - 1];
			inner--;
		}
		order[inner] = held;
	}

	std::vector<unsigned> depths(count + 1, 0);
	std::vector<std::uint32_t> left(count + 1, NOSTREAM);
	std::vector<std::uint32_t> right(count + 1, NOSTREAM);

	std::uint32_t const child = Build_Sibling_Tree(order, 0, count, 0, depths, left, right);

	unsigned deepest = 0;
	for (std::size_t index = 1; index <= count; index++) {
		if (depths[index] > deepest) deepest = depths[index];
	}

	std::vector<unsigned char> directory((std::size_t)directorysectors * SECTOR_SIZE, 0);

	for (std::size_t index = 0; index <= count; index++) {
		unsigned char * const entry = &directory[index * DIRECTORY_ENTRY_SIZE];
		std::wstring const name = (index == 0) ? Wide_String(RootEntryName) : Elements[index - 1]->Name;

		for (std::size_t character = 0; character < name.size(); character++) {
			Put_U16(entry + character * 2, (std::uint16_t)name[character]);
		}
		Put_U16(entry + 0x40, (std::uint16_t)((name.size() + 1) * 2));

		entry[0x42] = (index == 0) ? OBJECT_ROOT : OBJECT_STREAM;
		entry[0x43] = (index != 0 && depths[index] == deepest && deepest > 0) ? COLOR_RED : COLOR_BLACK;

		Put_U32(entry + 0x44, (index == 0) ? NOSTREAM : left[index]);
		Put_U32(entry + 0x48, (index == 0) ? NOSTREAM : right[index]);
		Put_U32(entry + 0x4C, (index == 0) ? child : NOSTREAM);

		if (index == 0) {
			memcpy(entry + 0x50, &Class, sizeof(CLSID));
			Put_U32(entry + 0x74, (minisectors > 0) ? ministreamstart : ENDOFCHAIN);
			Put_U64(entry + 0x78, ministreamsize);
		} else {
			Put_U32(entry + 0x74, placement[index - 1].Start);
			Put_U64(entry + 0x78, Elements[index - 1]->Data.size());
		}
	}

	/*
	** Any directory entry beyond the ones in use has to read as unallocated rather than as a
	** stream named by whatever the padding held.
	*/
	for (std::size_t index = count + 1; index < (std::size_t)directorysectors * DIRECTORY_PER_SECTOR; index++) {
		unsigned char * const entry = &directory[index * DIRECTORY_ENTRY_SIZE];

		entry[0x42] = OBJECT_UNALLOCATED;
		entry[0x43] = COLOR_BLACK;
		Put_U32(entry + 0x44, NOSTREAM);
		Put_U32(entry + 0x48, NOSTREAM);
		Put_U32(entry + 0x4C, NOSTREAM);
	}

	/*
	** Lay the file out: the header, then the sectors in the order they were allocated above.
	*/
	image.assign((std::size_t)HEADER_SIZE + (std::size_t)sectorcount * SECTOR_SIZE, 0);

	unsigned char * const header = image.data();
	memcpy(header, DocFileSignature, sizeof(DocFileSignature));
	Put_U16(header + 0x18, 0x003E);
	Put_U16(header + 0x1A, 3);
	Put_U16(header + 0x1C, 0xFFFE);
	Put_U16(header + 0x1E, 9);
	Put_U16(header + 0x20, 6);
	Put_U32(header + 0x2C, fatsectors);
	Put_U32(header + 0x30, directorystart);
	Put_U32(header + 0x38, MINI_CUTOFF);
	Put_U32(header + 0x3C, (minifatsectors > 0) ? minifatstart : ENDOFCHAIN);
	Put_U32(header + 0x40, minifatsectors);
	Put_U32(header + 0x44, (difatsectors > 0) ? difatstart : ENDOFCHAIN);
	Put_U32(header + 0x48, difatsectors);

	for (std::uint32_t slot = 0; slot < DIFAT_IN_HEADER; slot++) {
		Put_U32(header + 0x4C + slot * 4, (slot < fatsectors) ? (fatstart + slot) : FREESECT);
	}

	unsigned char * const sectors = image.data() + HEADER_SIZE;

	for (std::size_t index = 0; index < count; index++) {
		if (placement[index].Mini || placement[index].Sectors == 0) continue;

		memcpy(sectors + (std::size_t)placement[index].Start * SECTOR_SIZE,
			Elements[index]->Data.data(), Elements[index]->Data.size());
	}

	for (std::size_t index = 0; index < count; index++) {
		if (!placement[index].Mini) continue;

		memcpy(sectors + (std::size_t)ministreamstart * SECTOR_SIZE
			+ (std::size_t)placement[index].Start * MINI_SECTOR_SIZE,
			Elements[index]->Data.data(), Elements[index]->Data.size());
	}

	for (std::size_t slot = 0; slot < minifat.size(); slot++) {
		Put_U32(sectors + (std::size_t)minifatstart * SECTOR_SIZE + slot * 4, minifat[slot]);
	}

	memcpy(sectors + (std::size_t)directorystart * SECTOR_SIZE, directory.data(), directory.size());

	for (std::size_t slot = 0; slot < fat.size(); slot++) {
		Put_U32(sectors + (std::size_t)fatstart * SECTOR_SIZE + slot * 4, fat[slot]);
	}

	for (std::uint32_t sector = 0; sector < difatsectors; sector++) {
		unsigned char * const block = sectors + (std::size_t)(difatstart + sector) * SECTOR_SIZE;

		for (std::uint32_t slot = 0; slot < DIFAT_PER_SECTOR; slot++) {
			std::uint32_t const which = DIFAT_IN_HEADER + sector * DIFAT_PER_SECTOR + slot;
			Put_U32(block + slot * 4, (which < fatsectors) ? (fatstart + which) : FREESECT);
		}

		Put_U32(block + DIFAT_PER_SECTOR * 4,
			(sector + 1 < difatsectors) ? (difatstart + sector + 1) : ENDOFCHAIN);
	}
}


/*
** Walks a sector chain and returns the bytes it holds, up to the recorded length. A chain
** that leaves the file, repeats itself, or ends early is a corrupt file rather than a short
** read, so it fails outright.
*/
bool Gather_Chain(std::vector<unsigned char> const & image, std::uint32_t sectorsize, std::uint32_t base,
	std::vector<std::uint32_t> const & table, std::uint32_t start, std::uint64_t length,
	std::vector<unsigned char> & out)
{
	out.clear();
	out.reserve((std::size_t)length);

	std::uint32_t sector = start;
	std::uint64_t visited = 0;

	while (out.size() < length) {
		if (sector >= MAXREGSECT || sector >= table.size()) return(false);
		if (visited++ > table.size()) return(false);

		std::size_t const offset = (std::size_t)base + (std::size_t)sector * sectorsize;

		std::uint64_t take = length - out.size();
		if (take > sectorsize) take = sectorsize;

		if (offset + take > image.size()) return(false);

		out.insert(out.end(), image.begin() + offset, image.begin() + offset + (std::size_t)take);
		sector = table[sector];
	}

	return(true);
}


bool DocFileClass::Parse(std::vector<unsigned char> const & image)
{
	if (image.size() < HEADER_SIZE) return(false);
	if (memcmp(image.data(), DocFileSignature, sizeof(DocFileSignature)) != 0) return(false);

	std::uint16_t const sectorshift = Get_U16(&image[0x1E]);
	std::uint16_t const minishift = Get_U16(&image[0x20]);

	if (sectorshift != 9 && sectorshift != 12) return(false);
	if (minishift != 6) return(false);

	std::uint32_t const sectorsize = 1u << sectorshift;
	std::uint32_t const minisize = 1u << minishift;
	std::uint32_t const cutoff = Get_U32(&image[0x38]);
	std::uint32_t const perfat = sectorsize / 4;

	std::uint32_t const fatsectors = Get_U32(&image[0x2C]);
	std::uint32_t const directorystart = Get_U32(&image[0x30]);
	std::uint32_t const minifatstart = Get_U32(&image[0x3C]);
	std::uint32_t const minifatsectors = Get_U32(&image[0x40]);
	std::uint32_t const difatstart = Get_U32(&image[0x44]);
	std::uint32_t const difatsectors = Get_U32(&image[0x48]);

	/*
	** Sector zero begins one sector in, which is the 512 byte header for a version 3 file and
	** the header plus its padding for a version 4 one.
	*/
	std::uint32_t const base = sectorsize;
	if (image.size() < base) return(false);

	std::uint32_t const available = (std::uint32_t)((image.size() - base) / sectorsize);

	auto sector_at = [&](std::uint32_t sector) -> unsigned char const * {
		if (sector >= available) return(nullptr);
		return(image.data() + base + (std::size_t)sector * sectorsize);
	};

	/*
	** Collect the sectors the allocation table lives in, from the header's own list and then
	** from the chain of table sectors that continues it.
	*/
	std::vector<std::uint32_t> fatlocations;

	for (std::uint32_t slot = 0; slot < DIFAT_IN_HEADER && fatlocations.size() < fatsectors; slot++) {
		std::uint32_t const where = Get_U32(&image[0x4C + slot * 4]);
		if (where >= MAXREGSECT) break;
		fatlocations.push_back(where);
	}

	std::uint32_t difat = difatstart;

	for (std::uint32_t step = 0; step < difatsectors && fatlocations.size() < fatsectors; step++) {
		unsigned char const * const block = sector_at(difat);
		if (block == nullptr) return(false);

		for (std::uint32_t slot = 0; slot < perfat - 1 && fatlocations.size() < fatsectors; slot++) {
			std::uint32_t const where = Get_U32(block + slot * 4);
			if (where >= MAXREGSECT) continue;
			fatlocations.push_back(where);
		}

		difat = Get_U32(block + (perfat - 1) * 4);
	}

	if (fatlocations.size() != fatsectors) return(false);

	std::vector<std::uint32_t> fat;
	fat.reserve((std::size_t)fatsectors * perfat);

	for (std::uint32_t const where : fatlocations) {
		unsigned char const * const block = sector_at(where);
		if (block == nullptr) return(false);

		for (std::uint32_t slot = 0; slot < perfat; slot++) {
			fat.push_back(Get_U32(block + slot * 4));
		}
	}

	/*
	** The directory is a chain of its own, and its length is not recorded in a version 3
	** header, so it is followed to the end.
	*/
	std::vector<unsigned char> entries;
	std::uint32_t sector = directorystart;
	std::uint32_t guard = 0;

	while (sector < MAXREGSECT) {
		unsigned char const * const block = sector_at(sector);
		if (block == nullptr) return(false);
		if (guard++ > available) return(false);

		entries.insert(entries.end(), block, block + sectorsize);

		if (sector >= fat.size()) return(false);
		sector = fat[sector];
	}

	std::size_t const entrycount = entries.size() / DIRECTORY_ENTRY_SIZE;
	if (entrycount == 0) return(false);
	if (entries[0x42] != OBJECT_ROOT) return(false);

	memcpy(&Class, &entries[0x50], sizeof(CLSID));

	std::uint32_t const ministart = Get_U32(&entries[0x74]);
	std::uint64_t const ministreamsize = Get_U64(&entries[0x78]);

	std::vector<unsigned char> ministream;

	if (ministreamsize > 0) {
		if (!Gather_Chain(image, sectorsize, base, fat, ministart, ministreamsize, ministream)) return(false);

		// A mini sector is read whole, so a recorded length that stops inside one is padded out.
		ministream.resize((std::size_t)Round_Up(ministream.size(), minisize) * minisize, 0);
	}

	std::vector<std::uint32_t> minifat;

	if (minifatsectors > 0) {
		minifat.reserve((std::size_t)minifatsectors * perfat);
		sector = minifatstart;

		for (std::uint32_t step = 0; step < minifatsectors; step++) {
			unsigned char const * const block = sector_at(sector);
			if (block == nullptr) return(false);

			for (std::uint32_t slot = 0; slot < perfat; slot++) {
				minifat.push_back(Get_U32(block + slot * 4));
			}

			if (sector >= fat.size()) return(false);
			sector = fat[sector];
		}
	}

	/*
	** Take the streams the root storage holds. The tree is walked rather than the entry array
	** scanned, so an entry that belongs to a sub-storage is not mistaken for one of ours.
	*/
	std::vector<std::uint32_t> pending;
	std::vector<bool> seen(entrycount, false);

	std::uint32_t const child = Get_U32(&entries[0x4C]);
	if (child < entrycount) pending.push_back(child);

	while (!pending.empty()) {
		std::uint32_t const index = pending.back();
		pending.pop_back();

		if (index >= entrycount || seen[index]) continue;
		seen[index] = true;

		unsigned char const * const entry = &entries[(std::size_t)index * DIRECTORY_ENTRY_SIZE];

		std::uint32_t const leftid = Get_U32(entry + 0x44);
		std::uint32_t const rightid = Get_U32(entry + 0x48);
		if (leftid < entrycount) pending.push_back(leftid);
		if (rightid < entrycount) pending.push_back(rightid);

		if (entry[0x42] != OBJECT_STREAM) continue;

		std::uint16_t namebytes = Get_U16(entry + 0x40);
		if (namebytes < 2 || namebytes > 64) continue;

		std::wstring name;
		for (std::uint16_t character = 0; character + 2 < namebytes; character += 2) {
			name.push_back((wchar_t)Get_U16(entry + character));
		}

		std::shared_ptr<DocFileElementType> element = std::make_shared<DocFileElementType>();
		element->Name = name;

		std::uint64_t const size = Get_U64(entry + 0x78);
		std::uint32_t const start = Get_U32(entry + 0x74);

		if (size > 0) {
			bool ok;

			if (size < cutoff) {
				ok = Gather_Chain(ministream, minisize, 0, minifat, start, size, element->Data);
			} else {
				ok = Gather_Chain(image, sectorsize, base, fat, start, size, element->Data);
			}

			if (!ok) return(false);
		}

		Elements.push_back(element);
	}

	/*
	** The directory order is the tree's, which the walk above does not preserve. Sorting the
	** elements makes an opened file present its streams the way a written one does.
	*/
	for (std::size_t outer = 1; outer < Elements.size(); outer++) {
		std::shared_ptr<DocFileElementType> held = Elements[outer];
		std::size_t inner = outer;

		while (inner > 0 && Compare_Names(Elements[inner - 1]->Name, held->Name) > 0) {
			Elements[inner] = Elements[inner - 1];
			inner--;
		}
		Elements[inner] = held;
	}

	return(true);
}

}	// namespace


HRESULT DocFile_Create(char const * path, DWORD mode, IStorage ** storage)
{
	if (storage != nullptr) *storage = nullptr;
	if (path == nullptr || storage == nullptr) return(E_POINTER);

	if ((mode & STGM_CREATE) == 0) {
		HANDLE const existing = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (existing != INVALID_HANDLE_VALUE) {
			CloseHandle(existing);
			return(STG_E_FILEALREADYEXISTS);
		}
	}

	DocFileClass * const created = new DocFileClass(path, true);

	/*
	** Windows leaves a file behind the moment the storage is created, and the engine relies
	** on nothing more than that until it commits. An empty container is written now so that a
	** creation the host refuses is reported here rather than at the commit.
	*/
	if (!created->Write_File()) {
		created->Release();
		return(STG_E_WRITEFAULT);
	}

	*storage = created;
	return(S_OK);
}


HRESULT DocFile_Open(char const * path, DWORD mode, IStorage ** storage)
{
	if (storage != nullptr) *storage = nullptr;
	if (path == nullptr || storage == nullptr) return(E_POINTER);

	bool const writable = ((mode & (STGM_WRITE | STGM_READWRITE)) != 0);

	HANDLE const probe = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (probe == INVALID_HANDLE_VALUE) return(STG_E_FILENOTFOUND);
	CloseHandle(probe);

	DocFileClass * const opened = new DocFileClass(path, writable);

	if (!opened->Read_File()) {
		opened->Release();
		return(STG_E_INVALIDHEADER);
	}

	*storage = opened;
	return(S_OK);
}


HRESULT DocFile_Is_Storage_File(char const * path)
{
	if (path == nullptr) return(E_POINTER);

	HANDLE const file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return(STG_E_FILENOTFOUND);

	unsigned char signature[sizeof(DocFileSignature)];
	DWORD got = 0;
	bool const read = (ReadFile(file, signature, sizeof(signature), &got, nullptr) != FALSE);
	CloseHandle(file);

	if (!read || got != sizeof(signature)) return(S_FALSE);

	return(memcmp(signature, DocFileSignature, sizeof(signature)) == 0 ? S_OK : S_FALSE);
}
