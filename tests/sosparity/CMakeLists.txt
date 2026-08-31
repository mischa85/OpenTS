# The decoder is compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.
add_executable(SosParity
    "${CMAKE_CURRENT_SOURCE_DIR}/sosparity.cpp"
    "${CMAKE_SOURCE_DIR}/code/soscodec.cpp"
)

target_compile_features(SosParity PRIVATE cxx_std_20)

target_include_directories(SosParity PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(SosParity PRIVATE NOMINMAX)
if(WIN32)
    target_compile_definitions(SosParity PRIVATE WIN32 _WINDOWS _MBCS)
endif()

# The engine builds this source with SSE2 and precise floating point. The harness matches that
# so a result here carries over to the engine.
target_compile_options(SosParity PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS} ${OPENTS_HARNESS_FP_OPTIONS})

if(WIN32)
    target_link_libraries(SosParity PRIVATE kernel32 user32 shell32)
endif()

set_target_properties(SosParity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME sosparity COMMAND SosParity)
