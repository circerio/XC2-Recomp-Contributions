cmake_minimum_required(VERSION 3.16)
project(exposure_guide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

foreach(name test_exposure_timing sim_exposure)
  add_executable(${name} tests/${name}.cpp)
  target_include_directories(${name} PRIVATE src)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
