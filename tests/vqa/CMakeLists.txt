# The VQA codec primitives are compiled straight into the harness. They live outside
# any target that needs game assets, so this test runs on a bare checkout.
add_executable(VqaCodec
    "${CMAKE_CURRENT_SOURCE_DIR}/vqacodec.cpp"
    "${CMAKE_SOURCE_DIR}/code/vqalib/cmp.cpp"
    "${CMAKE_SOURCE_DIR}/code/vqalib/vqa_sos.cpp"
    "${CMAKE_SOURCE_DIR}/code/vqalib/unvq.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
)

target_compile_features(VqaCodec PRIVATE cxx_std_20)

target_include_directories(VqaCodec PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${CMAKE_SOURCE_DIR}/code/vqalib"
)

if(MSVC)
    target_compile_definitions(VqaCodec PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(VqaCodec PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

set_target_properties(VqaCodec PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME vqacodec COMMAND VqaCodec)
