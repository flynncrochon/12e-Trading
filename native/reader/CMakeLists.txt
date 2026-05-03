# Built by cmake-js. CMAKE_JS_INC / CMAKE_JS_LIB are set by the cmake-js wrapper.

cmake_minimum_required(VERSION 3.25)
project(shm_reader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_JS_INC)
    message(FATAL_ERROR "shm-reader must be configured via cmake-js (CMAKE_JS_INC missing).")
endif()

# node-addon-api ships under node_modules. cmake-js doesn't auto-include it.
# Resolve the absolute include directory from the repo root, since
# `include_dir` is reported relative to process.cwd().
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
execute_process(
    COMMAND node -e "process.stdout.write(require('path').resolve(require('node-addon-api').include_dir))"
    WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT EXISTS "${NODE_ADDON_API_DIR}/napi.h")
    message(FATAL_ERROR
        "node-addon-api include dir resolved to '${NODE_ADDON_API_DIR}' but napi.h not found there. "
        "Did you run pnpm install?")
endif()
message(STATUS "node-addon-api include: ${NODE_ADDON_API_DIR}")

# Ensure td_common is available. When invoked via cmake-js the parent project
# isn't included, so we add the common library here on its own.
if(NOT TARGET td_common)
    add_library(td_common STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/ShmRegion.cpp
    )
    target_include_directories(td_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
    set_target_properties(td_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(UNIX AND NOT APPLE)
        target_link_libraries(td_common PUBLIC rt pthread)
    endif()
endif()

add_library(${PROJECT_NAME} SHARED
    src/ShmReader.cpp
    src/binding.cc
    ${CMAKE_JS_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_JS_INC}
    ${NODE_ADDON_API_DIR}
    include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_JS_LIB}
    td_common
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
)

# NAPI version: pin so the addon doesn't drift across Node versions.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NAPI_VERSION=8
    NAPI_DISABLE_CPP_EXCEPTIONS
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Honour cmake-js's request to import the delay-load hook on Windows.
if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
    execute_process(
        COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS}
    )
endif()
