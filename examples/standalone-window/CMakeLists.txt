cmake_minimum_required(VERSION 2.8.12...3.29)
project(example-window)

set(CMAKE_CXX_STANDARD 17)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

include_directories(../../src/include)
link_directories(../../build/release/src)

include_directories(../../extension/core_functions)

add_executable(example main.cpp)
target_link_libraries(example duckdb)

add_executable(microbench microbench.cpp)
target_link_libraries(microbench duckdb)
