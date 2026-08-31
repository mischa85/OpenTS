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
add_subdirectory(win32file)
add_subdirectory(resources)
add_subdirectory(win32process)
add_subdirectory(win32user)
add_subdirectory(win32window)
add_subdirectory(com)
add_subdirectory(save)

# These exercise the WebAssembly target's own layers, so their harnesses build only there.
if(EMSCRIPTEN)
    add_subdirectory(audio)
    add_subdirectory(timer)
    add_subdirectory(isohttp)
endif()
