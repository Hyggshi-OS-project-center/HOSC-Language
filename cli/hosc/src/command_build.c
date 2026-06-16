#include "hosc_cli.h"

#include <stdio.h>
#include <stdlib.h>

int hosc_cli_command_build(const char* path) {
    HoscCompileResult result;
    char* output_path;
    int exit_code = 0;

    result = hosc_compile_file(path, NULL);
    if (!result.success) {
        hosc_cli_print_diagnostics(result.diagnostics);
        hosc_compile_result_free(&result);
        return 1;
    }

    output_path = hosc_cli_replace_extension(path, ".hbc");
    if (!output_path) {
        hosc_compile_result_free(&result);
        return 1;
    }

    if (!hosc_write_bytecode_file(output_path, result.bytecode)) {
        fputs("failed to write bytecode output\n", stderr);
        exit_code = 1;
    } else {
        printf("wrote %s\n", output_path);
    }

    free(output_path);
    hosc_compile_result_free(&result);
    return exit_code;
}
