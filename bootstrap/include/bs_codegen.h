/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_codegen.h
 * Purpose: AST to Bytecode Compiler
 */

#ifndef HOSC_BOOTSTRAP_CODEGEN_H
#define HOSC_BOOTSTRAP_CODEGEN_H

#include "bs_ast.h"
#include "bs_bytecode.h"
#include "bs_command_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    int depth;
} BsLocal;

typedef struct {
    BsChunk *chunk;
    BsCommandRegistry *registry;
    BsLocal locals[256];
    int local_count;
    int scope_depth;
    bool had_error;
    char error_message[256];
} BsCompiler;

void bs_compiler_init(BsCompiler *compiler, BsChunk *chunk, BsCommandRegistry *registry);
bool bs_compile_ast(BsCompiler *compiler, ASTNode *node);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_CODEGEN_H */
