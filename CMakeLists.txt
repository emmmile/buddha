cmake_minimum_required (VERSION 2.6) 
project (buddha++) 
add_definitions(-std=c++11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O3 -march=native -ffast-math" )
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -s")

include_directories ("${PROJECT_SOURCE_DIR}/core")
add_subdirectory ("${PROJECT_SOURCE_DIR}/core")
link_directories ("${PROJECT_SOURCE_DIR}/core")

add_executable(buddha++ main.cpp)


target_link_libraries (buddha++ core pthread)
