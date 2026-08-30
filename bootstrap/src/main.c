/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/main.c
 * Purpose: CLI entry point — hosc-bootstrap run/repl/compile/ast/disasm
 */

#include "bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "HOSC Bootstrap Compiler 0  (Compiler0 -> Self-Hosting Path)\n"
        "\n"
        "Usage:\n"
        "  %s run    <file.hosc>    Run using tree-walk interpreter (recommended)\n"
        "  %s vm     <file.hosc>    Compile to bytecode and run in stack VM\n"
        "  %s ast    <file.hosc>    Parse and dump AST to stdout\n"
        "  %s disasm <file.hosc>    Compile to bytecode and disassemble\n"
        "  %s repl                  Start interactive REPL\n"
        "  %s help                  Show this help\n"
        "\n"
        "Pipeline:\n"
        "  Source -> Lexer -> Parser -> AST -> Rewriter/Macros -> Interpreter\n"
        "                                                       -> Bytecode -> VM\n"
        "\n"
        "Extensibility:\n"
        "  command log(msg) { print(\"[LOG] \" + msg); }   // AST command\n"
        "  macro unless(cond) { if (!cond) { ... } }      // Syntax macro\n"
        "  syntax unless(cond) { if (!cond) { ... } }     // Alias for macro\n"
        "\n"
        "  bootstrap_register_command(runtime, \"name\", argc, fn); // C API\n"
        "\n",
        prog, prog, prog, prog, prog, prog
    );
}

/* Demo: C API custom command registration */
static BsValue native_make_window(BsRuntime *runtime, BsValue *args, size_t argc) {
    (void)runtime;
    printf("[WINDOW] Creating window");
    if (argc > 0 && args[0].type == BS_VAL_STRING) {
        printf(" — title: \"%s\"", args[0].as.string);
    }
    if (argc > 1 && args[1].type == BS_VAL_INT) {
        printf(", size: %ldx", (long)args[1].as.integer);
    }
    if (argc > 2 && args[2].type == BS_VAL_INT) {
        printf("%ld", (long)args[2].as.integer);
    }
    printf("\n");
    return bs_null();
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *subcommand = argv[1];

    if (strcmp(subcommand, "help") == 0 || strcmp(subcommand, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    /* Create runtime and register builtins + user C API commands */
    BsRuntime *runtime = bs_runtime_create();

    /* Register C API example: callwindow(title, width, height) */
    bootstrap_register_command(runtime, "callwindow", 3, native_make_window);

    /* REPL mode */
    if (strcmp(subcommand, "repl") == 0) {
        bs_repl_start(runtime);
        bs_runtime_free(runtime);
        return 0;
    }

    /* File modes */
    if (argc < 3) {
        fprintf(stderr, "Error: '%s' requires a .hosc file argument\n", subcommand);
        print_usage(argv[0]);
        bs_runtime_free(runtime);
        return 1;
    }

    const char *filepath = argv[2];

    if (strcmp(subcommand, "run") == 0) {
        bool ok = bs_run_file(runtime, filepath, false /* use interpreter */);
        bs_runtime_free(runtime);
        return ok ? 0 : 1;
    }

    if (strcmp(subcommand, "vm") == 0) {
        bool ok = bs_run_file(runtime, filepath, true /* use bytecode VM */);
        bs_runtime_free(runtime);
        return ok ? 0 : 1;
    }

    if (strcmp(subcommand, "ast") == 0) {
        FILE *f = fopen(filepath, "rb");
        if (!f) {
            fprintf(stderr, "Cannot open: %s\n", filepath);
            bs_runtime_free(runtime);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *source = (char*)malloc(size + 1);
        fread(source, 1, size, f);
        source[size] = '\0';
        fclose(f);

        BsParser parser;
        bs_parser_init(&parser, source);
        ASTNode *program = bs_parser_parse_program(&parser);
        free(source);

        if (parser.had_error) {
            fprintf(stderr, "Parse error: %s\n", parser.error_message);
            bs_ast_free(program);
            bs_runtime_free(runtime);
            return 1;
        }

        /* Rewrite macros */
        BsAstRewriter rewriter;
        bs_ast_rewriter_init(&rewriter, &runtime->command_registry);
        ASTNode *rewritten = bs_ast_rewrite(&rewriter, program);

        printf("=== AST Dump: %s ===\n", filepath);
        bs_ast_print(rewritten, 0);
        bs_ast_free(rewritten);
        bs_runtime_free(runtime);
        return 0;
    }

    if (strcmp(subcommand, "disasm") == 0) {
        FILE *f = fopen(filepath, "rb");
        if (!f) {
            fprintf(stderr, "Cannot open: %s\n", filepath);
            bs_runtime_free(runtime);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *source = (char*)malloc(size + 1);
        fread(source, 1, size, f);
        source[size] = '\0';
        fclose(f);

        BsParser parser;
        bs_parser_init(&parser, source);
        ASTNode *program = bs_parser_parse_program(&parser);
        free(source);

        if (parser.had_error) {
            fprintf(stderr, "Parse error: %s\n", parser.error_message);
            bs_ast_free(program);
            bs_runtime_free(runtime);
            return 1;
        }

        BsAstRewriter rewriter;
        bs_ast_rewriter_init(&rewriter, &runtime->command_registry);
        ASTNode *rewritten = bs_ast_rewrite(&rewriter, program);

        BsChunk chunk;
        bs_chunk_init(&chunk);
        BsCompiler compiler;
        bs_compiler_init(&compiler, &chunk, &runtime->command_registry);

        if (bs_compile_ast(&compiler, rewritten)) {
            bs_chunk_disassemble(&chunk, filepath);
        } else {
            fprintf(stderr, "Compile error: %s\n", compiler.error_message);
        }

        bs_ast_free(rewritten);
        bs_chunk_free(&chunk);
        bs_runtime_free(runtime);
        return compiler.had_error ? 1 : 0;
    }

    fprintf(stderr, "Unknown subcommand: '%s'\n", subcommand);
    print_usage(argv[0]);
    bs_runtime_free(runtime);
    return 1;
}
