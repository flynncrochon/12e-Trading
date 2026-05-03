add_library(td_yahoo STATIC
    src/HttpClient.cpp
    src/YahooAuth.cpp
    src/YahooClient.cpp
)

target_include_directories(td_yahoo PUBLIC include)
target_link_libraries(td_yahoo PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
set_target_properties(td_yahoo PROPERTIES POSITION_INDEPENDENT_CODE ON)
