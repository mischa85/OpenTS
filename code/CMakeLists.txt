# Limit the generated solution to supported configurations.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

# OpenTS builds against 32-bit pointers only. Win32 x86 and Emscripten wasm32 both provide
# them, along with the 32-bit long the engine's layouts assume.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "OpenTS must be built with 32-bit pointers. Reconfigure with -A Win32, or with the "
        "Emscripten toolchain file for the WebAssembly target.")
endif()

#
# ---------------------------------------------------------
# Directories that build as their own project
# ---------------------------------------------------------
#
set(SUBPROJECT_DIRS
    "language"
    "vqalib"
)

#
# ---------------------------------------------------------
# Gather all potential OpenTS files
# ---------------------------------------------------------
#
file(GLOB_RECURSE OPENTS_RAW CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"

    # Submodules of OpenTS
    "${CMAKE_CURRENT_SOURCE_DIR}/wolapi/*"
)

#
# ---------------------------------------------------------
# Sources the WebAssembly target drops entirely
# ---------------------------------------------------------
#
# Each of these builds on a Windows-only facility with no browser counterpart, and is left
# out whole rather than compiled hollow so the omission is visible in the build rather than
# buried in a header.
#
#   wonline.cpp -- Westwood Online: the internet lobby, its chat, ladders, and the World
#                  Domination Tour, reached through ATL and a COM client for a service
#                  retired in 2004. wonlinestub.cpp keeps what the rest of the engine
#                  references and reports that nothing happened.
#
set(OPENTS_EMSCRIPTEN_EXCLUDED
    "wonline.cpp"
)

#
# ---------------------------------------------------------
# Remove files that belong to a subproject
# ---------------------------------------------------------
#
set(OPENTS_SRC "")
foreach(f ${OPENTS_RAW})
    set(EXCLUDED FALSE)
    foreach(dir ${SUBPROJECT_DIRS})
        if(f MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/${dir}/.*")
            set(EXCLUDED TRUE)
        endif()
    endforeach()

    # Resource scripts are a Visual Studio toolchain input with no Emscripten equivalent.
    if(EMSCRIPTEN AND f MATCHES "\\.rc$")
        set(EXCLUDED TRUE)
    endif()

    if(EMSCRIPTEN)
        foreach(name ${OPENTS_EMSCRIPTEN_EXCLUDED})
            if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
                set(EXCLUDED TRUE)
                message(STATUS "${PROJECT_NAME}: WebAssembly target excludes ${name}")
            endif()
        endforeach()
    endif()

    if(NOT EXCLUDED)
        list(APPEND OPENTS_SRC "${f}")
    endif()
endforeach()

#
# ---------------------------------------------------------
# Create actual OpenTS executable (Win32)
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating '${PROJECT_NAME}' executable...")

# The WIN32 marker selects the Windows GUI entry point and means nothing elsewhere.
if(EMSCRIPTEN)
    add_executable(OpenTS ${OPENTS_SRC})
else()
    add_executable(OpenTS WIN32 ${OPENTS_SRC})
endif()

target_compile_features(OpenTS PRIVATE cxx_std_20)

#
# ---------------------------------------------------------
# Output paths
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting binary output paths...")

set_target_properties(OpenTS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"

    # Built under the shipped names, so the executable, the symbol file the debug record
    # names, and the map reach the run directory without a rename.
    OUTPUT_NAME_DEBUG "GameD"
    OUTPUT_NAME_RELEASE "Game"
    PDB_NAME_DEBUG "GameD"
    PDB_NAME_RELEASE "Game"
)

set_target_properties(OpenTS PROPERTIES
    VS_DEBUGGER_COMMAND "${TS_RUN_DIR}/$<TARGET_FILE_NAME:OpenTS>"
    VS_DEBUGGER_WORKING_DIRECTORY "${TS_RUN_DIR}"
)

#
# ---------------------------------------------------------
# Compile flags
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying compiler flags...")

if(EMSCRIPTEN)

set(OPENTS_COMPILE_OPTIONS

    # WebAssembly arithmetic is strict IEEE-754: single precision stays single precision and
    # nothing is reassociated, which is what /arch:SSE2 and /fp:precise buy under MSVC. So the
    # floating-point model needs nothing asked for here, but no fast-math option may ever be
    # added: the simulation runs in lockstep across a network game and any relaxation
    # desynchronizes it.
    -fno-fast-math

    # MSVC does not optimize on the strict aliasing rule and the inherited code puns types
    # freely, so clang is held to the assumption the code was written under.
    -fno-strict-aliasing

    # MSVC's wchar_t is 16 bits and clang's is 32. The engine's structures are
    # layout-sensitive, so the narrow form is kept.
    -fshort-wchar

    # C++ exception handling, matching /EHsc. Emscripten compiles throws to traps otherwise.
    -fexceptions

    # The inherited declarations carry MSVC's __declspec, which clang parses only when asked.
    -fdeclspec

    # WebAssembly has one calling convention, so the x86 keywords naming the others carry no
    # meaning here. vqapalette.h uses the underscore-less `cdecl`, which no clang option
    # accepts, so all four are erased on the command line rather than in a header the
    # declarations would have to be made to include.
    -D__cdecl=
    -D__stdcall=
    -D__fastcall=
    -Dcdecl=

    -g
    -fno-omit-frame-pointer

    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O2>
)

else()

set(OPENTS_COMPILE_OPTIONS

    # ================= DEBUG =================
    $<$<CONFIG:Debug>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /Od /RTC1 /GR /MP /EHsc /Oy- /MTd /Zc:__cplusplus /arch:SSE2 /fp:precise
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /Od /RTC1 /MP /Oy- /arch:SSE2 /fp:precise
        >
    >

    # ================= RELEASE =================
    $<$<CONFIG:Release>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /O2 /GF /GR /MP /EHsc /MT /Zc:__cplusplus /arch:SSE2 /fp:precise
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /O2 /GF /MP /arch:SSE2 /fp:precise
        >
    >
)

endif()

target_compile_options(OpenTS PRIVATE ${OPENTS_COMPILE_OPTIONS})

#
# ---------------------------------------------------------
# Subprojects
# ---------------------------------------------------------
#
# The VQA player carries its own project, and hands OpenTS its include path along with the
# library, so the movie code is reached the same way from anywhere in the solution.
add_subdirectory(vqalib)

message(STATUS "${PROJECT_NAME}: Adding include paths...")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OPENTS_GENERATED_DIR}"
)

