# The harness calls the Win32 file API and is checked against whichever implementation the
# target provides: the operating system's under MSVC, and the WebAssembly target's
# substitute for it under Emscripten. The substitute is compiled straight into the harness;
# its entry point lives outside code/ so that the recursive glob building OpenTS cannot pick
# it up.
set(WIN32FILE_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/win32filetest.cpp"
)

if(EMSCRIPTEN)
    # The substitute resolves a name the host has no file for on a mounted disc image, so
    # the reader and the block source it is handed come with it.
    list(APPEND WIN32FILE_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
        "${CMAKE_SOURCE_DIR}/code/isohttp.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32process.cpp"
    )
endif()

add_executable(Win32FileTest ${WIN32FILE_TEST_SOURCES})

target_compile_features(Win32FileTest PRIVATE cxx_std_20)

target_include_directories(Win32FileTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(Win32FileTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(Win32FileTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(Win32FileTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(EMSCRIPTEN)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with.
    target_compile_options(Win32FileTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
        -D__cdecl=
        -D__stdcall=
        -D__fastcall=
    )

    # The harness creates and reads back real files, so it needs a real filesystem rather
    # than the in-memory one Emscripten supplies by default.
    target_link_options(Win32FileTest PRIVATE -sNODERAWFS=1 -fexceptions)
endif()

# The harness writes the files it reads back, so it is given a directory of its own.
add_test(NAME win32file COMMAND Win32FileTest "${CMAKE_CURRENT_BINARY_DIR}")
