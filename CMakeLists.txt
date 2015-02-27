cmake_minimum_required(VERSION 2.8)
#set(CMAKE_CXX_COMPILER g++-4.9)
project(buddha++)

# also look at
# http://stackoverflow.com/questions/14446495/cmake-project-structure-with-unit-tests

add_definitions(-std=c++11)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O3 -mtune=native -ffast-math -funroll-loops" )
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS}")

#include( ExternalProject )
#set( Boost_Version 1.55.0 )
#string( REPLACE "." "_" Boost_Version_Underscore ${Boost_Version} )
#message( STATUS "Boost_Version: " ${Boost_Version} )

## Below is a CMake command to download, build and install Boost on the user's computer
#ExternalProject_Add(
#	 Boost
#    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/external/boost
#    URL http://downloads.sourceforge.net/project/boost/boost/${Boost_Version}/boost_${Boost_Version_Underscore}.zip
#    UPDATE_COMMAND ./bootstrap.sh
#    CONFIGURE_COMMAND ""
#    BUILD_COMMAND ./b2 --with-program_options --with-log --with-serialization --with-system --with-test --with-iostreams threading=multi toolset=gcc-4.9 link=static
#    BUILD_IN_SOURCE 1
#    INSTALL_COMMAND ./b2 install --prefix="${CMAKE_CURRENT_BUILD_DIR}/boost"
#)

#include_directories("${CMAKE_CURRENT_BUILD_DIR}/boost/include")
#link_directories("${CMAKE_CURRENT_BUILD_DIR}/boost/lib")

include_directories("${PROJECT_SOURCE_DIR}/random")
include_directories("${PROJECT_SOURCE_DIR}/core")
include_directories("${PROJECT_SOURCE_DIR}/timer")
add_subdirectory("${PROJECT_SOURCE_DIR}/core")
#add_subdirectory("${PROJECT_SOURCE_DIR}/timer")
link_directories("${PROJECT_SOURCE_DIR}/core")
link_directories("${PROJECT_SOURCE_DIR}/timer")


add_executable(buddha++ main.cpp)
add_executable(benchmark test/benchmark.cpp)
add_executable(test_generators test/test.cpp)

target_link_libraries(buddha++ core pthread timer)
target_link_libraries(benchmark core pthread timer)
target_link_libraries(test_generators core boost_system boost_unit_test_framework timer)

enable_testing()
add_test(test_generators test_generators)
