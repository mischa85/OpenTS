# The logger harness drives the Win32 debug log directly, so it builds only for that target.
if(MSVC)
    add_subdirectory(logstress)
endif()

# These cover portable code and carry no platform dependency, so they build wherever the
# engine does. None of them reads game data.
add_subdirectory(iso9660)
add_subdirectory(lcw)
add_subdirectory(soscodec)
add_subdirectory(vqa)
add_subdirectory(voxel)
add_subdirectory(lighting)
