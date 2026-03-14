/* hosc_framework.c - Framework entry point using compiler + HVM pipeline */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "hvm.h"
#include "hvm_compiler.h"
#include "hvm_platform.h"
#include "file_utils.h"

static void print_usage(const char *exe) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s run <script.hosc>\n", exe);
}

static int compile_and_run(const char *path) {
    char *source = NULL;
    ASTNode *ast = NULL;
    HVM_VM *vm = NULL;
    HVM_Compiler *compiler = NULL;
    int rc = 1;

    source = hosc_read_text_file(path, NULL);
    if (!source) {
        fprintf(stderr, "Cannot open script: %s\n", path);
        return 1;
    }

    ast = parser_parse(source);
    if (!ast) {
        fprintf(stderr, "Parse failed: %s\n", path);
        goto cleanup;
    }

    vm = hvm_create();
    if (!vm) {
        fprintf(stderr, "Failed to create VM\n");
        goto cleanup;
    }

    compiler = hvm_compiler_create(vm);
    if (!compiler) {
        fprintf(stderr, "Failed to create compiler\n");
        goto cleanup;
    }

    if (!hvm_compiler_compile_ast(compiler, ast)) {
        hvm_compiler_print_errors(compiler);
        goto cleanup;
    }

    if (hvm_compiler_has_errors(compiler)) {
        hvm_compiler_print_errors(compiler);
        goto cleanup;
    }

    hvm_platform_hide_console_if_needed(vm->instructions, vm->instruction_count);

    if (!hvm_run(vm)) {
        const char *msg = hvm_get_error(vm);
        fprintf(stderr, "VM execution failed%s%s\n", msg ? ": " : "", msg ? msg : "");
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (compiler) hvm_compiler_destroy(compiler);
    if (vm) hvm_destroy(vm);
    if (ast) ast_destroy(ast);
    free(source);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "run") != 0) {
        print_usage(argv[0]);
        return 1;
    }

    return compile_and_run(argv[2]);
}
