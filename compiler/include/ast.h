/* ast.h - HOSC source file */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "arena.h"
#include "token.h"

typedef enum {
    AST_PROGRAM,
    AST_PACKAGE,
    AST_IMPORT,
    AST_FUNCTION,
    AST_BLOCK,
    AST_VARIABLE_DECLARATION,
    AST_ASSIGNMENT,
    AST_NUMBER,
    AST_FLOAT,
    AST_STRING,
    AST_BOOL,
    AST_IDENTIFIER,
    AST_CALL_EXPR,
    AST_UNARY_OP,
    AST_BINARY_OP,
    AST_PRINT_STATEMENT,
    AST_EXPR_STATEMENT,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_WINDOW_STMT,
    AST_TEXT_STMT,
    AST_EOF
} ASTNodeType;

struct ASTNode;

typedef struct ASTNodeList {
    struct ASTNode *node;
    struct ASTNodeList *next;
} ASTNodeList;

typedef struct ASTNode {
    ASTNodeType type;
    struct ASTNode *next;
    union {
        struct { char *name; } package;
        struct { char *path; } import_stmt;
        struct { char *name; char **params; size_t param_count; struct ASTNode *body; } function;
        struct { ASTNodeList *statements; } block;
        struct { char *identifier; struct ASTNode *value; int is_var; } variable_declaration;
        struct { char *identifier; struct ASTNode *value; } assignment;
        struct { int value; } number;
        struct { double value; } fnumber;
        struct { char *value; } string_lit;
        struct { int value; } boolean;
        struct { char *name; } identifier;
        struct { char *callee; ASTNodeList *arguments; } call_expr;
        struct { TokenType op; struct ASTNode *operand; } unary_op;
        struct { struct ASTNode *left; struct ASTNode *right; TokenType op; } binary_op;
        struct { struct ASTNode *expression; } print_statement;
        struct { struct ASTNode *expression; } expr_stmt;
        struct { struct ASTNode *condition; struct ASTNode *then_branch; struct ASTNode *else_branch; } if_stmt;
        struct { struct ASTNode *condition; struct ASTNode *body; } while_stmt;
        struct { struct ASTNode *init; struct ASTNode *condition; struct ASTNode *update; struct ASTNode *body; } for_stmt;
        struct { struct ASTNode *value; } return_stmt;
        struct { char *title; } window_stmt;
        struct { struct ASTNode *x; struct ASTNode *y; char *msg; } text_stmt;
        struct { struct ASTNode *package; ASTNodeList *declarations; } program;
    } data;
} ASTNode;

ASTNode* create_ast_node(ASTNodeType type);
ASTNode* parser_parse(const char* source);
ASTNodeList* ast_list_append(ASTNodeList* head, ASTNode* node);
void free_ast(ASTNode* node);
void ast_set_arena(Arena* arena);
Arena* ast_get_arena(void);
void ast_release_arena(void);

#ifdef __cplusplus
}
#endif

