# Checks the generated language tables, so the harness is built against the same generated
# source the engine links. The entry point lives outside code/ so that the recursive glob
# building OpenTS cannot pick it up.
add_executable(ResourceTest
    "${CMAKE_CURRENT_SOURCE_DIR}/restest.cpp"
    "${OPENTS_LANGUAGE_SOURCE}"
)

target_compile_features(ResourceTest PRIVATE cxx_std_20)

target_include_directories(ResourceTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${OPENTS_GENERATED_DIR}"
)

if(MSVC)
    target_compile_definitions(ResourceTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(ResourceTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

if(NOT WIN32)
    target_link_libraries(ResourceTest PRIVATE OpenTSPosixCompat)
endif()

set_target_properties(ResourceTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(NOT MSVC)
    target_compile_definitions(ResourceTest PRIVATE NOMINMAX)
    target_compile_options(ResourceTest PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})
endif()

add_test(NAME resources COMMAND ResourceTest)
