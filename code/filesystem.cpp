/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The file layer's backends. filesystem.h states the contract they answer.
//
// There are two of them off Windows -- an ordinary host file and an entry inside a mounted
// disc image -- and Open_File_Stream is the one place that chooses. A read of a name the
// host has no file for falls through to the images; anything that writes stays on the host,
// because that is where a file the engine writes belongs and an image is read only.
//
// The Windows build has the host backend alone. Disc mounting is the native port's answer
// to game data that never left the CD, and a Windows install has the drive itself.

#include "filesystem.h"

#include <cstring>
#include <ctime>


/*
 * Windows counts hundred-nanosecond intervals from the start of 1601 and the POSIX hosts
 * count seconds from the start of 1970; this is the distance between the two epochs.
 */
static long long const FiletimeEpochOffset = 116444736000000000LL;


#if defined(_WIN32)

#include <windows.h>


/*
 * The host file on Windows. It makes the calls RawFileClass used to make inline, so the
 * supported build opens, shares, and flags its files exactly as it did before the file
 * layer existed.
 */
class HostFileStreamClass : public FileStreamClass
{
	public:
		HostFileStreamClass(HANDLE handle, bool existed) :
			Handle(handle),
			WasThere(existed)
		{
		}

		virtual ~HostFileStreamClass(void) override
		{
			if (Handle != INVALID_HANDLE_VALUE) ::CloseHandle(Handle);
		}

		virtual bool Read(void * buffer, unsigned int size, unsigned int & actual) override
		{
			DWORD got = 0;

			if (!::ReadFile(Handle, buffer, (DWORD)size, &got, NULL)) {
				actual = (unsigned int)got;
				LastError = (int)::GetLastError();
				return(false);
			}

			actual = (unsigned int)got;
			return(true);
		}

		virtual bool Write(void const * buffer, unsigned int size, unsigned int & actual) override
		{
			DWORD put = 0;

			if (!::WriteFile(Handle, buffer, (DWORD)size, &put, NULL)) {
				actual = (unsigned int)put;
				LastError = (int)::GetLastError();
				return(false);
			}

			actual = (unsigned int)put;
			return(true);
		}

		virtual bool Seek(long long distance, FileOriginType origin, long long & position) override
		{
			LARGE_INTEGER moved;
			LARGE_INTEGER wanted;

			wanted.QuadPart = distance;

			DWORD method = FILE_BEGIN;
			if (origin == FILE_ORIGIN_CURRENT) method = FILE_CURRENT;
			if (origin == FILE_ORIGIN_END) method = FILE_END;

			if (!::SetFilePointerEx(Handle, wanted, &moved, method)) {
				LastError = (int)::GetLastError();
				return(false);
			}

			position = moved.QuadPart;
			return(true);
		}

