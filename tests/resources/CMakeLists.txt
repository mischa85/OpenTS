# The resource reader is compiled straight into the harness. The entry point lives outside
# code/ so that the recursive glob building OpenTS cannot pick it up.
add_executable(ResourceTest
    "${CMAKE_CURRENT_SOURCE_DIR}/restest.cpp"
    "${CMAKE_SOURCE_DIR}/code/peresource.cpp"
)

target_compile_features(ResourceTest PRIVATE cxx_std_20)

target_include_directories(ResourceTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

if(MSVC)
    target_compile_definitions(ResourceTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(ResourceTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(ResourceTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME resources COMMAND ResourceTest)
