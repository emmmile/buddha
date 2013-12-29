cmake_minimum_required (VERSION 2.6)



file(GLOB BUDDHASRC "*.h" "*.cpp")
add_library(core ${BUDDHASRC})



if(APPLE)
    target_link_libraries (core boost_program_options-mt boost_log-mt pthread png)
else()
    target_link_libraries (core boost_program_options boost_log pthread png)
endif()
