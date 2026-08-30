/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A reader for the resource directory of a Portable Executable image. The language
// library is a resource-only DLL, and every platform that cannot ask an operating system
// to map it still has to reach the strings and dialog templates inside it. Nothing here
// touches the operating system: the caller supplies the bytes, of a whole image or of a
// directory on its own, and the resource tree is walked in place.

#pragma once

#include <cstddef>
#include <cstdint>


/*
 * Names a resource type or a resource the way the directory itself does, either by number
 * or by a name string. The implicit conversions let a call site read as it would against
 * the Windows resource functions.
  */
class PEResourceNameClass
{
	public:
		PEResourceNameClass(unsigned int id) : ID(id), Name(nullptr) {}
		PEResourceNameClass(char const * name) : ID(0), Name(name) {}

		bool Is_Named(void) const {return(Name != nullptr);}
		unsigned int Get_ID(void) const {return(ID);}
		char const * Get_Name(void) const {return(Name);}

	private:

		unsigned int ID;
		char const * Name;
};


/*
 * The standard resource type numbers this reader's callers ask for. The directory stores
 * them as plain integers, so a type Windows has a name for is no different from one it
 * does not.
  */
enum PEResourceType {
	PE_RESOURCE_DIALOG = 5,
	PE_RESOURCE_STRING = 6,
	PE_RESOURCE_VERSION = 16
};


/*
 * Holds an image and answers questions about the resources in it. The image is copied on
 * load and kept until the object is destroyed, because a fetched resource is a pointer
 * into it -- the same lifetime a locked resource has on Windows.
  */
class PEResourceClass
{
	public:
		PEResourceClass(void);
		~PEResourceClass(void);

		PEResourceClass(PEResourceClass const &) = delete;
		PEResourceClass & operator = (PEResourceClass const &) = delete;

		/// <summary>Takes a copy of an image and locates its resource directory.</summary>
		/// <param name="image">The bytes of the whole file.</param>
		/// <param name="size">How many bytes the image occupies.</param>
		/// <returns>bool; true when the image parsed and carries a resource directory.</returns>
		bool Load(void const * image, std::size_t size);

		/// <summary>Takes a copy of a resource directory that stands on its own.</summary>
		/// <remarks>The addresses inside such a directory count from the start of the
		/// directory rather than from an image base, which is what they hold before a
		/// linker places the section they belong to.</remarks>
		/// <param name="directory">The bytes of the directory and the resources in it.</param>
		/// <param name="size">How many bytes the directory occupies.</param>
		/// <returns>bool; true when the directory was taken.</returns>
		bool Load_Directory(void const * directory, std::size_t size);

		void Unload(void);
		bool Is_Loaded(void) const {return(Image != nullptr);}

		/// <summary>Locates a resource and hands back a pointer into the image.</summary>
		/// <param name="type">Type of the resource wanted.</param>
		/// <param name="name">Name or number of the resource wanted.</param>
		/// <param name="size">Filled in with the size of the resource, when supplied.</param>
		/// <returns>The resource data, or NULL when it is not there.</returns>
		void const * Fetch_Resource(PEResourceNameClass const & type, PEResourceNameClass const & name,
			std::size_t * size = nullptr) const;

		/// <summary>Fetches one string out of the string table.</summary>
		/// <param name="id">Identifier of the string wanted.</param>
		/// <param name="buffer">Destination, always terminated when it has room for it.</param>
		/// <param name="size">Size of the destination in bytes.</param>
		/// <returns>Characters written, not counting the terminator; zero when there is no
		/// such string.</returns>
		int Fetch_String(unsigned int id, char * buffer, int size) const;

		/// <summary>Fetches one entry of the version resource's first translation.</summary>
		/// <param name="key">Name of the entry, such as "FileVersion".</param>
		/// <param name="buffer">Destination, always terminated when it has room for it.</param>
		/// <param name="size">Size of the destination in bytes.</param>
		/// <returns>bool; true when the entry was found.</returns>
		bool Fetch_Version_String(char const * key, char * buffer, int size) const;

	private:

		bool Locate_Directory(void);
		std::size_t Offset_For_RVA(std::uint32_t rva) const;

		bool Is_Within(std::size_t offset, std::size_t length) const;
		std::uint16_t Fetch_Short(std::size_t offset) const;
		std::uint32_t Fetch_Long(std::size_t offset) const;

		std::size_t Find_Entry(std::size_t directory, PEResourceNameClass const & wanted) const;
		std::size_t First_Entry(std::size_t directory) const;
		std::size_t Sub_Directory(std::size_t entry) const;
		std::size_t Fetch_Leaf(std::size_t entry, std::size_t * size) const;

		/*
		 * The image, its length, and the pieces of it the resource walk needs: the section
		 * table that turns a relative virtual address back into a file offset, and the root
		 * of the resource tree, which is also the base every offset inside the tree counts
		 * from. A directory loaded on its own has no section table, and its addresses are
		 * resolved against the directory instead.
		 */
		unsigned char * Image;
		std::size_t Size;
		std::size_t SectionOffset;
		unsigned int SectionCount;
		std::size_t DirectoryOffset;
		bool Standalone;
};
