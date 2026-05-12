cmake_minimum_required(VERSION 3.16)
project(RPC VERSION 1.0.0 LANGUAGES CXX)

# Compiler settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Bundled third-party libraries
add_subdirectory(third_party/spdlog)
add_subdirectory(third_party/json)

# Project library (interface target: header-only for now, ready for src/ expansion)
add_library(rpc INTERFACE)
target_include_directories(rpc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(rpc INTERFACE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Test executable
add_executable(rpc_test test/main.cpp)
target_link_libraries(rpc_test PRIVATE rpc)
