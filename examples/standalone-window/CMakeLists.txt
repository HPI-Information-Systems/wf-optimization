cmake_minimum_required(VERSION 2.8.12...3.29)
project(example-window)

set(CMAKE_CXX_STANDARD 17)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

include_directories(../../src/include)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
	message("Building in Release mode.")
	link_directories(../../build/release/src)
else()
	message(WARNING "Building in Debug mode.")
	link_directories(../../build/debug/src)
endif()

message("Compiler: ${CMAKE_CXX_COMPILER}")

include_directories(../../extension/core_functions)

add_executable(microbench microbench.cpp)
target_link_libraries(microbench duckdb)

add_executable(playground playground.cpp)
target_link_libraries(playground duckdb)

add_executable(adaptive_strategy determine_adaptive_thresholds.cpp)
target_link_libraries(adaptive_strategy duckdb)

add_executable(adaptive_strategy_ext determine_adaptive_thresholds_ext.cpp)
target_link_libraries(adaptive_strategy_ext duckdb)
