add_executable(shm-smoke main.cpp)
target_link_libraries(shm-smoke PRIVATE td_common)

set_target_properties(shm-smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY              "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG        "${CMAKE_BINARY_DIR}/tools/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE      "${CMAKE_BINARY_DIR}/tools/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL   "${CMAKE_BINARY_DIR}/tools/MinSizeRel"
)
