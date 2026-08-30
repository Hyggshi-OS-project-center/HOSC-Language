/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bootstrap.c
 * Purpose: High-level API: run source via interpreter or bytecode VM
 */

#include "bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bs_run_source_interpreter(BsRuntime *runtime, const char *source) {
    BsParser parser;
    bs_parser_init(&parser, source);
    ASTNode *program = bs_parser_parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "[Bootstrap] Parse error at line %d: %s\n",
                parser.error_line, parser.error_message);
        bs_ast_free(program);
        return false;
    }

    /* Macro expansion + command registration pass */
    BsAstRewriter rewriter;
    bs_ast_rewriter_init(&rewriter, &runtime->command_registry);
    ASTNode *rewritten = bs_ast_rewrite(&rewriter, program);

    /* Execute */
    BsEvalResult res = bs_eval_program(runtime, rewritten);
    bs_ast_free(rewritten);

    if (res.status == BS_EVAL_ERROR) {
        fprintf(stderr, "[Bootstrap] Runtime error: %s\n", runtime->error_message);
        bs_free_value(res.value);
        return false;
    }

    bs_free_value(res.value);
    return true;
}

bool bs_run_source_bytecode(BsRuntime *runtime, const char *source) {
    BsParser parser;
    bs_parser_init(&parser, source);
    ASTNode *program = bs_parser_parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "[Bootstrap] Parse error at line %d: %s\n",
                parser.error_line, parser.error_message);
        bs_ast_free(program);
        return false;
    }

    /* Macro expansion + command registration pass (interpreter mode) */
    BsAstRewriter rewriter;
    bs_ast_rewriter_init(&rewriter, &runtime->command_registry);
    ASTNode *rewritten = bs_ast_rewrite(&rewriter, program);

    /* Compile to bytecode */
    BsChunk chunk;
    bs_chunk_init(&chunk);

    BsCompiler compiler;
    bs_compiler_init(&compiler, &chunk, &runtime->command_registry);
    bool compile_ok = bs_compile_ast(&compiler, rewritten);
    bs_ast_free(rewritten);

    if (!compile_ok) {
        fprintf(stderr, "[Bootstrap] Compile error: %s\n", compiler.error_message);
        bs_chunk_free(&chunk);
        return false;
    }

    /* Execute in bytecode VM */
    BsVM vm;
    bs_vm_init(&vm, runtime);
    BsVMResult vm_res = bs_vm_run(&vm, &chunk);
    bs_vm_free(&vm);
    bs_chunk_free(&chunk);

    return vm_res == VM_RESULT_OK;
}

static char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[Bootstrap] Cannot open file: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

bool bs_run_file(BsRuntime *runtime, const char *filepath, bool use_vm) {
    char *source = read_file(filepath);
    if (!source) return false;

    bool ok = use_vm
        ? bs_run_source_bytecode(runtime, source)
        : bs_run_source_interpreter(runtime, source);

    free(source);
    return ok;
}
