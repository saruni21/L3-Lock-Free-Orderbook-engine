cmake_minimum_required(VERSION 3.20)
project(OrderBook VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for performance and safety
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -march=native
        -Wall -Wextra -Wpedantic
        -Werror=return-type
        -fno-omit-frame-pointer
        -falign-functions=64
    )
    # Link-time optimization
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif(MSVC)
    add_compile_options(/O2 /W4 /permissive- /Zc:__cplusplus)
endif()

# Enable testing
enable_testing()

# Find packages
find_package(Threads REQUIRED)

# Main library
add_subdirectory(src)

# Tests
add_subdirectory(tests)

# Benchmarks
add_subdirectory(benchmarks)
