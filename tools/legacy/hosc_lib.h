/*
 * File: tools\legacy\hosc_lib.h
 * Purpose: HOSC source file.
 */

#ifndef HOSC_LIB_H
#define HOSC_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"

// HOSC Library Context
typedef struct {
    void* runtime_context;
    void* compiler_state;
    void* parser_state;
} HOSCContext;

// HOSC Library Functions
HOSCContext* hosc_init(void);
void hosc_cleanup(HOSCContext* context);
int hosc_execute(HOSCContext* context, ASTNode* ast);
int hosc_compile(HOSCContext* context, ASTNode* ast, const char* output_file);
int hosc_quick_execute(ASTNode* ast);
int hosc_quick_compile(ASTNode* ast, const char* output_file);

#ifdef __cplusplus
}
#endif

#endif // HOSC_LIB_H
