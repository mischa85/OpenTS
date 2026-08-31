# The compression sources are compiled straight into the harness. They live outside code/
# so that the recursive glob building OpenTS cannot pick this target's entry point up.
add_executable(LzoComp
    "${CMAKE_CURRENT_SOURCE_DIR}/lzocomp.cpp"
    "${CMAKE_SOURCE_DIR}/code/lzo1x_c.cpp"
    "${CMAKE_SOURCE_DIR}/code/lzo1x_d.cpp"
    "${CMAKE_SOURCE_DIR}/code/lzopipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/lzostraw.cpp"
    "${CMAKE_SOURCE_DIR}/code/pipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/straw.cpp"
)

target_compile_features(LzoComp PRIVATE cxx_std_20)

target_include_directories(LzoComp PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(LzoComp PRIVATE NOMINMAX)
if(WIN32)
    target_compile_definitions(LzoComp PRIVATE WIN32 _WINDOWS _MBCS)
endif()

# The engine builds these sources with SSE2 and precise floating point. The harness matches
# that so a result here carries over to the engine.
target_compile_options(LzoComp PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS} ${OPENTS_HARNESS_FP_OPTIONS})

if(WIN32)
    target_link_libraries(LzoComp PRIVATE kernel32 user32 shell32)
endif()

set_target_properties(LzoComp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME lzocomp COMMAND LzoComp)
