/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Name resolution for the POSIX hosts. hostfile.h states what this answers and who asks.

#include "hostfile.h"

#if !defined(_WIN32)

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>


bool Host_Path_Present(std::string const & path)
{
	struct stat info;

	return(::lstat(path.c_str(), &info) == 0);
}


/*
 * Matches a path against what the host actually holds, without regard to case.
 *
 * The engine asks for TIBSUN.MIX in upper case while the assets a player supplies may be
 * spelled either way, and Windows would not care. The host might: Emscripten's virtual
 * filesystem and Linux are case-sensitive even though APFS and NTFS are not. So a path that
 * exists as spelled is used as spelled -- one system call, and the common case costs nothing
 * -- and only a path that does not is walked component by component, each missing component
 * matched against its directory's entries without regard to case. A component with no match
 * at all stays as it was spelled, which is what lets a file be created under the name the
 * caller chose rather than under a neighbor's. Two entries differing only in case resolve to
 * the first in sort order, so repeated lookups on one directory agree with each other.
 */
static std::string Resolve_Case(std::string const & translated)
{
	if (translated.empty() || Host_Path_Present(translated)) return(translated);

	std::string resolved;
	std::size_t cursor = 0;

	if (translated[0] == '/') {
		resolved = "/";
		cursor = 1;
	}

	while (cursor < translated.size()) {
		std::size_t separator = translated.find('/', cursor);
		if (separator == std::string::npos) separator = translated.size();

		std::string component(translated, cursor, separator - cursor);

		if (!component.empty() && component != "." && component != ".." && !Host_Path_Present(resolved + component)) {
			DIR * const directory = ::opendir(resolved.empty() ? "." : resolved.c_str());

			if (directory != nullptr) {
				std::string match;

				for (struct dirent * item = ::readdir(directory); item != nullptr; item = ::readdir(directory)) {
					if (::strcasecmp(item->d_name, component.c_str()) != 0) continue;
					if (match.empty() || item->d_name < match) match = item->d_name;
				}

				::closedir(directory);
				if (!match.empty()) component = match;
			}
		}

		resolved += component;
		if (separator < translated.size()) resolved += '/';
		cursor = separator + 1;
	}

	return(resolved);
}


/*
 * Copying the game data into a browser's database would cost hundreds of megabytes of quota
 * to store what the page already has, so only this one directory is mounted on IndexedDB.
 */
#define OPENTS_PERSISTENT_DIRECTORY "/save"

std::string const & Host_Persistent_Root(void)
{
	static std::string const root = []() -> std::string {
		struct stat info;

		if (::stat(OPENTS_PERSISTENT_DIRECTORY, &info) == 0 && S_ISDIR(info.st_mode)) {
			return(OPENTS_PERSISTENT_DIRECTORY "/");
		}

		return(std::string());
	}();

	return(root);
}


bool Host_Path_Is_Persistent(std::string const & path)
{
	std::string const & root = Host_Persistent_Root();

	return(!root.empty() && path.compare(0, root.size(), root) == 0);
}


/*
 * A bare name is looked for in the persistent directory first and in the game directory
 * after, and a name that is in neither resolves into the persistent directory. That last rule
 * is what puts a saved game somewhere it survives the tab without the file layer having to be
 * told which opens are writes: a file that is about to be created exists nowhere, and a file
 * that is about to be read exists where it was written.
 */
std::string Host_File_Path(char const * path)
{
	std::string translated(path != nullptr ? path : "");

	for (char & character : translated) {
		if (character == '\\') character = '/';
	}

	std::string const & root = Host_Persistent_Root();

	if (!root.empty() && !translated.empty() && translated.find('/') == std::string::npos) {
		std::string const persistent = root + translated;

		// The spelling the caller wrote, in both directories, before either is matched
		// without regard to case: the engine spells the game data the way it is on disk, and
		// that path should cost two system calls rather than a scan of two directories.
		if (Host_Path_Present(persistent)) return(persistent);
		if (Host_Path_Present(translated)) return(translated);

		std::string const matched = Resolve_Case(persistent);
		if (Host_Path_Present(matched)) return(matched);

		std::string const local = Resolve_Case(translated);
		if (Host_Path_Present(local)) return(local);

		return(persistent);
	}

	return(Resolve_Case(translated));
}


static bool PersistentDirty = false;

void Host_Persistent_Touched(void)
{
	PersistentDirty = true;
}


/*
 * IndexedDB is reached asynchronously and the engine cannot wait on it, so the transfer is
 * started here and finishes on its own. The page counts the ones that complete, which is what
 * an automated check waits for before it reloads.
 */
