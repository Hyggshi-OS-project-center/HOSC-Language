cmake_minimum_required(VERSION 3.20)

project(hosc_language C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Keep executable locations stable across generators and operating systems.
# CMake appends the platform executable suffix itself (for example, `.exe` on
# Windows), so callers must never hard-code it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Multi-configuration generators (Visual Studio and Xcode) need per-config
# output locations as well.
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib/${config}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib/${config}")
endforeach()

option(HOSC_BUILD_TESTS "Build HOSC tests" ON)

add_subdirectory(compiler)
add_subdirectory(vm)
add_subdirectory(runtime)
add_subdirectory(cli)
add_subdirectory(bootstrap)
add_subdirectory(framework)

if(HOSC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
