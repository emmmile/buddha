cmake_minimum_required (VERSION 2.6)



file(GLOB BUDDHASRC "*.h" "*.cpp")
add_library(core ${BUDDHASRC})



if(APPLE)
    target_link_libraries (core boost_program_options-mt boost_log-mt boost_serialization-mt z pthread png boost_iostreams-mt)
else()
    target_link_libraries (core boost_program_options boost_log boost_serialization pthread png z boost_iostreams)
endif()