		virtual bool Status(FileStatusType & status) const override
		{
			BY_HANDLE_FILE_INFORMATION information;

			if (!::GetFileInformationByHandle(Handle, &information)) {
				LastError = (int)::GetLastError();
				return(false);
			}

			status.Size = ((unsigned long long)information.nFileSizeHigh << 32) | information.nFileSizeLow;
			status.Creation = Ticks_Of(information.ftCreationTime);
			status.Access = Ticks_Of(information.ftLastAccessTime);
			status.Write = Ticks_Of(information.ftLastWriteTime);
			status.Identifier = ((unsigned long long)information.nFileIndexHigh << 32) | information.nFileIndexLow;
			status.Volume = (unsigned int)information.dwVolumeSerialNumber;
			status.Links = (unsigned int)information.nNumberOfLinks;
			status.IsReadOnly = ((information.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0);
			return(true);
		}

		virtual bool Set_Times(unsigned long long creation, unsigned long long access,
			unsigned long long write) override
		{
			FILETIME const created = Filetime_Of(creation);
			FILETIME const touched = Filetime_Of(access);
			FILETIME const written = Filetime_Of(write);

			if (!::SetFileTime(Handle, (creation != 0) ? &created : NULL,
				(access != 0) ? &touched : NULL, (write != 0) ? &written : NULL)) {
				LastError = (int)::GetLastError();
				return(false);
			}

			return(true);
		}

		virtual bool Truncate(void) override
		{
			if (!::SetEndOfFile(Handle)) {
				LastError = (int)::GetLastError();
				return(false);
			}

			return(true);
		}

		virtual bool Flush(void) override
		{
			if (!::FlushFileBuffers(Handle)) {
				LastError = (int)::GetLastError();
				return(false);
			}

			return(true);
		}

		virtual void Hint(FileHintType, unsigned long long, unsigned long long) override {}

		virtual bool Existed(void) const override { return(WasThere); }
		virtual int Error(void) const override { return(LastError); }
		virtual bool Declined(void) const override { return(false); }

	private:

		static unsigned long long Ticks_Of(FILETIME const & filetime)
		{
			return(((unsigned long long)filetime.dwHighDateTime << 32) | filetime.dwLowDateTime);
		}

		static FILETIME Filetime_Of(unsigned long long ticks)
		{
			FILETIME result;

			result.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
			result.dwHighDateTime = (DWORD)(ticks >> 32);
			return(result);
		}

		HANDLE Handle;
		bool WasThere;
		mutable int LastError = 0;
};


static int LayerError = 0;


std::unique_ptr<FileStreamClass> Open_File_Stream(char const * name, FileAccessType access,
	FileDispositionType disposition, bool sequential)
{
	LayerError = 0;

	if (name == nullptr || name[0] == '\0') {
		LayerError = ERROR_INVALID_PARAMETER;
		return(nullptr);
	}

	DWORD rights = GENERIC_READ;
	if (access == FILE_ACCESS_WRITE) rights = GENERIC_WRITE;
	if (access == FILE_ACCESS_READ_WRITE) rights = GENERIC_READ | GENERIC_WRITE;

	/*
	 * A stream opened only to read lets others read and write behind it; one that can write
	 * holds the file to itself. Those are the two rules RawFileClass already followed.
	 */
	DWORD const sharing = (access == FILE_ACCESS_READ) ? (FILE_SHARE_READ | FILE_SHARE_WRITE) : 0;

	DWORD creation = OPEN_EXISTING;
	switch (disposition) {
		case FILE_OPEN_EXISTING:		creation = OPEN_EXISTING; break;
		case FILE_OPEN_ALWAYS:			creation = OPEN_ALWAYS; break;
		case FILE_CREATE_ALWAYS:		creation = CREATE_ALWAYS; break;
		case FILE_CREATE_NEW:			creation = CREATE_NEW; break;
		case FILE_TRUNCATE_EXISTING:	creation = TRUNCATE_EXISTING; break;
	}

	DWORD flags = FILE_ATTRIBUTE_NORMAL;
	if (sequential) flags |= FILE_FLAG_SEQUENTIAL_SCAN;

	::SetLastError(NO_ERROR);

	HANDLE const handle = ::CreateFileA(name, rights, sharing, NULL, creation, flags, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		LayerError = (int)::GetLastError();
		return(nullptr);
	}

	bool const existed = (::GetLastError() == ERROR_ALREADY_EXISTS);

	return(std::unique_ptr<FileStreamClass>(new HostFileStreamClass(handle, existed)));
}


bool File_Entry_Exists(char const * name)
{
	LayerError = 0;

	if (name == nullptr || name[0] == '\0') return(false);

	HANDLE const handle = ::CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		LayerError = (int)::GetLastError();
		return(false);
	}

	::CloseHandle(handle);
	return(true);
}


bool Delete_File_Entry(char const * name)
{
	LayerError = 0;

	if (name == nullptr || name[0] == '\0') return(false);

	if (!::DeleteFileA(name)) {
		LayerError = (int)::GetLastError();
		return(false);
	}

	return(true);
}

#else

#include "hostfile.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif


