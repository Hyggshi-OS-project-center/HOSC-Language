#include "hosc_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hosc_cli_command_build_ex(const char* path, const char* requested_output) {
    HoscCompileResult result;
    char* output_path;
    int exit_code = 0;

    result = hosc_compile_file(path, NULL);
    if (!result.success) {
        hosc_cli_print_diagnostics(result.diagnostics);
        hosc_compile_result_free(&result);
        return 1;
    }

    output_path = requested_output && requested_output[0]
        ? (char*)malloc(strlen(requested_output) + 1)
        : hosc_cli_replace_extension(path, ".hbc");
    if (!output_path) {
        hosc_compile_result_free(&result);
        return 1;
    }
    if (requested_output && requested_output[0]) {
        strcpy(output_path, requested_output);
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

int hosc_cli_command_build(const char* path) {
    return hosc_cli_command_build_ex(path, NULL);
}
