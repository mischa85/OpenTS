# The codec is compiled straight into the harness. The entry point lives outside code/ so
# that the recursive glob building OpenTS cannot pick it up.
add_executable(LCWTest
    "${CMAKE_CURRENT_SOURCE_DIR}/lcwtest.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
)

# The block container that carries LCW data through the pipe and straw chains. It brings the
# base classes with it and, like the codec, needs nothing else from the engine.
add_executable(LCWStreamTest
    "${CMAKE_CURRENT_SOURCE_DIR}/lcwstreamtest.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwpipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwstraw.cpp"
    "${CMAKE_SOURCE_DIR}/code/pipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/straw.cpp"
)

# The entry point the VQA player reaches the codec through. It brings the codec with it and
# needs nothing else from the engine.
add_executable(LCWUncompTest
    "${CMAKE_CURRENT_SOURCE_DIR}/lcwuncmptest.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwuncmp.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
)

# iff.h spells the entry point with the Microsoft calling convention, which has no meaning
# elsewhere.
if(NOT MSVC)
    target_compile_definitions(LCWUncompTest PRIVATE __cdecl=)
endif()

foreach(target LCWTest LCWStreamTest LCWUncompTest)
    target_compile_features(${target} PRIVATE cxx_std_20)

    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/code"
    )

    if(MSVC)
        target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS _MBCS)

        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
            $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
        )
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()

add_test(NAME lcw COMMAND LCWTest)
add_test(NAME lcwstream COMMAND LCWStreamTest)
add_test(NAME lcwuncomp COMMAND LCWUncompTest)
