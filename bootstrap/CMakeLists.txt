cmake_minimum_required(VERSION 3.20)

# ─────────────────────────────────────────────────────────────────
# HOSC Bootstrap Library: hosc_bootstrap
# ─────────────────────────────────────────────────────────────────

add_library(hosc_bootstrap STATIC
    src/bs_value.c
    src/bs_lexer.c
    src/bs_ast.c
    src/bs_command_registry.c
    src/bs_ast_rewriter.c
    src/bs_parser.c
    src/bs_runtime.c
    src/bs_interpreter.c
    src/bs_bytecode.c
    src/bs_codegen.c
    src/bs_vm.c
    src/bs_repl.c
    src/bootstrap.c
    src/commands/make_window.c
    src/commands/play_sound.c
    src/commands/send_packet.c
    src/commands/draw_rect.c
)

target_include_directories(hosc_bootstrap
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(hosc_bootstrap PUBLIC _POSIX_C_SOURCE=200809L)

target_compile_options(hosc_bootstrap PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
)

# ─────────────────────────────────────────────────────────────────
# CLI Executable: hosc-bootstrap
# ─────────────────────────────────────────────────────────────────

add_executable(hosc-bootstrap
    src/main.c
)

target_link_libraries(hosc-bootstrap
    PRIVATE hosc_bootstrap
)

target_include_directories(hosc-bootstrap
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ─────────────────────────────────────────────────────────────────
# Tests: bootstrap_tests
# ─────────────────────────────────────────────────────────────────

if(HOSC_BUILD_TESTS)
    add_executable(bootstrap_tests
        tests/test_bootstrap.c
    )

    target_link_libraries(bootstrap_tests
        PRIVATE hosc_bootstrap
    )

    target_include_directories(bootstrap_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    add_test(NAME bootstrap_lexer_parser_interp
             COMMAND bootstrap_tests)
endif()
