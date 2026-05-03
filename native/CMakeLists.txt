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
# Third-party: libcurl via FetchContent. Schannel on Windows / SecureTransport
# on macOS keeps us off OpenSSL on those platforms; Linux falls back to the
# system OpenSSL. HTTP_ONLY drops protocols we never use (FTP, SMTP, ...).
# First configure pulls and builds curl, which takes a minute or two; the
# subsequent FetchContent cache makes incremental builds fast.
# ---------------------------------------------------------------------------
include(FetchContent)

set(BUILD_CURL_EXE             OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS          OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS          ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS         ON  CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS         OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS            OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL         OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY                  ON  CACHE BOOL "" FORCE)

if(WIN32)
    set(CURL_USE_SCHANNEL  ON  CACHE BOOL "" FORCE)
elseif(APPLE)
    set(CURL_USE_SECTRANSP ON  CACHE BOOL "" FORCE)
else()
    set(CURL_USE_OPENSSL   ON  CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_11_0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(curl)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Older curl versions expose the static target under a different name. Add a
# CURL::libcurl alias if find_package-style targets weren't created.
if(NOT TARGET CURL::libcurl)
    if(TARGET libcurl_static)
        add_library(CURL::libcurl ALIAS libcurl_static)
    elseif(TARGET libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
    endif()
endif()

# ---------------------------------------------------------------------------
# Subprojects. The reader/ addon is built separately by cmake-js (it needs
# Node's CMAKE_JS_INC and CMAKE_JS_LIB), but it can also be configured via
# this top-level project for IDE indexing if CMAKE_JS_INC is provided.
# ---------------------------------------------------------------------------
add_subdirectory(yahoo)
add_subdirectory(daemon)
add_subdirectory(tools/shm-smoke)
add_subdirectory(tools/yahoo-crumb-smoke)
add_subdirectory(tools/yahoo-quote-smoke)
add_subdirectory(tools/yahoo-chart-smoke)

# Reader is only configured here when invoked through cmake-js (which sets
# the CMAKE_JS_* variables). For a normal `cmake ..` invocation we skip it.
if(DEFINED CMAKE_JS_INC)
    add_subdirectory(reader)
endif()
