# The harness asks platform.h the two questions every target answers for itself, and is
# checked against whichever implementation the target provides. The arguments below are the
# ones Test_Arguments expects to be handed back.
add_executable(PlatformTest "${CMAKE_CURRENT_SOURCE_DIR}/platformtest.cpp")

target_compile_features(PlatformTest PRIVATE cxx_std_20)

target_include_directories(PlatformTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

target_link_libraries(PlatformTest PRIVATE OpenTSPosixCompat)

set_target_properties(PlatformTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_definitions(PlatformTest PRIVATE NOMINMAX)
target_compile_options(PlatformTest PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})

add_test(NAME platform COMMAND PlatformTest -CD "a path with spaces" "say \"hello\"")
