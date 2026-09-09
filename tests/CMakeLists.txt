# The harnesses build beside each other rather than into bin/, which holds what the game
# runs. Set before the subdirectories so every target below inherits it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-bin")

# Everything opents_add_test registers, so a build that cannot finish the engine can still
# ask for the harnesses by name.
add_custom_target(harnesses)

# Builds one test harness and registers it with CTest.
#
# A harness compiles the engine sources it exercises instead of linking the game, so ENGINE
# names them per test, relative to code/. SOURCES are relative to the test's own directory.
# FLOAT adds the float semantics the engine builds with, so a harness measuring numeric
# results matches it. UTF8 is for a harness whose own source holds non-ASCII text. STAMP is
# for a harness that reaches the generated version headers.
function(opents_add_test target)
    cmake_parse_arguments(TEST "FLOAT;UTF8;STAMP" "NAME"
        "SOURCES;ENGINE;INCLUDES;DEFINITIONS;LIBRARIES" ${ARGN})

    set(sources "")
    foreach(source IN LISTS TEST_SOURCES)
        list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endforeach()
    foreach(source IN LISTS TEST_ENGINE)
        list(APPEND sources "${CMAKE_SOURCE_DIR}/code/${source}")
    endforeach()

    add_executable(${target} ${sources})
    target_compile_features(${target} PRIVATE cxx_std_20)

    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/code"
        ${TEST_INCLUDES}
    )

    # The platform a harness is built for is this function's to name, as it is for the
    # engine target. DEFINITIONS is for what a harness's own sources ask for.
    if(WIN32)
        target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS)
    endif()

    if(TEST_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${TEST_DEFINITIONS})
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:/MTd>
            $<$<CONFIG:Release>:/MT>
            /EHsc
            /Zc:__cplusplus
        )
        if(TEST_UTF8)
            target_compile_options(${target} PRIVATE /utf-8)
        endif()
    else()
        # A native compiler reads UTF-8 sources and enables exceptions already, and has no
        # runtime library to choose. The Microsoft extensions the engine headers need come
        # from the top-level build. What it does add, which MSVC never assumes, is
        # type-based alias analysis, and the engine sources a harness compiles pun freely.
        target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    endif()

    # /arch:SSE2 is the default beyond 32-bit x86 and is rejected as an unknown option
    # there, so it is only passed where it applies.
    if(TEST_FLOAT)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fp:precise)
            if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                target_compile_options(${target} PRIVATE /arch:SSE2)
            endif()
        else()
            # /fp:precise forbids contracting the engine's accumulations into fused
            # operations. A native compiler contracts them within a statement by default.
            target_compile_options(${target} PRIVATE -ffp-contract=off)
            if(OPENTS_TARGET_X86_32)
                target_compile_options(${target} PRIVATE -msse2 -mfpmath=sse)
            endif()
        endif()
    endif()

    if(WIN32)
        target_link_libraries(${target} PRIVATE kernel32 user32 shell32)
    endif()
    target_link_libraries(${target} PRIVATE ${TEST_LIBRARIES})

    if(TEST_STAMP)
        add_dependencies(${target} OpenTSBuildStamp)
    endif()

    add_dependencies(harnesses ${target})
    add_test(NAME ${TEST_NAME} COMMAND ${target})
endfunction()

add_subdirectory(actionids)
add_subdirectory(cpudetect)
add_subdirectory(gamedirs)
add_subdirectory(ini)
add_subdirectory(logstress)
add_subdirectory(netpacket)
add_subdirectory(socketudp)
add_subdirectory(sosparity)
add_subdirectory(spawner)
add_subdirectory(spawnhouse)
add_subdirectory(syncrec)
add_subdirectory(unvqdelta)
add_subdirectory(colorops)
add_subdirectory(voxeldraw)
add_subdirectory(unvqdec)
add_subdirectory(lcwcomp)
add_subdirectory(lcwblock)
add_subdirectory(lzoblock)
add_subdirectory(autosave)
add_subdirectory(audioring)
add_subdirectory(audiohandle)
add_subdirectory(audiolevel)
add_subdirectory(auddecode)
add_subdirectory(audiocache)
add_subdirectory(audiomixer)
add_subdirectory(audiostream)
add_subdirectory(audioevent)
add_subdirectory(soundini)
add_subdirectory(voxqueue)
add_subdirectory(desync)
add_subdirectory(scenfile)
add_subdirectory(deploymentconfig)
add_subdirectory(tutorial)
add_subdirectory(utf8)
add_subdirectory(shapefacing)
add_subdirectory(cstream)
add_subdirectory(zbufring)
