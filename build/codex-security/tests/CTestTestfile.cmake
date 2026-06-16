# CMake generated Testfile for 
# Source directory: C:/Users/Laptop dell/Downloads/hosc-language/tests
# Build directory: C:/Users/Laptop dell/Downloads/hosc-language/build/codex-security/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[security_regression]=] "C:/Users/Laptop dell/Downloads/hosc-language/build/codex-security/tests/security_regression.exe")
set_tests_properties([=[security_regression]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Laptop dell/Downloads/hosc-language/tests/CMakeLists.txt;7;add_test;C:/Users/Laptop dell/Downloads/hosc-language/tests/CMakeLists.txt;0;")
add_test([=[hosc_version]=] "C:/Users/Laptop dell/Downloads/hosc-language/build/codex-security/cli/hosc.exe" "version")
set_tests_properties([=[hosc_version]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Laptop dell/Downloads/hosc-language/tests/CMakeLists.txt;12;add_test;C:/Users/Laptop dell/Downloads/hosc-language/tests/CMakeLists.txt;0;")
add_test([=[hosc_run_hello]=] "C:/Users/Laptop dell/Downloads/hosc-language/build/codex-security/cli/hosc.exe" "run" "C:/Users/Laptop dell/Downloads/hosc-language/examples/level_a/hello.hosc")
set_tests_properties([=[hosc_run_hello]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Laptop dell/Downloads/hosc-language/tests/CMakeLists.txt;17;add_test;C:/Users/Laptop dell/Downloads/hosc-language/tests/CMakeLists.txt;0;")
