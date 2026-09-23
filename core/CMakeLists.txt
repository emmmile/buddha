file(GLOB BUDDHASRC "*.h" "*.cpp")
add_library(core ${BUDDHASRC})
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/timer
)
target_link_libraries(core
    PUBLIC
        Boost::program_options
        Boost::log
        Boost::log_setup
        Boost::serialization
        Boost::iostreams
        Boost::filesystem
        PNG::PNG
        TIFF::TIFF
        ZLIB::ZLIB
        Threads::Threads
        ${CMAKE_DL_LIBS}
)
