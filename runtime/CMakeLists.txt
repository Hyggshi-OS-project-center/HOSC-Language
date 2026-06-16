add_library(hosc_runtime STATIC
  src/entry/bundle_loader.c
  src/platform/platform_win32.c
  src/embed/embed_api.c
  src/bundle/exe_stub.c
)

target_include_directories(hosc_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/compiler/include
    ${CMAKE_SOURCE_DIR}/vm/include
)

target_link_libraries(hosc_runtime PUBLIC hvm)

add_executable(hvm_host
  src/entry/main_host.c
)

target_link_libraries(hvm_host PRIVATE hosc_runtime)
