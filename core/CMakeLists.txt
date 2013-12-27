cmake_minimum_required (VERSION 2.6)



file(GLOB BUDDHASRC "*.h" "*.cpp")
add_executable(buddha++ ${BUDDHASRC})
target_link_libraries (buddha++ boost_program_options boost_log) 