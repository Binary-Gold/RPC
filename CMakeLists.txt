cmake_minimum_required(VERSION 3.16)
project(RPC VERSION 1.0.0 LANGUAGES CXX)

# Compiler settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Bundled third-party libraries
add_subdirectory(third_party/fmt)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library" FORCE)
add_subdirectory(third_party/spdlog)
add_subdirectory(third_party/json)

# System-installed ZooKeeper C client
find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h PATHS /usr/include NO_DEFAULT_PATH)
find_library(ZOOKEEPER_LIBRARY libzookeeper_mt.a PATHS /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
add_library(zookeeper_mt STATIC IMPORTED)
target_include_directories(zookeeper_mt INTERFACE ${ZOOKEEPER_INCLUDE_DIR})
set_target_properties(zookeeper_mt PROPERTIES IMPORTED_LOCATION ${ZOOKEEPER_LIBRARY})

# System-installed zstd
find_library(ZSTD_LIBRARY libzstd.a PATHS /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
add_library(zstd STATIC IMPORTED)
set_target_properties(zstd PROPERTIES IMPORTED_LOCATION ${ZSTD_LIBRARY})

# Project library
file(GLOB_RECURSE RPC_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(rpc STATIC ${RPC_SOURCES})
target_include_directories(rpc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(rpc PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    zookeeper_mt
    zstd
    pthread
)

# Google Test
add_subdirectory(/home/kaanh/googletest ${CMAKE_BINARY_DIR}/googletest)

# Integration test (ZooKeeper-based)
add_executable(rpc_test test/main.cpp)
target_link_libraries(rpc_test PRIVATE rpc)

# Load balancer unit tests
add_executable(load_balancer_test test/load_balancer_test.cpp)
target_link_libraries(load_balancer_test PRIVATE rpc gtest gtest_main)

# ServiceRegistry integration tests (requires ZooKeeper on localhost:2181)
add_executable(services_registry_test test/services_registry_test.cpp)
target_link_libraries(services_registry_test PRIVATE rpc gtest gtest_main)

# ZkConnHandler integration tests (requires ZooKeeper on localhost:2181)
add_executable(zk_conn_handler_test test/zk_conn_handler_test.cpp)
target_link_libraries(zk_conn_handler_test PRIVATE rpc gtest gtest_main)

# ThreadPool unit tests
add_executable(thread_pool_test test/thread_pool_test.cpp)
target_link_libraries(thread_pool_test PRIVATE rpc gtest gtest_main)

# ZstdCompress unit tests
add_executable(zstd_compress_test test/zstd_compress_test.cpp)
target_link_libraries(zstd_compress_test PRIVATE rpc gtest gtest_main)
