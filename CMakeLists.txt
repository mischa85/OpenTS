cmake_minimum_required(VERSION 3.23)

project(OpenTS VERSION 0.2.0 LANGUAGES C CXX)

# A SemVer prerelease label, which project() cannot carry because it takes numbers only. Set it
# to "beta1" and the version reads 0.1.0-beta1; leave it empty outside a prerelease series. The
# manual's release registry is checked against both this and the project version.
set(OPENTS_VERSION_PRERELEASE "")

# Marks a build published under the version it declares, so that it is named by that version
# alone. Every other build names the commit it came from as well.
option(OPENTS_OFFICIAL_BUILD "Build as an official release of the declared version" OFF)

# The toolchain decides what is configured here; nothing refuses a compiler, and what a
# target needs it asks for. docs/BUILDING.md records which of them are supported.
message(STATUS "OpenTS: configuring for ${CMAKE_CXX_COMPILER_ID} on ${CMAKE_SYSTEM_NAME}.")

# Lets a target's FOLDER place it in a Visual Studio solution folder. The bgfx submodule sets
# this as well, so declare it here rather than inherit it from a vendored build.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC AND MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "OpenTS requires MSVC 19.30 or newer for C++20.")
endif()

# The tree declares imports and exports with __declspec throughout, which a compiler with
# a GCC front end accepts only with the Microsoft extensions enabled. clang-cl takes the
# MSVC front end and needs nothing.
if(NOT MSVC)
    add_compile_options(-fms-extensions)
endif()

# 32-bit x86 is the only target with an x87 unit to fall back on, which is what the
# supported build spends /arch:SSE2 avoiding. Which processor name such a target reports
# varies by host and toolchain -- i686 natively, x86_64 when a 64-bit host is asked for a
# 32-bit build -- so the compiler is asked what it is compiling for instead of the name
# being matched.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#if !defined(__i386__) && !defined(_M_IX86)
#error not 32-bit x86
#endif
int main(void) { return 0; }
" OPENTS_TARGET_X86_32)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    # A save records pointer identities at a fixed width, but the members and raw
    # structures around them still travel at the build's own widths, and the packed
    # version stamp that saves and network packets carry is the same either way.
    message(WARNING
        "OpenTS: this build has ${CMAKE_SIZEOF_VOID_P}-byte pointers. Saved games it "
        "writes are not interchangeable with a supported 32-bit build's, and nothing in "
        "the version stamp distinguishes them.")
endif()

# Only the Windows toolchains have a resource compiler.
if(WIN32)
    enable_language(RC)
endif()

string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" OPENTS_ARCH)
if(NOT OPENTS_ARCH)
    set(OPENTS_ARCH "unknown")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENTS_GAME_DIR "${CMAKE_SOURCE_DIR}/Run" CACHE PATH "Tiberian Sun game data directory")

#
# ---------------------------------------------------------
# Build identity stamp
# ---------------------------------------------------------
#
# The version resources, the version displays, and the debug log's opening banner all report
# the version and commit that produced the binary. Stamping runs once here so the headers exist
# before the first compile, and again per build through OpenTSBuildStamp so that committing does
# not require a reconfigure to be reflected.
set(OPENTS_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(OPENTS_VERSION_HEADER "${OPENTS_GENERATED_DIR}/opents_version.h")
set(OPENTS_STAMP_HEADER "${OPENTS_GENERATED_DIR}/opents_build.h")
file(MAKE_DIRECTORY "${OPENTS_GENERATED_DIR}")

set(OPENTS_STAMP_ARGS
    "-DOPENTS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
    "-DOPENTS_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
    "-DOPENTS_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
    "-DOPENTS_VERSION_PRERELEASE=${OPENTS_VERSION_PRERELEASE}"
    "-DOPENTS_OFFICIAL_BUILD=${OPENTS_OFFICIAL_BUILD}"
    "-DOPENTS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DOPENTS_VERSION_HEADER=${OPENTS_VERSION_HEADER}"
    "-DOPENTS_STAMP_HEADER=${OPENTS_STAMP_HEADER}"
    "-DOPENTS_ARCH=${OPENTS_ARCH}"
    -P "${CMAKE_SOURCE_DIR}/cmake/GitStamp.cmake"
)

execute_process(COMMAND ${CMAKE_COMMAND} ${OPENTS_STAMP_ARGS})

add_custom_target(OpenTSBuildStamp ALL
    COMMAND ${CMAKE_COMMAND} ${OPENTS_STAMP_ARGS}
    COMMENT "Stamping the build identity"
    VERBATIM
)

add_subdirectory(thirdparty)
add_subdirectory(code)
add_subdirectory(code/language)

enable_testing()
add_subdirectory(tests)
