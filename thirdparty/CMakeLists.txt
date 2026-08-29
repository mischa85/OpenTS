#
# ---------------------------------------------------------
# bgfx (rendering backend)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bgfx.cmake/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/bgfx.cmake is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# bgfx builds shader and texture tools by default; the engine ships neither.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_RENDERER_WEBGPU OFF CACHE BOOL "" FORCE)

if(MSVC)
    # The engine links the static CRT, so its dependencies must agree.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# bgfx chooses its renderer and its render thread per platform, so the Emscripten build asks
# for neither: it reaches the browser GPU through OpenGL ES 3, which Emscripten translates to
# WebGL 2, and it runs single-threaded, which is what a page without cross-origin isolation
# can load.

# Emscripten's toolchain reports an x86 processor, so bx adds -msse4.2 to every one of its
# translation units. clang lowers those intrinsics to WebAssembly SIMD, but only when the
# SIMD feature is enabled, and refuses the flags otherwise.
if(EMSCRIPTEN)
    add_compile_options(-msimd128)
endif()

# bgfx defaults to its OpenGL ES 2 path under Emscripten. The context is WebGL 2 either way,
# so ask for the renderer that matches it.
if(EMSCRIPTEN)
    set(BGFX_OPENGLES_VERSION 30 CACHE STRING "" FORCE)
endif()

add_subdirectory(bgfx.cmake)
