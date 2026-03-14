/* core.c - HOSC source file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "hvm.h"
#include "hvm_compiler.h"
#include "hvm_platform.h"
#include "file_utils.h"

static void print_usage(const char *exe) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s run <source.hosc>     - Generate C code\n", exe);
    fprintf(stderr, "  %s debug <source.hosc>   - Generate C code with debug info\n", exe);
    fprintf(stderr, "  %s exec <source.hosc>    - Execute directly (runtime)\n", exe);
    fprintf(stderr, "  %s exec-debug <source.hosc> - Execute with debug info\n", exe);
}

int main(int argc, char **argv) {
    ASTNode *ast;

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];
    const char *path = argv[2];

    char *source = hosc_read_text_file(path, NULL);
    if (!source) {
        fprintf(stderr, "Could not open file: %s\n", path);
        return 1;
    }

    int debug = (strcmp(command, "debug") == 0 || strcmp(command, "exec-debug") == 0);
    int exec_mode = (strcmp(command, "exec") == 0 || strcmp(command, "exec-debug") == 0);

    if (!(debug || strcmp(command, "run") == 0 || exec_mode)) {
        print_usage(argv[0]);
        free(source);
        return 1;
    }

    if (debug) {
        size_t n;
        fprintf(stderr, "[DEBUG] Input file: %s\n", path);
        fprintf(stderr, "[DEBUG] Source (first 200 chars):\n");
        n = strlen(source);
        fwrite(source, 1, n < 200 ? n : 200, stderr);
        fprintf(stderr, "\n");
    }

    /* Current pipeline parses directly from source */
    ast = parser_parse(source);
    if (debug) {
        if (!ast) {
            fprintf(stderr, "[DEBUG] AST: NULL\n");
        } else {
            fprintf(stderr, "[DEBUG] AST: kind=%d\n", ast->type);
        }
    }

    if (!ast) {
        fprintf(stderr, "Error: parse failed\n");
        free(source);
        return 1;
    }

    if (exec_mode) {
        /* Execute using HVM pipeline */
        if (debug) {
            fprintf(stderr, "[DEBUG] Executing via HVM pipeline...\n");
        }
        HVM_VM *vm = hvm_create();
        HVM_Compiler *compiler = vm ? hvm_compiler_create(vm) : NULL;
        if (!vm || !compiler) {
            fprintf(stderr, "Error: failed to initialize HVM runtime\n");
            if (compiler) hvm_compiler_destroy(compiler);
            if (vm) hvm_destroy(vm);
            ast_destroy(ast);
            free(source);
            return 1;
        }
        if (!hvm_compiler_compile_ast(compiler, ast) || hvm_compiler_has_errors(compiler)) {
            hvm_compiler_print_errors(compiler);
            hvm_compiler_destroy(compiler);
            hvm_destroy(vm);
            ast_destroy(ast);
            free(source);
            return 1;
        }
        hvm_platform_hide_console_if_needed(vm->instructions, vm->instruction_count);
        if (!hvm_run(vm)) {
            const char *msg = hvm_get_error(vm);
            fprintf(stderr, "VM execution failed%s%s\n", msg ? ": " : "", msg ? msg : "");
            hvm_compiler_destroy(compiler);
            hvm_destroy(vm);
            ast_destroy(ast);
            free(source);
            return 1;
        }
        hvm_compiler_destroy(compiler);
        hvm_destroy(vm);
    } else {
        /* Generate C code */
        char *output = codegen_generate(ast);
        if (!output) {
            fprintf(stderr, "Error: codegen failed\n");
            ast_destroy(ast);
            free(source);
            return 1;
        }
        printf("%s\n", output);
        free(output);
    }

    ast_destroy(ast);
    free(source);
    return 0;
}

