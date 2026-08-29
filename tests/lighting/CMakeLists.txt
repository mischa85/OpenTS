# The lighting arithmetic is compiled straight into the harness. The entry point lives
# outside code/ so that the recursive glob building OpenTS cannot pick it up.
add_executable(LightTest
    "${CMAKE_CURRENT_SOURCE_DIR}/lighttest.cpp"
    "${CMAKE_SOURCE_DIR}/code/lightops.cpp"
)

target_compile_features(LightTest PRIVATE cxx_std_20)

target_include_directories(LightTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

if(MSVC)
    target_compile_definitions(LightTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(LightTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(LightTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME lighting COMMAND LightTest)
