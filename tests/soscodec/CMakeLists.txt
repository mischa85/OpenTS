# The codec is compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.
add_executable(SosCodecTest
    "${CMAKE_CURRENT_SOURCE_DIR}/soscodectest.cpp"
    "${CMAKE_SOURCE_DIR}/code/soscodec.cpp"
)

target_compile_features(SosCodecTest PRIVATE cxx_std_20)

target_include_directories(SosCodecTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

# soscomp.h declares the entry points with the Microsoft calling convention. The codec has
# no other platform dependency, so the harness builds anywhere once that spelling is
# neutralized.
if(NOT MSVC)
    target_compile_definitions(SosCodecTest PRIVATE __cdecl=)
endif()

target_compile_options(SosCodecTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:$<$<CONFIG:Debug>:/MTd>>
    $<$<CXX_COMPILER_ID:MSVC>:$<$<CONFIG:Release>:/MT>>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

set_target_properties(SosCodecTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME soscodec COMMAND SosCodecTest)
