cmake_minimum_required(VERSION 3.16)
project(buddha++ LANGUAGES CXX)

# also look at
# http://stackoverflow.com/questions/14446495/cmake-project-structure-with-unit-tests

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -O3 -ffast-math -march=native -funroll-loops)

find_package(Boost REQUIRED COMPONENTS program_options log log_setup serialization iostreams filesystem)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(timer timer/timer.cpp)
target_include_directories(timer PUBLIC ${PROJECT_SOURCE_DIR}/timer)

add_subdirectory("${PROJECT_SOURCE_DIR}/core")

add_executable(buddha++ main.cpp)
target_link_libraries(buddha++ PRIVATE core timer Threads::Threads ${CMAKE_DL_LIBS})

add_executable(exclusion exclusion.cpp)
target_link_libraries(exclusion PRIVATE core timer Threads::Threads ${CMAKE_DL_LIBS})
