# The image overlay is the WebAssembly target's own answer to having no host filesystem, so
# the harness builds only where that target does. Under MSVC the same calls would reach the
# operating system's file API, which knows nothing about a mounted image.
if(NOT EMSCRIPTEN)
    return()
endif()

# The substitute and the reader are compiled straight into the harness; its entry point
# lives outside code/ so that the recursive glob building OpenTS cannot pick it up.
add_executable(ISOHttpTest
    "${CMAKE_CURRENT_SOURCE_DIR}/isohttptest.cpp"
    "${CMAKE_SOURCE_DIR}/code/iso9660.cpp"
    "${CMAKE_SOURCE_DIR}/code/isohttp.cpp"
    "${CMAKE_SOURCE_DIR}/code/win32compat.cpp"
)

target_compile_features(ISOHttpTest PRIVATE cxx_std_20)

target_include_directories(ISOHttpTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

# The substitute's structures are pinned to their Win32 x86 widths, so the harness needs the
# same narrow wchar_t and MSVC keyword handling the engine is built with.
target_compile_options(ISOHttpTest PRIVATE
    -fshort-wchar
    -fexceptions
    -fdeclspec
    -D__cdecl=
    -D__stdcall=
    -D__fastcall=
)

set_target_properties(ISOHttpTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The harness writes the image it then reads back, so it needs a real filesystem rather than
# the in-memory one Emscripten supplies by default.
target_link_options(ISOHttpTest PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -fexceptions)

# The harness writes into the directory it searches, so it is given one of its own.
add_test(NAME isohttp COMMAND ISOHttpTest "${CMAKE_CURRENT_BINARY_DIR}")
