#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "ast.h"
// Function to generate code from the abstract syntax tree
void generate_code(ASTNode *root);
// Function to initialize the code generator
void init_codegen();
// Function to finalize the code generation process
void finalize_codegen();
char *codegen_generate(ASTNode *ast);
#ifdef __cplusplus
}
#endif