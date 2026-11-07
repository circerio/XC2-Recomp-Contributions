cmake_minimum_required(VERSION 3.16)
project(vrr_guide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

foreach(name test_vrr_policy test_flip_pacer sim_flip_pacing)
  add_executable(${name} tests/${name}.cpp)
  target_include_directories(${name} PRIVATE src)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
