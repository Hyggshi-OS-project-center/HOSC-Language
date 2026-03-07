#include "hosc_lib.h"
#include "include/runtime.h"
#include "include/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// HOSC Library Context
HOSCContext* hosc_init(void) {
    HOSCContext* context = malloc(sizeof(HOSCContext));
    if (context) {
        context->runtime_context = NULL;
        context->compiler_state = NULL;
        context->parser_state = NULL;
        printf("HOSC Library initialized\n");
    }
    return context;
}

void hosc_cleanup(HOSCContext* context) {
    if (context) {
        free(context);
        printf("HOSC Library cleaned up\n");
    }
}

int hosc_execute(HOSCContext* context, ASTNode* ast) {
    if (!context || !ast) return 0;
    
    printf("HOSC Library: Executing AST\n");
    runtime_execute(ast);
    return 1;
}

int hosc_compile(HOSCContext* context, ASTNode* ast, const char* output_file) {
    if (!context || !ast || !output_file) return 0;
    
    printf("HOSC Library: Compiling to C code\n");
    char* code = codegen_generate(ast);
    if (code) {
        printf("Generated code:\n%s\n", code);
        free(code);
    }
    return 1;
}

int hosc_quick_execute(ASTNode* ast) {
    if (!ast) return 0;
    
    printf("HOSC Library: Quick execute\n");
    runtime_execute(ast);
    return 1;
}

int hosc_quick_compile(ASTNode* ast, const char* output_file) {
    if (!ast || !output_file) return 0;
    
    printf("HOSC Library: Quick compile\n");
    char* code = codegen_generate(ast);
    if (code) {
        printf("Generated code:\n%s\n", code);
        free(code);
    }
    return 1;
}