# The generated build stamp has to exist before anything compiles.
add_dependencies(OpenTS OpenTSBuildStamp)

# Only the renderer's own translation unit sees bgfx, so its headers and the shaders it
# embeds are put on that file rather than on the whole target.
set(BGFX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" PROPERTIES
    INCLUDE_DIRECTORIES
        "${BGFX_ROOT}/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bx/include;${BGFX_ROOT}/examples/common/imgui"
)

if(MSVC)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" PROPERTIES
        COMPILE_OPTIONS "/Zc:preprocessor"
    )
endif()

message(STATUS "${PROJECT_NAME}: Adding compilier definitions...")
target_compile_definitions(OpenTS PRIVATE
    WIN32
    _WINDOWS
    _MBCS

    # Compiles Blowfish into the binary instead of reaching it through the COM object in
    # blowfish.dll. It decides the layout of BlowfishEngine, so every translation unit has
    # to agree on it and it belongs on the compile line rather than in a header.
    NO_BLOWFISH_DLL
)

#
# ---------------------------------------------------------
# Windows libraries and linker options
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying linker options...")

# Required libraries:
#  Comctl32.lib
#  DbgHelp.lib
#  DSound.lib
#  Version.lib
#  Winmm.lib
#  wsock32.lib
#  + all Win32 libs
#

# The bgfx targets are linked by file rather than by name. Their interface settings
# include compiler switches and a directory of headers that shadow the system ones, and
# the Visual Studio generator would hand all of that to ML.EXE as well, which fails.
add_dependencies(OpenTS bgfx bx bimg)
target_link_libraries(OpenTS PRIVATE
    VQALib

    $<TARGET_FILE:bgfx>
    $<TARGET_FILE:bx>
    $<TARGET_FILE:bimg>
)

if (NOT EMSCRIPTEN)
    target_link_libraries(OpenTS PRIVATE
        comctl32
        dbghelp
        dsound
        iphlpapi
        psapi
        version
        winmm
        wsock32
        kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32
        ole32 oleaut32 uuid odbc32 odbccp32
    )
