cmake_minimum_required(VERSION 3.23)

project(OpenTS VERSION 0.2.0 LANGUAGES C CXX)

# A SemVer prerelease label, which project() cannot carry because it takes numbers only. Set it
# to "beta1" and the version reads 0.1.0-beta1; leave it empty outside a prerelease series. The
# manual's release registry is checked against both this and the project version.
set(OPENTS_VERSION_PRERELEASE "")

# Marks a build published under the version it declares, so that it is named by that version
# alone. Every other build names the commit it came from as well.
option(OPENTS_OFFICIAL_BUILD "Build as an official release of the declared version" OFF)

# Windows builds the full engine and requires the supported toolchain. Other platforms build
# the dependencies and the test suite while the engine is ported; docs/PORTING.md records the
# plan and status.
if(WIN32)
    if(NOT MSVC OR MSVC_VERSION LESS 1930)
        message(FATAL_ERROR "OpenTS requires the Visual Studio 2022 MSVC toolchain.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TS_RUN_DIR "${CMAKE_SOURCE_DIR}/Run" CACHE PATH "Tiberian Sun run directory")

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

if(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
  set(OPENTS_ARCH "x86")
else()
  set(OPENTS_ARCH "x64")
endif()

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

# Compile options shared by the test harnesses; the engine's own live in code/CMakeLists.txt.
# The floating point choice mirrors the engine's /fp:precise intent on every toolchain.
if(MSVC)
    set(OPENTS_HARNESS_COMPILE_OPTIONS /EHsc /Zc:__cplusplus $<$<CONFIG:Debug>:/MTd> $<$<CONFIG:Release>:/MT>)
    set(OPENTS_HARNESS_FP_OPTIONS /arch:SSE2 /fp:precise)
else()
    # -fms-extensions covers the tree's __declspec(property) accessors.
    set(OPENTS_HARNESS_COMPILE_OPTIONS -fms-extensions)
    set(OPENTS_HARNESS_FP_OPTIONS -ffp-contract=off)
endif()

# The CPU detection reads CPUID, so its harness only builds for an x86 processor.
set(OPENTS_TARGET_IS_X86 FALSE)
if(MSVC)
    set(OPENTS_TARGET_IS_X86 TRUE)
elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64|i386")
    set(OPENTS_TARGET_IS_X86 TRUE)
elseif(NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
    set(OPENTS_TARGET_IS_X86 TRUE)
endif()

# The Win32 substitute every non-Windows target builds on: the compatibility layer, the
# compound-file writer, and the disc image reader, with the native POSIX helpers.
if(NOT WIN32)
    add_library(OpenTSPosixCompat STATIC
        "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32stub.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32process.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32timer.cpp"
        "${CMAKE_SOURCE_DIR}/code/docfile.cpp"
        "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
        "${CMAKE_SOURCE_DIR}/code/isofile.cpp"
        "${CMAKE_SOURCE_DIR}/code/isodisc.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32window.cpp"
        "${CMAKE_SOURCE_DIR}/code/hostyield.cpp"
        "${CMAKE_SOURCE_DIR}/code/platform_posix.cpp"
    )
    target_compile_features(OpenTSPosixCompat PUBLIC cxx_std_20)
    target_include_directories(OpenTSPosixCompat PUBLIC "${CMAKE_SOURCE_DIR}/code")
    target_compile_definitions(OpenTSPosixCompat PUBLIC NOMINMAX)
    target_compile_options(OpenTSPosixCompat PUBLIC ${OPENTS_HARNESS_COMPILE_OPTIONS})
endif()

add_subdirectory(thirdparty)
if(WIN32)
    add_subdirectory(code)
    add_subdirectory(code/language)
endif()

enable_testing()
add_subdirectory(tests)
