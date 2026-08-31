# The harness drives the compound file a saved game is written into. docfile.cpp builds on
# every target, so the same checks run in both places -- and on Windows the harness also
# reads what it wrote with OLE, and reads what OLE wrote with docfile.cpp, which is the only
# place that interchange can actually be established.
set(SAVE_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/savetest.cpp"
)
if(WIN32)
    list(APPEND SAVE_TEST_SOURCES "${CMAKE_SOURCE_DIR}/code/docfile.cpp")
endif()

add_executable(SaveTest ${SAVE_TEST_SOURCES})

target_compile_features(SaveTest PRIVATE cxx_std_20)

target_include_directories(SaveTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(SaveTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(SaveTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )

    target_link_libraries(SaveTest PRIVATE ole32)
endif()

if(NOT WIN32)
    target_link_libraries(SaveTest PRIVATE OpenTSPosixCompat)
endif()

set_target_properties(SaveTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(EMSCRIPTEN)
    # The substitute's structures are pinned to their Win32 x86 widths, so the harness needs
    # the same narrow wchar_t and MSVC keyword handling the engine is built with.
    target_compile_options(SaveTest PRIVATE
        -fshort-wchar
        -fno-builtin-wcslen
        -fno-builtin-wcsnlen
        -fexceptions
        -fdeclspec
        -D__cdecl=
        -D__stdcall=
        -D__fastcall=
    )

    # The harness writes the files it reads back, so it needs a real filesystem rather than
    # the in-memory one Emscripten supplies by default.
    target_link_options(SaveTest PRIVATE -sNODERAWFS=1 -fexceptions -sALLOW_MEMORY_GROWTH=1)
endif()

# The harness writes the files it reads back, so it is given a directory of its own.
if(NOT MSVC)
    target_compile_definitions(SaveTest PRIVATE NOMINMAX)
    target_compile_options(SaveTest PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})
endif()

add_test(NAME save COMMAND SaveTest "${CMAKE_CURRENT_BINARY_DIR}")
