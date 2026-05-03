add_executable(market-data-service
    src/main.cpp
    src/Logger.cpp
    src/Config.cpp
    src/SymbolRegistry.cpp
    src/YahooFeed.cpp
    src/YahooHistory.cpp
    src/NewsService.cpp
    src/EventChannel.cpp
    src/MarketDataService.cpp
)

target_include_directories(market-data-service PRIVATE include)
target_link_libraries(market-data-service PRIVATE td_common td_yahoo)
if(WIN32)
    target_link_libraries(market-data-service PRIVATE Ws2_32)
endif()

if(WIN32)
    set_target_properties(market-data-service PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

# Encourage the Release build artifact to land at a stable path that the
# Electron supervisor knows how to find.
set_target_properties(market-data-service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY              "${CMAKE_BINARY_DIR}/daemon"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG        "${CMAKE_BINARY_DIR}/daemon/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE      "${CMAKE_BINARY_DIR}/daemon/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/daemon/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL   "${CMAKE_BINARY_DIR}/daemon/MinSizeRel"
)
