# The logger is compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.
add_executable(LogStress
    "${CMAKE_CURRENT_SOURCE_DIR}/logstress.cpp"
    "${CMAKE_SOURCE_DIR}/code/dbgprint.cpp"
    "${CMAKE_SOURCE_DIR}/code/platform_posix.cpp"
)

target_compile_features(LogStress PRIVATE cxx_std_20)

# The generated build stamp comes with dbgprint.cpp, so this target needs it too.
target_include_directories(LogStress PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${OPENTS_GENERATED_DIR}"
)

add_dependencies(LogStress OpenTSBuildStamp)

target_compile_definitions(LogStress PRIVATE NOMINMAX)
if(WIN32)
    target_compile_definitions(LogStress PRIVATE WIN32 _WINDOWS _MBCS)
endif()

target_compile_options(LogStress PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})

if(WIN32)
    target_link_libraries(LogStress PRIVATE kernel32 user32 shell32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(LogStress PRIVATE Threads::Threads)
endif()

set_target_properties(LogStress PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME logstress COMMAND LogStress)
