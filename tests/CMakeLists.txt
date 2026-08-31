add_subdirectory(gamedirs)
add_subdirectory(logstress)
add_subdirectory(sosparity)
add_subdirectory(vqaunpack)
add_subdirectory(unvqdec)
add_subdirectory(unvqdelta)
add_subdirectory(colorops)
add_subdirectory(voxeldraw)
add_subdirectory(lcwcomp)

# The CPU detection reads CPUID and the time stamp counter, so it only exists for x86.
if(OPENTS_TARGET_IS_X86)
    add_subdirectory(cpudetect)
endif()
