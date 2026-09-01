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

# The Win32 substitute every non-Windows target builds on: the file layer, the compatibility
# layer, the compound-file writer, and the disc image reader, with the native POSIX helpers.
if(NOT WIN32)
    add_library(OpenTSPosixCompat STATIC
        "${CMAKE_SOURCE_DIR}/code/filesystem.cpp"
        "${CMAKE_SOURCE_DIR}/code/hostfile.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32stub.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32timer.cpp"
        "${CMAKE_SOURCE_DIR}/code/docfile.cpp"
        "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
        "${CMAKE_SOURCE_DIR}/code/isofile.cpp"
        "${CMAKE_SOURCE_DIR}/code/isodisc.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32codepage.cpp"
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

#
# ---------------------------------------------------------
# The native engine, compiled but not yet a supported target
# ---------------------------------------------------------
#
# Builds every engine source against the Win32 substitute, as the WebAssembly target does,
# with the clang flag set. A static library rather than an executable while the native host
# is still being written; docs/PORTING.md stage 5 records the state.
if(NOT WIN32 AND NOT EMSCRIPTEN)
    file(GLOB OPENTS_NATIVE_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/code/*.cpp" "${CMAKE_SOURCE_DIR}/code/*.c")
    file(GLOB OPENTS_VQALIB_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/code/vqalib/*.cpp")

    # The MIDL stubs are inputs to the Windows build only, and the compat family already
    # builds as OpenTSPosixCompat.
    list(FILTER OPENTS_NATIVE_SRC EXCLUDE REGEX "_i\\.c$")

    # The hosts live outside the library: the SDL host is the executable's own source, and
    # the windowless default yield belongs only to the harnesses.
    list(REMOVE_ITEM OPENTS_NATIVE_SRC
        "${CMAKE_SOURCE_DIR}/code/sdlhost.cpp"
        "${CMAKE_SOURCE_DIR}/code/hostyield.cpp"
    )

    add_library(OpenTSEngine STATIC ${OPENTS_NATIVE_SRC} ${OPENTS_VQALIB_SRC})
    target_compile_features(OpenTSEngine PRIVATE cxx_std_20)
    target_include_directories(OpenTSEngine PRIVATE
        "${CMAKE_SOURCE_DIR}/code"
        "${CMAKE_SOURCE_DIR}/code/vqalib"
        "${OPENTS_GENERATED_DIR}"
    )
    target_compile_definitions(OpenTSEngine PRIVATE NOMINMAX NO_BLOWFISH_DLL)
    target_compile_options(OpenTSEngine PRIVATE
        ${OPENTS_HARNESS_COMPILE_OPTIONS}
        ${OPENTS_HARNESS_FP_OPTIONS}
        -fno-strict-aliasing
        -fno-delete-null-pointer-checks
    )
    add_dependencies(OpenTSEngine OpenTSBuildStamp)

    # The audio backend speaks OpenAL on every non-Windows target; openal-soft is the
    # provider outside a browser. Homebrew installs it keg-only, so its prefix is named.
    if(APPLE)
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/openal-soft" "/usr/local/opt/openal-soft")
    endif()
    find_package(OpenAL CONFIG QUIET)
    if(OpenAL_FOUND)
        get_target_property(OPENTS_OPENAL_INCLUDE OpenAL::OpenAL INTERFACE_INCLUDE_DIRECTORIES)
        target_include_directories(OpenTSEngine PRIVATE ${OPENTS_OPENAL_INCLUDE})
    else()
        message(STATUS "OpenAL not found; the native build carries no audio output")
        target_compile_definitions(OpenTSEngine PRIVATE OPENTS_NO_AUDIO_BACKEND)
    endif()

    set(BGFX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/code/bgfxbackend.cpp" PROPERTIES
        INCLUDE_DIRECTORIES
            "${BGFX_ROOT}/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bx/include;${BGFX_ROOT}/examples/common/imgui"
        COMPILE_DEFINITIONS
            "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>"
    )

    # The language resources, compiled from the same script the Visual Studio build turns
    # into Language.dll, exactly as the WebAssembly target carries them.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        set(OPENTS_LANGUAGE_DIR "${CMAKE_SOURCE_DIR}/code/language")
        set(OPENTS_LANGUAGE_SOURCE "${OPENTS_GENERATED_DIR}/languageimage.cpp")

        add_custom_command(
            OUTPUT "${OPENTS_LANGUAGE_SOURCE}"
            COMMAND "${Python3_EXECUTABLE}"
                "${OPENTS_LANGUAGE_DIR}/rcimage.py"
                "${OPENTS_LANGUAGE_DIR}/language.rc"
                --include "${OPENTS_LANGUAGE_DIR}"
                --include "${OPENTS_GENERATED_DIR}"
                --source "${OPENTS_LANGUAGE_SOURCE}"
            DEPENDS
                "${OPENTS_LANGUAGE_DIR}/rcimage.py"
                "${OPENTS_LANGUAGE_DIR}/language.rc"
                "${OPENTS_LANGUAGE_DIR}/language.h"
                "${OPENTS_LANGUAGE_DIR}/language.hm"
                "${OPENTS_VERSION_HEADER}"
                "${OPENTS_STAMP_HEADER}"
            COMMENT "Compiling the language resources"
            VERBATIM
        )
    endif()

    # The native executable: the SDL host wrapped around the engine. Unsupported and in
    # progress; docs/PORTING.md owns the status.
    find_package(SDL2 QUIET CONFIG)
    if(SDL2_FOUND AND Python3_FOUND)
        add_executable(OpenTSNative
            "${CMAKE_SOURCE_DIR}/code/sdlhost.cpp"
            "${OPENTS_LANGUAGE_SOURCE}"
        )
        target_include_directories(OpenTSNative PRIVATE
            "${CMAKE_SOURCE_DIR}/code"
            "${OPENTS_GENERATED_DIR}"
        )
        target_compile_definitions(OpenTSNative PRIVATE NOMINMAX NO_BLOWFISH_DLL)
        target_compile_options(OpenTSNative PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})

        if(TARGET SDL2::SDL2)
            target_link_libraries(OpenTSNative PRIVATE SDL2::SDL2)
        else()
            target_include_directories(OpenTSNative PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(OpenTSNative PRIVATE ${SDL2_LIBRARIES})
        endif()

        find_package(Threads REQUIRED)

        # The engine archive is loaded whole: the class factories and globals it registers
        # from static initializers must all be present, not only what the host names.
        if(APPLE)
            target_link_options(OpenTSNative PRIVATE
                "SHELL:-Wl,-force_load,$<TARGET_FILE:OpenTSEngine>")
            target_link_libraries(OpenTSNative PRIVATE
                "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
                "-framework IOKit" "-framework CoreFoundation"
                "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo")
        else()
            target_link_options(OpenTSNative PRIVATE
                "SHELL:-Wl,--whole-archive" "$<TARGET_FILE:OpenTSEngine>" "SHELL:-Wl,--no-whole-archive")
        endif()

        add_dependencies(OpenTSNative OpenTSEngine)
        target_link_libraries(OpenTSNative PRIVATE
            OpenTSEngine
            bgfx bx bimg
            Threads::Threads
        )
        if(OpenAL_FOUND)
            target_link_libraries(OpenTSNative PRIVATE OpenAL::OpenAL)
        endif()

        set_target_properties(OpenTSNative PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            OUTPUT_NAME "opents"
        )
    endif()
endif()

enable_testing()
add_subdirectory(tests)
