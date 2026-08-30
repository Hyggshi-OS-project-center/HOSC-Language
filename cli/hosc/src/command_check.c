#include "hosc_cli.h"

int hosc_cli_command_check(const char* path) {
    HoscCompileOptions options;
    HoscCompileResult result;

    options.module_name = NULL;
    options.output_path = NULL;
    options.emit_debug_info = false;
    options.check_only = true;

    result = hosc_compile_file(path, &options);
    if (!result.success) {
        hosc_cli_print_diagnostics(result.diagnostics);
        hosc_compile_result_free(&result);
        return 1;
    }

    hosc_compile_result_free(&result);
    return 0;
}
