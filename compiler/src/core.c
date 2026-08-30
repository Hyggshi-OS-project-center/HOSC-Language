/*
 * File: compiler\src\core.c
 * Purpose: HOSC source file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "runtime.h"

static char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    rewind(f);
    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

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

    char *source = read_file(path);
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
        /* Execute directly using runtime */
        if (debug) {
            fprintf(stderr, "[DEBUG] Executing AST directly...\n");
        }
        runtime_execute(ast);
    } else {
        /* Generate C code */
        char *output = codegen_generate(ast);
        if (!output) {
            fprintf(stderr, "Error: codegen failed\n");
            free_ast(ast);
            ast_release_arena();
            free(source);
            return 1;
        }
        printf("%s\n", output);
        free(output);
    }

    free_ast(ast);
    ast_release_arena();
    free(source);
    return 0;
}
