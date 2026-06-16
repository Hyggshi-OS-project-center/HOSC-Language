#include "hosc_cli.h"

#include <stdio.h>
#include <stdlib.h>

#include "hosc_runtime_api.h"

int hosc_cli_command_run_ex(const char* path, const char* output_hbc, int keep_bytecode) {
    HoscCompileResult result;
    int exit_code;
    char* default_out = NULL;
    const char* write_path = NULL;

    result = hosc_compile_file(path, NULL);
    if (!result.success) {
        hosc_cli_print_diagnostics(result.diagnostics);
        hosc_compile_result_free(&result);
        return 1;
    }

    if (output_hbc != NULL && output_hbc[0] != '\0') {
        write_path = output_hbc;
    } else if (keep_bytecode) {
        default_out = hosc_cli_replace_extension(path, ".hbc");
        if (default_out == NULL) {
            hosc_compile_result_free(&result);
            return 1;
        }
        write_path = default_out;
    }

    if (write_path != NULL) {
        if (!hosc_write_bytecode_file(write_path, result.bytecode)) {
            fputs("failed to write bytecode file\n", stderr);
            free(default_out);
            hosc_compile_result_free(&result);
            return 1;
        }
    }

    exit_code = hosc_runtime_run_bytecode(result.bytecode, NULL);

    free(default_out);
    hosc_compile_result_free(&result);
    return exit_code;
}

int hosc_cli_command_run(const char* path) {
    return hosc_cli_command_run_ex(path, NULL, 0);
}