void Host_Flush_Persistent(void)
{
#if defined(__EMSCRIPTEN__)
	if (!PersistentDirty) return;
	PersistentDirty = false;

	MAIN_THREAD_EM_ASM({
		if (typeof FS === "undefined") return;

		var again = function () {
			FS.syncfs(false, function (error) {
				if (error) {
					console.error("OpenTS: writing persistent storage failed: " + error);
				}
				Module.OpenTS_Syncs = (Module.OpenTS_Syncs || 0) + 1;

				if (Module.OpenTS_SyncAgain) {
					Module.OpenTS_SyncAgain = false;
					again();
				} else {
					Module.OpenTS_SyncRunning = false;
				}
			});
		};

		if (Module.OpenTS_SyncRunning) {
			Module.OpenTS_SyncAgain = true;
		} else {
			Module.OpenTS_SyncRunning = true;
			again();
		}
	});
#else
	PersistentDirty = false;
#endif
}


static std::vector<MountedImageType> & Image_Table(void)
{
	static std::vector<MountedImageType> images;

	return(images);
}

static bool ImageMountTried = false;


bool Mount_Disc_Image(char const * location)
{
	ImageMountTried = true;

	std::unique_ptr<ISOBlockSourceClass> source = ISO_Open_Location(location);
	if (!source) return(false);

	MountedImageType mounted;

	mounted.Volume = std::make_shared<ISOVolumeClass>();
	if (!mounted.Volume->Attach(std::move(source))) return(false);

	ISO_Search_Directories(*mounted.Volume, mounted.Directories);
	Image_Table().push_back(std::move(mounted));
	return(true);
}


void Unmount_Disc_Images(void)
{
	Image_Table().clear();
	ImageMountTried = true;
}


/*
 * Images are mounted on the first name the host cannot answer for, so that a build with no
 * image beside it pays a single failed lookup rather than a cost at startup -- and, on the
 * suspending transport, so that no fetch is attempted from a static constructor, where the
 * module is not yet inside a promising export.
 */
std::vector<MountedImageType> const & Mounted_Disc_Images(void)
{
	if (!ImageMountTried) {
		std::vector<std::string> locations;

		ISO_Image_Locations(locations);
		ImageMountTried = true;

		for (std::string const & location : locations) {
			if (Mount_Disc_Image(location.c_str())) {
				fprintf(stderr, "OpenTS: reading game data out of the image at %s.\n", location.c_str());
			} else {
				fprintf(stderr, "OpenTS: no image mounted from %s; only files the host answers "
					"for can be read.\n", location.c_str());
			}
			fflush(stderr);
		}
	}

	return(Image_Table());
}


/*
 * A path relative to the volume root has the drive letter and the leading separator a caller
 * may have written taken off, since the image root is what the engine would have been running
 * out of. A path that climbs out of the volume has no answer here and is refused rather than
 * clamped.
 */
bool Image_Relative_Path(char const * path, std::string & inside)
{
	inside.clear();

	if (path == nullptr) return(false);

	std::string translated(path);

	for (char & character : translated) {
		if (character == '\\') character = '/';
	}

	if (translated.size() >= 2 && translated[1] == ':') translated.erase(0, 2);

	std::size_t cursor = 0;

	while (cursor < translated.size()) {
		std::size_t separator = translated.find('/', cursor);
		if (separator == std::string::npos) separator = translated.size();

		std::string const component(translated, cursor, separator - cursor);
		cursor = separator + 1;

		if (component.empty() || component == ".") continue;
		if (component == "..") return(false);

		if (!inside.empty()) inside += '/';
		inside += component;
	}

	return(true);
}


std::string Image_Search_Path(std::string const & directory, std::string const & inside)
{
	if (directory.empty()) return(inside);
	if (inside.empty()) return(directory);

	return(directory + '/' + inside);
}


/*
 * The volumes are left mounted, so a handle taken out on the entry keeps reading from the one
 * it was found on for as long as it is open.
 */
std::shared_ptr<ISOVolumeClass> Image_File_Entry(char const * filename, ISOEntryClass & entry)
{
	std::string inside;
	if (!Image_Relative_Path(filename, inside) || inside.empty()) return(nullptr);

	for (MountedImageType const & image : Mounted_Disc_Images()) {
		for (std::string const & directory : image.Directories) {
			std::string const path = Image_Search_Path(directory, inside);

			if (image.Volume->Find(path.c_str(), entry) && !entry.IsDirectory) return(image.Volume);
		}
	}

	entry.Reset();
	return(nullptr);
}

#endif
