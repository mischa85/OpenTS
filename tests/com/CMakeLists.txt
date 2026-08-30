# The harness calls COM and is checked against whichever implementation the target
# provides: OLE's under MSVC, and the WebAssembly target's in-process class object table
# under Emscripten. The substitute is compiled straight into the harness; its entry point
# lives outside code/ so that the recursive glob building OpenTS cannot pick it up.
set(COM_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/comtest.cpp"
)

if(EMSCRIPTEN)
    # The substitute's file layer resolves names against a mounted disc image, so the
    # reader and the block source it is handed come with it.
    list(APPEND COM_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
        "${CMAKE_SOURCE_DIR}/code/isohttp.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"
        "${CMAKE_SOURCE_DIR}/code/win32process.cpp"
    )
endif()

add_executable(ComTest ${COM_TEST_SOURCES})

target_compile_features(ComTest PRIVATE cxx_std_20)

target_include_directories(ComTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(ComTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(ComTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )

    target_link_libraries(ComTest PRIVATE ole32)
endif()

set_target_properties(ComTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(EMSCRIPTEN)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with.
    target_compile_options(ComTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
        -D__cdecl=
        -D__stdcall=
        -D__fastcall=
    )

    target_link_options(ComTest PRIVATE -fexceptions)
endif()

add_test(NAME com COMMAND ComTest)
