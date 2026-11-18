cmake_minimum_required(VERSION 3.16)
project(surround_guide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(test_surround_pan tests/test_surround_pan.cpp)
target_include_directories(test_surround_pan PRIVATE src)
add_test(NAME test_surround_pan COMMAND test_surround_pan)
