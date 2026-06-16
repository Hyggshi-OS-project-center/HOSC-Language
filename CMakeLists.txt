cmake_minimum_required(VERSION 3.20)

project(hosc_language C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(HOSC_BUILD_TESTS "Build HOSC tests" ON)

add_subdirectory(compiler)
add_subdirectory(vm)
add_subdirectory(runtime)
add_subdirectory(cli)

if(HOSC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
