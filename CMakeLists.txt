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

add_executable(buddha-benchmark test/benchmark.cpp)
target_link_libraries(buddha-benchmark PRIVATE core)
# Keep the frozen complex-arithmetic reference from acquiring fused operations
# when instantiated outside the renderer. The optimized loop also avoids fusion.
target_compile_options(buddha-benchmark PRIVATE -ffp-contract=off)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(buddha-benchmark PRIVATE -Wno-overriding-option)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(render-regression test/render_regression.cpp)
    target_link_libraries(render-regression PRIVATE core)
    add_test(NAME render-regression COMMAND render-regression)
    add_executable(orbit-regression test/orbit_regression.cpp)
    target_link_libraries(orbit-regression PRIVATE core)
    target_compile_options(orbit-regression PRIVATE -ffp-contract=off)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(orbit-regression PRIVATE -Wno-overriding-option)
    endif()
    add_test(NAME orbit-regression COMMAND orbit-regression)
    add_test(NAME benchmark-reproducibility
        COMMAND ${CMAKE_COMMAND} -DBENCHMARK=$<TARGET_FILE:buddha-benchmark>
                -DRENDERER=$<TARGET_FILE:buddha++>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/test/benchmark_reproducibility.cmake)
endif()
