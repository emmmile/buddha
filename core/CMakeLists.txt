cmake_minimum_required (VERSION 2.6)



file(GLOB BUDDHASRC "*.h" "*.cpp")
add_library(core ${BUDDHASRC})

target_link_libraries (core boost_program_options boost_log pthread png)
