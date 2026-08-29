# The drawers are compiled straight into the harness. The entry point lives outside code/
# so that the recursive glob building OpenTS cannot pick it up.
add_executable(VoxelDrawTest
    "${CMAKE_CURRENT_SOURCE_DIR}/voxeldrawtest.cpp"
    "${CMAKE_SOURCE_DIR}/code/voxdraw.cpp"
)

target_compile_features(VoxelDrawTest PRIVATE cxx_std_20)

target_include_directories(VoxelDrawTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

# voxdraw.hh spells the drawers with the Microsoft calling convention. They have no other
# platform dependency, so the harness builds anywhere once that spelling is neutralized.
if(NOT MSVC)
    target_compile_definitions(VoxelDrawTest PRIVATE __cdecl=)
endif()

target_compile_options(VoxelDrawTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:$<$<CONFIG:Debug>:/MTd>>
    $<$<CXX_COMPILER_ID:MSVC>:$<$<CONFIG:Release>:/MT>>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

set_target_properties(VoxelDrawTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME voxel COMMAND VoxelDrawTest)
