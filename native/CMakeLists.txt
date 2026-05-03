cmake_minimum_required(VERSION 3.25)
project(twelve_e_trading_native CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Common: shared headers + the platform shm wrapper.
# ---------------------------------------------------------------------------
add_library(td_common STATIC
    common/src/ShmRegion.cpp
)
target_include_directories(td_common PUBLIC common/include)
set_target_properties(td_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    # CreateFileMapping / MapViewOfFile live in kernel32 (linked by default),
    # but be explicit so cross-toolchain builds (clang-cl, MinGW) don't surprise us.
    target_link_libraries(td_common PUBLIC)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(td_common PUBLIC rt pthread)
elseif(APPLE)
    # shm_open is in libSystem; nothing extra needed.
endif()

# ---------------------------------------------------------------------------
# Subprojects. The reader/ addon is built separately by cmake-js (it needs
# Node's CMAKE_JS_INC and CMAKE_JS_LIB), but it can also be configured via
# this top-level project for IDE indexing if CMAKE_JS_INC is provided.
# ---------------------------------------------------------------------------
add_subdirectory(daemon)
add_subdirectory(tools/shm-smoke)

# Reader is only configured here when invoked through cmake-js (which sets
# the CMAKE_JS_* variables). For a normal `cmake ..` invocation we skip it.
if(DEFINED CMAKE_JS_INC)
    add_subdirectory(reader)
endif()
