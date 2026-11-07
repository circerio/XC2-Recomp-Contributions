cmake_minimum_required(VERSION 3.16)
project(hdr_guide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(test_hdr_math tests/test_hdr_math.cpp)
target_include_directories(test_hdr_math PRIVATE src)
add_test(NAME test_hdr_math COMMAND test_hdr_math)

if(WIN32)
  add_executable(print_display_levels tests/print_display_levels.cpp)
  target_include_directories(print_display_levels PRIVATE src)
  target_link_libraries(print_display_levels PRIVATE dxgi)
endif()

find_program(GLSLC glslc)
if(GLSLC)
  foreach(shader present_hdr highlights)
    add_test(NAME compile_${shader}
             COMMAND ${GLSLC} -o ${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv ${CMAKE_CURRENT_SOURCE_DIR}/src/${shader}.frag)
  endforeach()
endif()
