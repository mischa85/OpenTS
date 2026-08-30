# The harness calls the Win32 process and module API and is checked against whichever
# implementation the target provides: the operating system's under MSVC, and the
# WebAssembly target's substitute for it under Emscripten. The substitute is compiled
# straight into the harness.
set(WIN32PROCESS_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/win32processtest.cpp"
)

if(EMSCRIPTEN)
    list(APPEND WIN32PROCESS_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/code/win32process.cpp"

        # The command line is released with LocalFree and the last error is read back, both
        # of which live with the rest of the substitute.
        "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"

        # The substitute resolves a name the host has no file for on a mounted disc image,
        # so the reader and the block source it is handed come with it.
        "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
        "${CMAKE_SOURCE_DIR}/code/isohttp.cpp"
    )
endif()

add_executable(Win32ProcessTest ${WIN32PROCESS_TEST_SOURCES})

target_compile_features(Win32ProcessTest PRIVATE cxx_std_20)

target_include_directories(Win32ProcessTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(Win32ProcessTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(Win32ProcessTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )

    # CommandLineToArgvW is the shell's, wherever the rest of the process API comes from.
    target_link_libraries(Win32ProcessTest PRIVATE Shell32.lib)
endif()

set_target_properties(Win32ProcessTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(EMSCRIPTEN)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with.
    target_compile_options(Win32ProcessTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
        -D__cdecl=
        -D__stdcall=
        -D__fastcall=
    )

    target_link_options(Win32ProcessTest PRIVATE -fexceptions)
endif()

add_test(NAME win32process COMMAND Win32ProcessTest)
