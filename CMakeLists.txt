cmake_minimum_required(VERSION 2.8)
#set(CMAKE_CXX_COMPILER g++-4.9)
project(buddha++)

# also look at
# http://stackoverflow.com/questions/14446495/cmake-project-structure-with-unit-tests

add_definitions(-std=c++0x)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O3 -mtune=native -ffast-math -funroll-loops" )
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS}")

include_directories("${PROJECT_SOURCE_DIR}/random")
include_directories("${PROJECT_SOURCE_DIR}/core")
include_directories("${PROJECT_SOURCE_DIR}/timer")
add_subdirectory("${PROJECT_SOURCE_DIR}/core")
add_subdirectory("${PROJECT_SOURCE_DIR}/timer")
link_directories("${PROJECT_SOURCE_DIR}/core")
link_directories("${PROJECT_SOURCE_DIR}/timer")

add_executable(buddha++ main.cpp)
#add_executable(benchmark test/benchmark.cpp)
#add_executable(test_generators test/test.cpp)

target_link_libraries(buddha++ core pthread timer dl)
#target_link_libraries(benchmark core pthread timer dl)
#target_link_libraries(test_generators core boost_system boost_unit_test_framework timer dl)

#enable_testing()
#add_test(test_generators test_generators)
