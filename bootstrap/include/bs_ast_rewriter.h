/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_ast_rewriter.h
 * Purpose: Macro expansion, AST transformation, and syntax lowering pass
 */

#ifndef HOSC_BOOTSTRAP_AST_REWRITER_H
#define HOSC_BOOTSTRAP_AST_REWRITER_H

#include "bs_ast.h"
#include "bs_command_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BsCommandRegistry *registry;
    char error_message[256];
    bool has_error;
} BsAstRewriter;

void bs_ast_rewriter_init(BsAstRewriter *rewriter, BsCommandRegistry *registry);

/* Rewrites an AST node by expanding macros and resolving command definitions */
ASTNode* bs_ast_rewrite(BsAstRewriter *rewriter, ASTNode *node);

/* Helper to expand a macro definition with given arguments */
ASTNode* bs_ast_expand_macro(BsCommand *macro_cmd, ASTNodeList *args, ASTNode *body_block);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_AST_REWRITER_H */