static unsigned long long Ticks_From_Host(long long seconds, long nanoseconds)
{
	return((unsigned long long)(seconds * 10000000LL + nanoseconds / 100 + FiletimeEpochOffset));
}


class HostFileStreamClass : public FileStreamClass
{
	public:
		HostFileStreamClass(int descriptor, bool existed) :
			Descriptor(descriptor),
			WasThere(existed)
		{
		}

		virtual ~HostFileStreamClass(void) override
		{
			if (Descriptor >= 0) ::close(Descriptor);
			Host_Flush_Persistent();
		}

		/*
		 * A short host read is resumed rather than reported, because a caller asking a file
		 * on disk for a run of bytes expects all of them. Only the end of the file stops it.
		 */
		virtual bool Read(void * buffer, unsigned int size, unsigned int & actual) override
		{
			char * const cursor = (char *)buffer;
			unsigned int total = 0;

			while (total < size) {
				ssize_t const got = ::read(Descriptor, cursor + total, (size_t)(size - total));

				if (got < 0) {
					if (errno == EINTR) continue;
					LastError = errno;
					actual = total;
					return(false);
				}

				if (got == 0) break;
				total += (unsigned int)got;
			}

			actual = total;
			return(true);
		}

		virtual bool Write(void const * buffer, unsigned int size, unsigned int & actual) override
		{
			char const * const cursor = (char const *)buffer;
			unsigned int total = 0;

			while (total < size) {
				ssize_t const put = ::write(Descriptor, cursor + total, (size_t)(size - total));

				if (put < 0) {
					if (errno == EINTR) continue;
					LastError = errno;
					actual = total;
					return(false);
				}

				if (put == 0) break;
				total += (unsigned int)put;
			}

			actual = total;
			return(true);
		}

		virtual bool Seek(long long distance, FileOriginType origin, long long & position) override
		{
			int where = SEEK_SET;
			if (origin == FILE_ORIGIN_CURRENT) where = SEEK_CUR;
			if (origin == FILE_ORIGIN_END) where = SEEK_END;

			off_t const moved = ::lseek(Descriptor, (off_t)distance, where);
			if (moved < 0) {
				LastError = errno;
				return(false);
			}

			position = (long long)moved;
			return(true);
		}

		virtual bool Status(FileStatusType & status) const override
		{
			struct stat info;

			if (::fstat(Descriptor, &info) != 0) {
				LastError = errno;
				return(false);
			}

			status.Size = (unsigned long long)info.st_size;
			status.Creation = Ticks_From_Host(info.st_ctim.tv_sec, info.st_ctim.tv_nsec);
			status.Access = Ticks_From_Host(info.st_atim.tv_sec, info.st_atim.tv_nsec);
			status.Write = Ticks_From_Host(info.st_mtim.tv_sec, info.st_mtim.tv_nsec);
			status.Identifier = (unsigned long long)info.st_ino;
			status.Volume = (unsigned int)info.st_dev;
			status.Links = (unsigned int)info.st_nlink;
			status.IsReadOnly = ((info.st_mode & S_IWUSR) == 0);
			return(true);
		}

		/*
		 * The creation time is accepted and dropped: the host does not store one that can be
		 * set, and refusing the call would fail a caller that only wanted the write time.
		 */
		virtual bool Set_Times(unsigned long long creation, unsigned long long access,
			unsigned long long write) override
		{
			(void)creation;

			struct timespec times[2];

			times[0].tv_sec = 0;
			times[0].tv_nsec = UTIME_OMIT;
			times[1].tv_sec = 0;
			times[1].tv_nsec = UTIME_OMIT;

			if (access != 0) Host_Timespec(access, times[0]);
			if (write != 0) Host_Timespec(write, times[1]);

			if (::futimens(Descriptor, times) != 0) {
				LastError = errno;
				return(false);
			}

			return(true);
		}

		virtual bool Truncate(void) override
		{
			off_t const position = ::lseek(Descriptor, 0, SEEK_CUR);

			if (position < 0 || ::ftruncate(Descriptor, position) != 0) {
				LastError = errno;
				return(false);
			}

			return(true);
		}

