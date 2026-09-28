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

    # Tone mapping, display adjustments and preview/TIFF orientation, with buddha-browser's
    # floating-point flags.
    add_executable(image-pipeline-test test/image_pipeline.cpp)
    target_include_directories(image-pipeline-test PRIVATE server)
    target_link_libraries(image-pipeline-test PRIVATE core)
    target_compile_options(image-pipeline-test PRIVATE -fno-fast-math)
    add_test(NAME image-pipeline-test COMMAND image-pipeline-test)

    add_executable(browser-session-test test/browser_session.cpp)
    target_include_directories(browser-session-test PRIVATE server)
    target_link_libraries(browser-session-test PRIVATE core Threads::Threads)
    target_compile_options(browser-session-test PRIVATE -fno-fast-math)
    add_test(NAME browser-session-test COMMAND browser-session-test)
endif()

if(APPLE)
    enable_language(OBJCXX)

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
                              "-framework Foundation" "-framework IOKit")
    endfunction()

    # GPU renderer (naive sampling) with buddha++'s options, exclusion map, checkpoints and TIFF.
    add_metal_executable(buddha-metal metal/main.mm)

    # Embed the browser files in the executable, just as the Metal kernel is embedded.
    set(BUDDHA_WEB_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/buddha_web_source.h)
    add_custom_command(OUTPUT ${BUDDHA_WEB_GENERATED}
        COMMAND ${CMAKE_COMMAND}
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/web
                -DOUTPUT=${BUDDHA_WEB_GENERATED}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/web/embed_source.cmake
        DEPENDS web/index.html web/style.css web/api.js web/geometry.js web/controls.js
                web/embed_source.cmake
        COMMENT "Embedding the browser page")
    add_custom_target(web-source DEPENDS ${BUDDHA_WEB_GENERATED})
    add_metal_executable(buddha-browser server/browser_server.cpp metal/browser_sampler.mm
                         metal/browser_open.mm)
    add_dependencies(buddha-browser web-source)
    target_include_directories(buddha-browser PRIVATE server)
    target_compile_options(buddha-browser PRIVATE -fno-fast-math)
    target_link_libraries(buddha-browser PRIVATE "-framework AppKit")

    if(BUILD_TESTING)
        # The GPU kernel against the shared lane on the CPU; skipped without a Metal device.
        add_metal_executable(metal-consistency test/metal_consistency.mm)
        target_compile_options(metal-consistency PRIVATE ${BUDDHA_STRICT_FP})
        add_test(NAME metal-consistency COMMAND metal-consistency)
        set_tests_properties(metal-consistency PROPERTIES SKIP_RETURN_CODE 77)

        # buddha-browser's worker lifecycle over HTTP (about 20 s); skipped without a Metal device.
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME browser-lifecycle
                     COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/browser_lifecycle.py
                             $<TARGET_FILE:buddha-browser>)
            set_tests_properties(browser-lifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
        endif()
    endif()
endif()
