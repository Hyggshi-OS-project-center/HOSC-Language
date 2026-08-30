add_executable(hosc
  hosc/src/main.c
  hosc/src/command_run.c
  hosc/src/command_build.c
  hosc/src/command_check.c
  hosc/src/command_fmt.c
  hosc/src/command_test.c
  hosc/src/command_version.c
  hosc/src/cli_options.c
  hosc/src/cli_output.c
)

target_include_directories(hosc
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hosc/include
    ${CMAKE_SOURCE_DIR}/compiler/include
    ${CMAKE_SOURCE_DIR}/runtime/include
)

target_link_libraries(hosc
  PRIVATE
    hosc_compiler
    hosc_runtime
)