endif()

if (EMSCRIPTEN)
    target_link_options(OpenTS PRIVATE
        # bgfx's OpenGL ES 3 renderer reaches the page through WebGL 2 alone.
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2

        # The engine sizes its own allocations at startup rather than against a fixed heap.
        -sALLOW_MEMORY_GROWTH=1

        -fexceptions
    )
endif()

if (MSVC)
    target_link_options(OpenTS PRIVATE
        /SUBSYSTEM:WINDOWS
        /DEBUG
        /MAP
        # Raises the user address space from 2GB to 4GB under 64-bit Windows. Pointers may
        # then have the high bit set, so anything holding one in a signed integer must only
        # round-trip the bits and never compare or scale them.
        /LARGEADDRESSAWARE
        # Declares the process per-monitor DPI aware, so a scaled desktop does not blur
        # the frame by stretching the whole window.
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/sun.manifest"
        /MANIFEST:EMBED
    )
endif()

#
# ---------------------------------------------------------
# Visual Studio source groups
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting up Visual Studio filters...")

# Init Source Files (_*.cpp)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.cpp$")
        source_group("Source Files\\Init Source Files" FILES ${f})
    endif()
endforeach()

# Init Header Files (_*.h)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.h$")
        source_group("Header Files\\Init Header Files" FILES ${f})
    endif()
endforeach()

# Define Header Files (*.hh)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.hh$")
        source_group("Header Files\\Define Header Files" FILES ${f})
    endif()
endforeach()

# Resource Files
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.(ico|cur|bmp|gif|jpe?g|rc)$")
        source_group("Resource Files" FILES ${f})
    endif()
endforeach()

# List of all interface filenames (headers + C stubs)
set(INTERFACE_FILES
    iblockci.h       iblockci_i.c
    iblowfish.h      iblowfish_i.c
    iflyctrl.h       iflyctrl_i.c
    ilinkstm.h
    iloco.h          iloco_i.c
    ilocos.h         ilocos_i.c
    ipiggy.h         ipiggy_i.c
    isun.h           isun_i.c
    wolapi/wolapi_i.c
)

# Convert to full paths relative to source dir
set(FULL_INTERFACE_FILES "")
foreach(f IN LISTS INTERFACE_FILES)
    list(APPEND FULL_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
endforeach()

# Add them to the "Interface Files" group and exclude *_i.c from build
foreach(f IN LISTS FULL_INTERFACE_FILES)

    # Put into VS filter
    source_group("Interface Files" FILES "${f}")

    # Exclude *_i.c from build, but DO NOT mark headers as header-only
    if(f MATCHES "_i\\.c$")
        set_source_files_properties("${f}" PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()

endforeach()

# General source files
source_group("Source Files" REGULAR_EXPRESSION ".*\\.(c|cpp)$")
source_group("Header Files" REGULAR_EXPRESSION ".*\\.(h|hpp)$")

#
# ---------------------------------------------------------
# Populate the local run directory
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating post-build commands...")
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${TS_RUN_DIR}"
)

add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenTS>"
        "${TS_RUN_DIR}"
)

# The symbol file and the map come from the MSVC linker.
if(MSVC)
    # Copy the linker-generated .pdb alongside the exe
    add_custom_command(TARGET OpenTS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:OpenTS>"
            "${TS_RUN_DIR}"
    )

    # Copy the linker-generated .map (sits next to the exe, no GenEx for it)
    add_custom_command(TARGET OpenTS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:OpenTS>/$<TARGET_FILE_BASE_NAME:OpenTS>.map"
            "${TS_RUN_DIR}"
    )
endif()

#
# ---------------------------------------------------------
# Visual Studio startup project
# ---------------------------------------------------------
#
set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT OpenTS)

# ---------------------------------------------------------
# Add the .natvis file for Visual Studio debugging
# ---------------------------------------------------------
if(MSVC)
    set(NATVIS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sun.natvis")
    source_group("Natvis Files" FILES ${NATVIS_FILE})
    target_sources(OpenTS PRIVATE ${NATVIS_FILE})
    target_link_options(OpenTS PRIVATE "/NATVIS:${NATVIS_FILE}")
endif()