		virtual bool Flush(void) override
		{
			if (::fsync(Descriptor) != 0) {
				LastError = errno;
				return(false);
			}

			return(true);
		}

		virtual void Hint(FileHintType, unsigned long long, unsigned long long) override {}

		virtual bool Existed(void) const override { return(WasThere); }
		virtual int Error(void) const override { return(LastError); }
		virtual bool Declined(void) const override { return(false); }

	private:

		static void Host_Timespec(unsigned long long filetime, struct timespec & when)
		{
			long long const ticks = (long long)filetime - FiletimeEpochOffset;

			when.tv_sec = (time_t)(ticks / 10000000LL);
			when.tv_nsec = (long)((ticks % 10000000LL) * 100LL);
		}

		int Descriptor;
		bool WasThere;
		mutable int LastError = 0;
};


/*
 * A file on a mounted disc. It has no descriptor and no position the host is keeping, so
 * both live here, and every write is refused because the volume is read only.
 */
class ImageFileStreamClass : public FileStreamClass
{
	public:
		ImageFileStreamClass(std::shared_ptr<ISOVolumeClass> volume, ISOEntryClass const & entry) :
			Volume(std::move(volume)),
			Image(entry)
		{
		}

		virtual ~ImageFileStreamClass(void) override {}

		/*
		 * The volume reports a transport failure the same way it reports the end of the file,
		 * so the count the file could have answered is worked out first and anything less
		 * than that is a failure rather than a quiet short read.
		 */
		virtual bool Read(void * buffer, unsigned int size, unsigned int & actual) override
		{
			WasDeclined = false;

			unsigned int const available = (Cursor < Image.Size) ? (unsigned int)(Image.Size - Cursor) : 0u;
			unsigned int const wanted = (size < available) ? size : available;

			int const got = Volume->Read(Image, Cursor, buffer, wanted);

			Cursor += (std::uint32_t)(got > 0 ? got : 0);
			actual = (unsigned int)(got > 0 ? got : 0);

			if (got < 0 || (unsigned int)got != wanted) {
				WasDeclined = ISODeferredReadClass::Declined_Now();
				LastError = EIO;
				return(false);
			}

			return(true);
		}

		virtual bool Write(void const *, unsigned int, unsigned int & actual) override
		{
			actual = 0;
			LastError = EROFS;
			return(false);
		}

		virtual bool Seek(long long distance, FileOriginType origin, long long & position) override
		{
			long long base = 0;
			if (origin == FILE_ORIGIN_CURRENT) base = (long long)Cursor;
			if (origin == FILE_ORIGIN_END) base = (long long)Image.Size;

			long long const wanted = base + distance;

			/*
			 * A seek past the end is allowed and reads there answer with nothing; a seek
			 * before the start is the one that fails.
			 */
			if (wanted < 0 || wanted > (long long)0xFFFFFFFFLL) {
				LastError = EINVAL;
				return(false);
			}

			Cursor = (std::uint32_t)wanted;
			position = wanted;
			return(true);
		}

		virtual bool Status(FileStatusType & status) const override
		{
			unsigned long long const recorded = File_Time_From_Dos_Date_Time(Image.DateTime);

			status.Size = (unsigned long long)Image.Size;
			status.Creation = recorded;
			status.Access = recorded;
			status.Write = recorded;
			status.Identifier = Image.Extents.empty() ? 0ull : (unsigned long long)Image.Extents.front().Start;
			status.Volume = 0;
			status.Links = 1;
			status.IsReadOnly = true;
			return(true);
		}

		virtual bool Set_Times(unsigned long long, unsigned long long, unsigned long long) override
		{
			LastError = EROFS;
			return(false);
		}

		virtual bool Truncate(void) override
		{
			LastError = EROFS;
			return(false);
		}

		virtual bool Flush(void) override
		{
			LastError = EROFS;
			return(false);
		}

