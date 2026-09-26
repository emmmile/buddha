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
target_link_libraries(buddha++ PRIVATE core Threads::Threads ${CMAKE_DL_LIBS})

add_executable(exclusion exclusion.cpp)
target_link_libraries(exclusion PRIVATE core Threads::Threads ${CMAKE_DL_LIBS})

include(CTest)
if(BUILD_TESTING)
    add_executable(render-regression test/render_regression.cpp)
    target_link_libraries(render-regression PRIVATE core)
    add_test(NAME render-regression COMMAND render-regression)
endif()

if(APPLE)
    enable_language(OBJCXX)
    add_executable(metal-orbit-benchmark metal/orbit_benchmark.mm)
    target_compile_features(metal-orbit-benchmark PRIVATE cxx_std_17)
    target_compile_options(metal-orbit-benchmark PRIVATE -fno-fast-math)
    target_link_libraries(metal-orbit-benchmark PRIVATE "-framework Metal" "-framework Foundation")

    # GPU renderer (naive sampling) with buddha++'s options, exclusion map, checkpoints and TIFF
    # output. The kernel lives in metal/render_kernel.h.
    add_executable(buddha-metal metal/main.mm)
    set_target_properties(buddha-metal PROPERTIES OBJCXX_STANDARD 23)
    target_compile_options(buddha-metal PRIVATE -fobjc-arc)
    target_link_libraries(buddha-metal PRIVATE core Threads::Threads "-framework Metal" "-framework Foundation")

    # Naive-sampling benchmark: CPU renderer path, CPU float mirror and both Metal kernels. It
    # keeps the project flags so the CPU double path is the renderer's own code.
    add_executable(metal-render-benchmark metal/render_benchmark.mm)
    set_target_properties(metal-render-benchmark PROPERTIES OBJCXX_STANDARD 23)
    target_compile_options(metal-render-benchmark PRIVATE -fobjc-arc)
    target_link_libraries(metal-render-benchmark PRIVATE core "-framework Metal" "-framework Foundation")
endif()
