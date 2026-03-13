/* hosc_engine.c - HOSC source file */

#include "ast.h"
#include "include/runtime.h"
#include "include/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// AST utility functions are now in ast_utils.c

// AST utility functions are now in ast_utils.c

// HOSC Engine functions
void hosc_engine_execute(ASTNode* ast) {
    printf("HOSC Engine: Executing AST directly\n");
    runtime_execute(ast);
}

void hosc_engine_compile(ASTNode* ast, const char* output_file) {
    printf("HOSC Engine: Compiling to C code\n");
    char* code = codegen_generate(ast);
    if (code) {
        printf("Generated code:\n%s\n", code);
        free(code);
    }
}

// Main function removed to avoid conflicts with core.c

