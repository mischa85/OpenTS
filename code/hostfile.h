/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Where a name the engine writes actually lives on a POSIX host, and what discs are
// mounted beside it. filesystem.cpp opens through this, and the Win32 substitute's
// remaining file entry points -- the attribute, rename, directory, and search calls that
// never take a stream -- resolve their paths through it too, so a name means the same
// thing whichever of them is asked.

#pragma once

#if !defined(_WIN32)

#include "iso9660.h"

#include <memory>
#include <string>
#include <vector>


/*
 * One mounted disc. A game is spread over several, so the list is ordered and a name is
 * answered by the first disc carrying it; each disc offers its installed data directory
 * ahead of its own root, which is the order cdfile.cpp searches an image in.
 */
struct MountedImageType
{
	std::shared_ptr<ISOVolumeClass> Volume;
	std::vector<std::string> Directories;
};


/// <summary>Whether the host has anything at all under this path.</summary>
bool Host_Path_Present(std::string const & path);

/// <summary>Rebuilds a path the engine wrote as one the host will accept.</summary>
/// <remarks>
/// Backslashes become slashes, a component the host spells differently is matched without
/// regard to case, and a bare name resolves into the persistent directory when the host
/// has one. Host_Persistent_Root states what that directory is for.
/// </remarks>
std::string Host_File_Path(char const * path);

/// <summary>The directory that survives the session, with its separator, or empty.</summary>
/// <remarks>
/// Only the browser target has one: everything else the engine can reach there is gone
/// with the tab, so a single directory is mounted on IndexedDB before main runs and the
/// saves land in it. The overlay stands in front of the game directory rather than beside
/// it, so a name that exists as game data still resolves to the game data.
/// </remarks>
std::string const & Host_Persistent_Root(void);

/// <summary>Whether a resolved host path lies inside the persistent directory.</summary>
bool Host_Path_Is_Persistent(std::string const & path);

/// <summary>Records that the persistent directory has been written to.</summary>
void Host_Persistent_Touched(void);

/// <summary>Hands the persistent directory to the host to store, if it was touched.</summary>
void Host_Flush_Persistent(void);

/// <summary>Mounts a disc image, which is then searched for every name opened.</summary>
bool Mount_Disc_Image(char const * location);

/// <summary>Releases every mounted image.</summary>
void Unmount_Disc_Images(void);

/// <summary>The mounted discs, mounting the configured ones on the first call.</summary>
std::vector<MountedImageType> const & Mounted_Disc_Images(void);

/// <summary>Rebuilds a path the engine wrote as one relative to an image root.</summary>
/// <returns>bool; false for a path that climbs out of the volume.</returns>
bool Image_Relative_Path(char const * path, std::string & inside);

/// <summary>Joins one of a disc's search directories to a path relative to its root.</summary>
std::string Image_Search_Path(std::string const & directory, std::string const & inside);

/// <summary>Finds a file across the mounted discs, first disc and directory first.</summary>
/// <returns>The volume carrying it, or nothing when no disc has it.</returns>
std::shared_ptr<ISOVolumeClass> Image_File_Entry(char const * filename, ISOEntryClass & entry);

#endif