		virtual void Hint(FileHintType hint, unsigned long long offset, unsigned long long length) override
		{
			if (!Volume || offset >= Image.Size) return;

			std::uint32_t const span = (length != 0)
				? (std::uint32_t)length : (Image.Size - (std::uint32_t)offset);

			ISOHintType claim = ISO_HINT_SEQUENTIAL;
			if (hint == FILE_HINT_SOON) claim = ISO_HINT_SOON;
			if (hint == FILE_HINT_DONE) claim = ISO_HINT_DONE;

			Volume->Hint(Image, claim, (std::uint32_t)offset, span);
		}

		virtual bool Existed(void) const override { return(true); }
		virtual int Error(void) const override { return(LastError); }
		virtual bool Declined(void) const override { return(WasDeclined); }

	private:

		std::shared_ptr<ISOVolumeClass> Volume;
		ISOEntryClass Image;
		std::uint32_t Cursor = 0;
		mutable int LastError = 0;
		bool WasDeclined = false;
};


static int LayerError = 0;


std::unique_ptr<FileStreamClass> Open_File_Stream(char const * name, FileAccessType access,
	FileDispositionType disposition, bool sequential)
{
	LayerError = 0;

	if (name == nullptr || name[0] == '\0') {
		LayerError = EINVAL;
		return(nullptr);
	}

	bool const wantsread = (access != FILE_ACCESS_WRITE);
	bool const wantswrite = (access != FILE_ACCESS_READ);

	int openflags = O_RDONLY;
	if (wantsread && wantswrite) openflags = O_RDWR;
	else if (wantswrite) openflags = O_WRONLY;

	bool truncates = false;

	switch (disposition) {
		case FILE_OPEN_EXISTING:
			break;

		case FILE_OPEN_ALWAYS:
			openflags |= O_CREAT;
			break;

		case FILE_CREATE_ALWAYS:
			openflags |= O_CREAT | O_TRUNC;
			truncates = true;
			break;

		case FILE_CREATE_NEW:
			openflags |= O_CREAT | O_EXCL;
			break;

		case FILE_TRUNCATE_EXISTING:
			openflags |= O_TRUNC;
			truncates = true;
			break;
	}

	if (truncates && !wantswrite) {
		LayerError = EINVAL;
		return(nullptr);
	}

	std::string const path = Host_File_Path(name);
	struct stat existing;
	bool const present = (::stat(path.c_str(), &existing) == 0);

	/*
	 * POSIX would hand back a descriptor for a directory, where Windows refuses one without
	 * backup semantics that nothing here asks for. The case is rejected rather than letting a
	 * caller read a directory as if it held bytes.
	 */
	if (present && S_ISDIR(existing.st_mode)) {
		LayerError = EISDIR;
		return(nullptr);
	}

	/*
	 * A name the host has no file for may still be on a mounted disc. The discs are read
	 * only, so only an open that would have read an existing file resolves there.
	 */
	if (!present && !wantswrite && (disposition == FILE_OPEN_EXISTING || disposition == FILE_OPEN_ALWAYS)) {
		ISOEntryClass found;
		std::shared_ptr<ISOVolumeClass> volume = Image_File_Entry(name, found);

		if (volume) {
			std::unique_ptr<ImageFileStreamClass> stream(new ImageFileStreamClass(std::move(volume), found));

			/*
			 * The disc is told what the directory lookup just established and the reads that
			 * follow cannot say: these bytes are one file, they are about to be read from
			 * front to back, and they end where the file does. A disc whose bytes are at hand
			 * does nothing with it; one being fetched over a network reads ahead from the
			 * first block rather than after the reads it would take to notice.
			 */
			stream->Hint(FILE_HINT_SEQUENTIAL, 0, 0);
			return(stream);
		}
	}

	int const descriptor = ::open(path.c_str(), openflags, (mode_t)0666);
	if (descriptor < 0) {
		LayerError = errno;
		return(nullptr);
	}

	if (wantswrite && Host_Path_Is_Persistent(path)) Host_Persistent_Touched();

	std::unique_ptr<FileStreamClass> stream(new HostFileStreamClass(descriptor, present));

	if (sequential) stream->Hint(FILE_HINT_SEQUENTIAL, 0, 0);
	return(stream);
}


