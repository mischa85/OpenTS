# The timer is compiled straight into the harness, which supplies the clock and the yield
# it sits on. It lives outside code/ so that the recursive glob building OpenTS cannot pick
# this target's entry point up.
add_executable(TimerTest
    "${CMAKE_CURRENT_SOURCE_DIR}/timertest.cpp"
)
target_link_libraries(TimerTest PRIVATE OpenTSPosixCompat)

target_compile_features(TimerTest PRIVATE cxx_std_20)

target_include_directories(TimerTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

# The Win32 shim the timer is written against spells its entry points with the Microsoft
# calling conventions and storage class.
target_compile_definitions(TimerTest PRIVATE
    WIN32
    _MBCS
    _WINDOWS
    NO_BLOWFISH_DLL
    __cdecl=
    __stdcall=
    __fastcall=
    cdecl=
)

target_compile_options(TimerTest PRIVATE ${OPENTS_HARNESS_COMPILE_OPTIONS})
if(EMSCRIPTEN)
    target_compile_options(TimerTest PRIVATE -fshort-wchar)
endif()

set_target_properties(TimerTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME timer COMMAND TimerTest)
