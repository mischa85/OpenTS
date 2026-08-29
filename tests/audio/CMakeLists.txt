# The backend is compiled straight into the harness. It lives outside code/ so that the
# recursive glob building OpenTS cannot pick this target's entry point up.
add_executable(AudioBackendTest
    "${CMAKE_CURRENT_SOURCE_DIR}/audiobackendtest.cpp"
    "${CMAKE_SOURCE_DIR}/code/audiobackend.cpp"
)

target_compile_features(AudioBackendTest PRIVATE cxx_std_20)

target_include_directories(AudioBackendTest PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
    "${CMAKE_SOURCE_DIR}/code/vqalib"
)

# The Win32 shim the backend's DirectSound facing half is written against spells its entry
# points with the Microsoft calling conventions and storage class.
target_compile_definitions(AudioBackendTest PRIVATE
    WIN32
    _MBCS
    _WINDOWS
    NO_BLOWFISH_DLL
    __cdecl=
    __stdcall=
    __fastcall=
    cdecl=
)

target_compile_options(AudioBackendTest PRIVATE -fdeclspec -fshort-wchar)

target_link_libraries(AudioBackendTest PRIVATE openal)

set_target_properties(AudioBackendTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME audiobackend COMMAND AudioBackendTest)
