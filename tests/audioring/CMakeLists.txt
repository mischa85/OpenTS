# The rings are header-only; the harness needs nothing from code/ beyond the include path.

opents_add_test(AudioRing
    NAME audioring
    SOURCES audioring.cpp
    FLOAT
)
