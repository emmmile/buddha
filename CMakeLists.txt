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

if(APPLE)
    enable_language(OBJCXX)
    add_executable(metal-orbit-benchmark metal/orbit_benchmark.mm)
    target_compile_features(metal-orbit-benchmark PRIVATE cxx_std_17)
    target_compile_options(metal-orbit-benchmark PRIVATE -fno-fast-math)
    target_link_libraries(metal-orbit-benchmark PRIVATE "-framework Metal" "-framework Foundation")

    # metal/render.metal with core/buddha_kernel.h inlined, embedded as a string for run-time
    # compilation (no Xcode needed) and written as a .metal file for ahead-of-time checks.
    set(BUDDHA_METAL_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
    add_custom_command(
        OUTPUT ${BUDDHA_METAL_GENERATED}/buddha_metal_source.h ${BUDDHA_METAL_GENERATED}/buddha.metal
        COMMAND ${CMAKE_COMMAND}
                -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/core/buddha_kernel.h
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/metal/render.metal
                -DOUTPUT_METAL=${BUDDHA_METAL_GENERATED}/buddha.metal
                -DOUTPUT_HEADER=${BUDDHA_METAL_GENERATED}/buddha_metal_source.h
                -P ${CMAKE_CURRENT_SOURCE_DIR}/metal/embed_source.cmake
        DEPENDS core/buddha_kernel.h metal/render.metal metal/embed_source.cmake
        COMMENT "Embedding the Metal kernel")
    add_custom_target(metal-source DEPENDS ${BUDDHA_METAL_GENERATED}/buddha_metal_source.h)

    function(add_metal_executable name)
        add_executable(${name} ${ARGN})
        add_dependencies(${name} metal-source)
        set_target_properties(${name} PROPERTIES OBJCXX_STANDARD 23)
        target_compile_options(${name} PRIVATE -fobjc-arc)
        target_include_directories(${name} PRIVATE metal ${BUDDHA_METAL_GENERATED})
        target_link_libraries(${name} PRIVATE core Threads::Threads "-framework Metal"
                              "-framework Foundation")
    endfunction()

    # GPU renderer (naive sampling) with buddha++'s options, exclusion map, checkpoints and TIFF.
    add_metal_executable(buddha-metal metal/main.mm)

    # CPU renderer code, shared lane on CPU threads and Metal kernel on identical samples.
    add_metal_executable(metal-render-benchmark metal/render_benchmark.mm)

    if(BUILD_TESTING)
        # The GPU kernel against the shared lane on the CPU; skipped without a Metal device.
        add_metal_executable(metal-consistency test/metal_consistency.mm)
        target_compile_options(metal-consistency PRIVATE ${BUDDHA_STRICT_FP})
        add_test(NAME metal-consistency COMMAND metal-consistency)
        set_tests_properties(metal-consistency PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
