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

find_package(Boost 1.88 REQUIRED COMPONENTS program_options log log_setup serialization iostreams filesystem)
find_package(TIFF REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory("${PROJECT_SOURCE_DIR}/core")

add_executable(buddha++ main.cpp)
target_link_libraries(buddha++ PRIVATE core Threads::Threads)

add_executable(exclusion exclusion.cpp)
target_link_libraries(exclusion PRIVATE core Threads::Threads)

# Strict IEEE arithmetic for consistency tests, so any mismatch is a code difference.
set(BUDDHA_STRICT_FP -fno-fast-math -ffp-contract=off)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND BUDDHA_STRICT_FP -Wno-overriding-option)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(render-regression test/render_regression.cpp)
    target_link_libraries(render-regression PRIVATE core)
    add_test(NAME render-regression COMMAND render-regression)

    # The CPU renderer against the kernel shared with buddha-metal.
    add_executable(kernel-consistency test/kernel_consistency.cpp)
    target_link_libraries(kernel-consistency PRIVATE core)
    target_compile_options(kernel-consistency PRIVATE ${BUDDHA_STRICT_FP})
    add_test(NAME kernel-consistency COMMAND kernel-consistency)
endif()
