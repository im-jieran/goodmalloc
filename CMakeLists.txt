cmake_minimum_required(VERSION 3.28)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(goodmalloc 
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置构建类型为 Debug（包含调试符号，禁用优化）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 添加调试符号和编译选项
add_compile_options(
    -g              # 生成调试符号
    -O0             # 禁用优化，便于调试
    -Wall           # 启用所有警告
    -Wextra         # 启用额外警告
)

# 生成 compile_commands.json 供 clangd 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# 核心库: goodmalloc
# 包含核心 Modules 实现和兼容层
# -----------------------------------------------------------------------------
add_library(goodmalloc)

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

# 暴露 include 目录作为公共接口
target_include_directories(goodmalloc PUBLIC include)

# -----------------------------------------------------------------------------
# 可执行文件: benchmark
# -----------------------------------------------------------------------------
add_executable(benchmark
    benchmark/benchmark.cpp
)
 
target_link_libraries(benchmark
    PRIVATE
        goodmalloc
)

message(STATUS "Build setup complete for goodmalloc with C++${CMAKE_CXX_STANDARD}")
