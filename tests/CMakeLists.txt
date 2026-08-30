add_executable(security_regression
  security_regression.c
)

target_link_libraries(security_regression PRIVATE hvm hosc_compiler)

add_test(
  NAME security_regression
  COMMAND security_regression
)

add_test(
  NAME hosc_version
  COMMAND hosc version
)

add_test(
  NAME hosc_version_long_option
  COMMAND hosc --version
)

add_test(
  NAME hosc_run_hello
  COMMAND hosc run ${CMAKE_SOURCE_DIR}/framework/examples/Hello.hosc
)
