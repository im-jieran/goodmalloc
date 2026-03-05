cmake_minimum_required(VERSION 3.28)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(goodmalloc LANGUAGES CXX)

# -----------------------------------------------------------------------------
# 全局编译配置
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json 供 clangd 语言服务器基于提供支持
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# 构建类型设置 (Debug / Release)
# -----------------------------------------------------------------------------

# 默认使用 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "Current build type: ${CMAKE_BUILD_TYPE}")

# 编译警告拦截
add_compile_options(-Wall -Wextra)

# 根据不同的构建类型追加特定的参数
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug：生成调试符号，禁用代码优化
    add_compile_options(-g -O0)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Release：启用 O3 优化，定义 NDEBUG 宏
    add_compile_options(-O3 -DNDEBUG)
endif()

# -----------------------------------------------------------------------------
# 目标：核心库（goodmalloc）
# -----------------------------------------------------------------------------

add_library(goodmalloc SHARED)

target_sources(goodmalloc
  PUBLIC
    FILE_SET CXX_MODULES FILES
        modules/utils.cppm
        modules/thread_cache.cppm
        modules/central_cache.cppm
        modules/page_cache.cppm
  PRIVATE
    include/shared.hpp
    src/goodmalloc.cpp
)

# 向引用者暴露 include 目录
target_include_directories(goodmalloc PUBLIC include)

# -----------------------------------------------------------------------------
# 目标：性能测试 (benchmark)
# -----------------------------------------------------------------------------

add_executable(benchmark benchmark/benchmark.cpp)

target_link_libraries(benchmark PRIVATE goodmalloc)

# -----------------------------------------------------------------------------
# 测试
# -----------------------------------------------------------------------------

# 开启 CTest 测试支持
enable_testing()

# 通过 FetchContent 模块引入 Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# 包含 GTest 宏，用于后续利用 gtest_discover_tests 自动扫描测试用例
include(GoogleTest)

# 测试用例
add_executable(test_radix_tree tests/test_radix_tree.cpp)
target_link_libraries(test_radix_tree PRIVATE goodmalloc gtest_main)
gtest_discover_tests(test_radix_tree)

# 测试用例
add_executable(test_page_cache tests/test_page_cache.cpp)
target_link_libraries(test_page_cache PRIVATE goodmalloc gtest_main)
gtest_discover_tests(test_page_cache)

# 测试用例
add_executable(test_allocator_api tests/test_allocator_api.cpp)
target_link_libraries(test_allocator_api PRIVATE goodmalloc gtest_main)
gtest_discover_tests(test_allocator_api)

# -----------------------------------------------------------------------------
message(STATUS "Build setup complete for goodmalloc with C++${CMAKE_CXX_STANDARD}")
