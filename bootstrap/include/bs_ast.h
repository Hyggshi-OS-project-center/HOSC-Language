/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_ast.h
 * Purpose: Abstract Syntax Tree definitions for core language, macros, and dynamic commands
 */

#ifndef HOSC_BOOTSTRAP_AST_H
#define HOSC_BOOTSTRAP_AST_H

#include "bs_value.h"
#include "bs_token.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Expressions */
    AST_EXPR_LITERAL,
    AST_EXPR_IDENTIFIER,
    AST_EXPR_UNARY,
    AST_EXPR_BINARY,
    AST_EXPR_ASSIGN,
    AST_EXPR_CALL,

    /* Statements */
    AST_STMT_VAR_DECL,
    AST_STMT_EXPR,
    AST_STMT_BLOCK,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_FOR,
    AST_STMT_RETURN,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_PRINT,
    AST_STMT_FUNC_DECL,
    AST_STMT_PACKAGE,
    AST_STMT_IMPORT,

    /* Extensibility & Metaprogramming */
    AST_STMT_COMMAND_DEF,   /* 'command name(...) { ... }' */
    AST_STMT_MACRO_DEF,     /* 'macro name(...) { ... }' / 'syntax name(...) { ... }' */
    AST_STMT_CUSTOM_CALL    /* Invocations of registered custom command or macro */
} ASTNodeType;

typedef struct ASTNode ASTNode;

/* Node list for blocks, argument lists, parameter lists, etc. */
typedef struct ASTNodeList {
    ASTNode **items;
    size_t count;
    size_t capacity;
} ASTNodeList;

struct ASTNode {
    ASTNodeType type;
    int line;
    int column;

    union {
        /* AST_EXPR_LITERAL */
        struct {
            BsValue value;
        } literal;

        /* AST_EXPR_IDENTIFIER */
        struct {
            char *name;
        } identifier;

        /* AST_EXPR_UNARY */
        struct {
            BsTokenType op;
            ASTNode *operand;
        } unary;

        /* AST_EXPR_BINARY */
        struct {
            BsTokenType op;
            ASTNode *left;
            ASTNode *right;
        } binary;

        /* AST_EXPR_ASSIGN */
        struct {
            char *target_name;
            ASTNode *value;
        } assign;

        /* AST_EXPR_CALL */
        struct {
            char *callee;
            ASTNodeList args;
        } call;

        /* AST_STMT_VAR_DECL */
        struct {
            char *name;
            bool is_const;
            ASTNode *initializer;
        } var_decl;

        /* AST_STMT_EXPR */
        struct {
            ASTNode *expr;
        } expr_stmt;

        /* AST_STMT_BLOCK */
        struct {
            ASTNodeList statements;
        } block;

        /* AST_STMT_IF */
        struct {
            ASTNode *condition;
            ASTNode *then_branch;
            ASTNode *else_branch; /* may be NULL */
        } if_stmt;

        /* AST_STMT_WHILE */
        struct {
            ASTNode *condition;
            ASTNode *body;
        } while_stmt;

        /* AST_STMT_FOR */
        struct {
            ASTNode *init;
            ASTNode *condition;
            ASTNode *update;
            ASTNode *body;
        } for_stmt;

        /* AST_STMT_RETURN */
        struct {
            ASTNode *value; /* may be NULL */
        } return_stmt;

        /* AST_STMT_PRINT */
        struct {
            ASTNode *expr;
            bool is_raw;     /* prints vs print */
            bool add_newline;
        } print_stmt;

        /* AST_STMT_FUNC_DECL */
        struct {
            char *name;
            size_t param_count;
            char **param_names;
            ASTNode *body;
        } func_decl;

        /* AST_STMT_PACKAGE & AST_STMT_IMPORT */
        struct {
            char *path_or_name;
        } module_stmt;

        /* AST_STMT_COMMAND_DEF & AST_STMT_MACRO_DEF */
        struct {
            char *name;
            size_t param_count;
            char **param_names;
            ASTNode *body;
        } extension_def;

        /* AST_STMT_CUSTOM_CALL */
        struct {
            char *command_name;
            ASTNodeList args;
            ASTNode *body_block; /* Optional trailing block */
        } custom_call;
    } as;
};

/* AST Node List operations */
void bs_ast_list_init(ASTNodeList *list);
void bs_ast_list_append(ASTNodeList *list, ASTNode *node);
void bs_ast_list_free(ASTNodeList *list);

/* AST Node Constructors */
ASTNode* bs_ast_new_literal(BsValue val, int line, int col);
ASTNode* bs_ast_new_identifier(const char *name, int line, int col);
ASTNode* bs_ast_new_unary(BsTokenType op, ASTNode *operand, int line, int col);
ASTNode* bs_ast_new_binary(BsTokenType op, ASTNode *left, ASTNode *right, int line, int col);
ASTNode* bs_ast_new_assign(const char *target, ASTNode *value, int line, int col);
ASTNode* bs_ast_new_call(const char *callee, ASTNodeList args, int line, int col);

ASTNode* bs_ast_new_var_decl(const char *name, bool is_const, ASTNode *init, int line, int col);
ASTNode* bs_ast_new_expr_stmt(ASTNode *expr, int line, int col);
ASTNode* bs_ast_new_block(ASTNodeList statements, int line, int col);
ASTNode* bs_ast_new_if(ASTNode *cond, ASTNode *then_b, ASTNode *else_b, int line, int col);
ASTNode* bs_ast_new_while(ASTNode *cond, ASTNode *body, int line, int col);
ASTNode* bs_ast_new_for(ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body, int line, int col);
ASTNode* bs_ast_new_return(ASTNode *val, int line, int col);
ASTNode* bs_ast_new_break(int line, int col);
ASTNode* bs_ast_new_continue(int line, int col);
ASTNode* bs_ast_new_print(ASTNode *expr, bool is_raw, bool add_newline, int line, int col);
ASTNode* bs_ast_new_func_decl(const char *name, size_t param_count, char **param_names, ASTNode *body, int line, int col);
ASTNode* bs_ast_new_package(const char *name, int line, int col);
ASTNode* bs_ast_new_import(const char *path, int line, int col);

/* Extensibility Constructors */
ASTNode* bs_ast_new_command_def(const char *name, size_t param_count, char **param_names, ASTNode *body, int line, int col);
ASTNode* bs_ast_new_macro_def(const char *name, size_t param_count, char **param_names, ASTNode *body, int line, int col);
ASTNode* bs_ast_new_custom_call(const char *name, ASTNodeList args, ASTNode *body_block, int line, int col);

/* AST Utilities */
ASTNode* bs_ast_clone(const ASTNode *node);
void bs_ast_free(ASTNode *node);
void bs_ast_print(const ASTNode *node, int indent);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_AST_H */
