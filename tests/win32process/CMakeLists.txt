# The harness calls the Win32 process and module API and is checked against whichever
# implementation the target provides: the operating system's under MSVC, and the
# WebAssembly target's substitute for it under Emscripten. The substitute is compiled
# straight into the harness.
set(WIN32PROCESS_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/win32processtest.cpp"
)

add_executable(Win32ProcessTest ${WIN32PROCESS_TEST_SOURCES})

target_compile_features(Win32ProcessTest PRIVATE cxx_std_20)

target_include_directories(Win32ProcessTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(Win32ProcessTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(Win32ProcessTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )

    # CommandLineToArgvW is the shell's, wherever the rest of the process API comes from.
    target_link_libraries(Win32ProcessTest PRIVATE Shell32.lib)
endif()

if(NOT WIN32)
    target_link_libraries(Win32ProcessTest PRIVATE OpenTSPosixCompat)
endif()

set_target_properties(Win32ProcessTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


if(NOT MSVC)
    target_compile_definitions(Win32ProcessTest PRIVATE NOMINMAX)
    target_compile_options(Win32ProcessTest PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})
endif()

add_test(NAME win32process COMMAND Win32ProcessTest)