bool File_Entry_Exists(char const * name)
{
	LayerError = 0;

	if (name == nullptr || name[0] == '\0') return(false);

	/*
	 * Opened rather than asked after, because a name the host holds but will not hand over
	 * is not available to the caller either.
	 */
	std::string const path = Host_File_Path(name);
	int const descriptor = ::open(path.c_str(), O_RDONLY);

	if (descriptor >= 0) {
		struct stat info;
		bool const isfile = (::fstat(descriptor, &info) == 0 && !S_ISDIR(info.st_mode));

		::close(descriptor);
		if (isfile) return(true);
	}

	ISOEntryClass found;
	if (Image_File_Entry(name, found)) return(true);

	LayerError = ENOENT;
	return(false);
}


bool Delete_File_Entry(char const * name)
{
	LayerError = 0;

	if (name == nullptr || name[0] == '\0') {
		LayerError = EINVAL;
		return(false);
	}

	std::string const path = Host_File_Path(name);

	if (::unlink(path.c_str()) != 0) {
		LayerError = errno;
		return(false);
	}

	if (Host_Path_Is_Persistent(path)) Host_Persistent_Touched();
	Host_Flush_Persistent();
	return(true);
}

#endif


int File_Layer_Error(void)
{
	return(LayerError);
}


/*
 * The DOS packing, which the archive headers and the file dialogs still speak: the date in
 * the high word as year since 1980, month, and day; the time in the low word as hour,
 * minute, and the second halved. Both conversions go through UTC, as the Win32 pair they
 * replace does, so a date read back is the date that was written wherever the game runs.
 */
static bool Calendar_From_File_Time(unsigned long long filetime, struct tm & parts)
{
	long long const ticks = (long long)filetime - FiletimeEpochOffset;
	long long seconds = ticks / 10000000LL;

	if (ticks % 10000000LL < 0) seconds--;

	time_t const when = (time_t)seconds;

#if defined(_WIN32)
	return(::gmtime_s(&parts, &when) == 0);
#else
	return(::gmtime_r(&when, &parts) != nullptr);
#endif
}


unsigned int Dos_Date_Time_From_File_Time(unsigned long long filetime)
{
	struct tm parts;

	if (filetime == 0 || !Calendar_From_File_Time(filetime, parts)) return(0);

	int const year = parts.tm_year + 1900;
	if (year < 1980 || year > 2107) return(0);

	unsigned int const date = (unsigned int)(((year - 1980) << 9) | ((parts.tm_mon + 1) << 5) | parts.tm_mday);
	unsigned int const time = (unsigned int)((parts.tm_hour << 11) | (parts.tm_min << 5) | (parts.tm_sec / 2));

	return((date << 16) | time);
}


unsigned long long File_Time_From_Dos_Date_Time(unsigned int datetime)
{
	if (datetime == 0) return(0);

	struct tm parts;
	memset(&parts, 0, sizeof(parts));

	parts.tm_year = (int)((datetime >> 25) & 0x7F) + 1980 - 1900;
	parts.tm_mon = (int)((datetime >> 21) & 0x0F) - 1;
	parts.tm_mday = (int)((datetime >> 16) & 0x1F);
	parts.tm_hour = (int)((datetime >> 11) & 0x1F);
	parts.tm_min = (int)((datetime >> 5) & 0x3F);
	parts.tm_sec = (int)(datetime & 0x1F) * 2;

#if defined(_WIN32)
	time_t const seconds = ::_mkgmtime(&parts);
#else
	time_t const seconds = ::timegm(&parts);
#endif
	if (seconds == (time_t)-1) return(0);

	return((unsigned long long)((long long)seconds * 10000000LL + FiletimeEpochOffset));
}
