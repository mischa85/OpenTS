# The harness calls the Win32 window and message API and is checked against whichever
# implementation the target provides: USER32 under MSVC, and the WebAssembly target's
# window manager under Emscripten. The window manager is compiled straight into the
# harness, along with the quiet answers it reaches for outside itself.
set(WIN32USER_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/win32usertest.cpp"
)

if(EMSCRIPTEN)
    list(APPEND WIN32USER_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/win32usersupport.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32user.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32ctrl.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32process.cpp"

        # The substitute resolves a name the host has no file for on a mounted disc image,
        # so the reader and the block source it is handed come with it.
        "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
        "${CMAKE_SOURCE_DIR}/code/isohttp.cpp"
    )
endif()

add_executable(Win32UserTest ${WIN32USER_TEST_SOURCES})

target_compile_features(Win32UserTest PRIVATE cxx_std_20)

target_include_directories(Win32UserTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(Win32UserTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(Win32UserTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(Win32UserTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(EMSCRIPTEN)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with.
    target_compile_options(Win32UserTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
        -D__cdecl=
        -D__stdcall=
        -D__fastcall=
    )

    target_link_options(Win32UserTest PRIVATE -fexceptions)
endif()

add_test(NAME win32user COMMAND Win32UserTest)
