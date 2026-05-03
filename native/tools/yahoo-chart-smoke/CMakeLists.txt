add_executable(yahoo-chart-smoke main.cpp)
target_link_libraries(yahoo-chart-smoke PRIVATE td_yahoo)

set_target_properties(yahoo-chart-smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/tools/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/tools/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/tools/MinSizeRel"
)
