# The harness calls COM and is checked against whichever implementation the target
# provides: OLE's under MSVC, and the WebAssembly target's in-process class object table
# under Emscripten. The substitute is compiled straight into the harness; its entry point
# lives outside code/ so that the recursive glob building OpenTS cannot pick it up.
set(COM_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/comtest.cpp"
)

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

if(NOT WIN32)
    target_link_libraries(ComTest PRIVATE OpenTSPosixCompat)
endif()

set_target_properties(ComTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


if(NOT MSVC)
    target_compile_definitions(ComTest PRIVATE NOMINMAX)
    target_compile_options(ComTest PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})
endif()

add_test(NAME com COMMAND ComTest)
