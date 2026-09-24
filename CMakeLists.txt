cmake_minimum_required(VERSION 3.20)
project(buddha++ LANGUAGES CXX)

# also look at
# http://stackoverflow.com/questions/14446495/cmake-project-structure-with-unit-tests

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -O3 -ffast-math -march=native -funroll-loops)

# LTO improves the hot orbit-generation loop on Apple Silicon.  Keep it to
# Release builds and gracefully fall back when a toolchain cannot provide it.
include(CheckIPOSupported)
check_ipo_supported(RESULT BUDDHA_IPO_SUPPORTED OUTPUT BUDDHA_IPO_ERROR LANGUAGES CXX)
if(BUDDHA_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "Release LTO unavailable: ${BUDDHA_IPO_ERROR}")
endif()

find_package(Boost REQUIRED COMPONENTS program_options log log_setup serialization iostreams filesystem)
find_package(TIFF REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory("${PROJECT_SOURCE_DIR}/core")

add_executable(buddha++ main.cpp)
target_link_libraries(buddha++ PRIVATE core Threads::Threads ${CMAKE_DL_LIBS})

add_executable(exclusion exclusion.cpp)
target_link_libraries(exclusion PRIVATE core Threads::Threads ${CMAKE_DL_LIBS})
