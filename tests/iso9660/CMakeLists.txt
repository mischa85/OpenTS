# The ISO9660 reader is compiled straight into the harness. It lives outside code/ so that
# the recursive glob building OpenTS cannot pick this target's entry point up.
set(ISO9660_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/isotest.cpp"
    "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
)

# The file class the engine consumes the reader through is Win32 only, so the read-only
# checks that live with it are built only where it builds.
if(NOT EMSCRIPTEN)
    list(APPEND ISO9660_TEST_SOURCES "${CMAKE_SOURCE_DIR}/code/isofile.cpp")
endif()

add_executable(ISO9660Test ${ISO9660_TEST_SOURCES})

target_compile_features(ISO9660Test PRIVATE cxx_std_20)

target_include_directories(ISO9660Test PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(ISO9660Test PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(ISO9660Test PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

if(NOT WIN32)
    target_link_libraries(ISO9660Test PRIVATE OpenTSPosixCompat)
endif()

set_target_properties(ISO9660Test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The harness writes the images it reads back, so it is given a directory of its own.
# The harness writes the images it reads back, so it needs a real filesystem rather than
# the in-memory one Emscripten supplies by default.
if(EMSCRIPTEN)
    target_link_options(ISO9660Test PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
endif()

if(NOT MSVC)
    target_compile_definitions(ISO9660Test PRIVATE NOMINMAX)
    target_compile_options(ISO9660Test PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})
endif()

add_test(NAME iso9660 COMMAND ISO9660Test "${CMAKE_CURRENT_BINARY_DIR}")
