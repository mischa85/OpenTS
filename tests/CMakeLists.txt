add_subdirectory(gamedirs)
add_subdirectory(logstress)

# The CPU detection reads CPUID and the time stamp counter, so it only exists for x86.
if(OPENTS_TARGET_IS_X86)
    add_subdirectory(cpudetect)
endif()
