file(GLOB BUDDHASRC "*.h" "*.cpp")
add_library(core ${BUDDHASRC})
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The exclusion map committed to the repository, the default for every binary and test.
target_compile_definitions(core PUBLIC
    BUDDHA_EXCLUSION_MAP="${PROJECT_SOURCE_DIR}/data/exclusion.map")
target_link_libraries(core
    PUBLIC
        Boost::program_options
        Boost::log
        Boost::log_setup
        Boost::serialization
        Boost::iostreams
        Boost::filesystem
        TIFF::TIFF
        ZLIB::ZLIB
        zstd::libzstd
        Threads::Threads
)
