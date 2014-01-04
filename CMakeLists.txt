cmake_minimum_required (VERSION 2.6) 
project (buddha++) 
add_definitions(-std=c++11)
set(CMAKE_VERBOSE_MAKEFILE true)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O4 -mtune=native -ffast-math -funroll-loops" )
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS}")

include_directories ("${PROJECT_SOURCE_DIR}/random")
include_directories ("${PROJECT_SOURCE_DIR}/core")
add_subdirectory ("${PROJECT_SOURCE_DIR}/core")
link_directories ("${PROJECT_SOURCE_DIR}/core")

add_executable(buddha++ main.cpp)


target_link_libraries (buddha++ core pthread)
