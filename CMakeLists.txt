cmake_minimum_required(VERSION 2.6)
project(buddha++)
add_definitions(-std=c++11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O4 -mtune=native -ffast-math -funroll-loops" )
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS}")

include_directories("${PROJECT_SOURCE_DIR}/random")
include_directories("${PROJECT_SOURCE_DIR}/core")
add_subdirectory("${PROJECT_SOURCE_DIR}/core")
link_directories("${PROJECT_SOURCE_DIR}/core")

add_executable(buddha++ main.cpp)
add_executable(benchmark benchmark.cpp)
add_executable(test test/test.cpp)

target_link_libraries(buddha++ core pthread)
target_link_libraries(benchmark core pthread)
target_link_libraries(test boost_system boost_unit_test_framework)
