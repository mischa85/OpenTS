# The codec is compiled straight into the harness. The entry point lives outside code/ so
# that the recursive glob building OpenTS cannot pick it up.
add_executable(LCWTest
    "${CMAKE_CURRENT_SOURCE_DIR}/lcwtest.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
)

target_compile_features(LCWTest PRIVATE cxx_std_20)

target_include_directories(LCWTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

if(MSVC)
    target_compile_definitions(LCWTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(LCWTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(LCWTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME lcw COMMAND LCWTest)
