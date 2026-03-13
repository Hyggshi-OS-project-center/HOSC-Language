/*
 * File: compiler\src\ast_utils.c
 * Purpose: HOSC source file.
 */

#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "arena.h"

static Arena* g_ast_arena = NULL;

void ast_set_arena(Arena* arena) { g_ast_arena = arena; }
Arena* ast_get_arena(void) { return g_ast_arena; }
void ast_release_arena(void) {
    if (g_ast_arena) {
        arena_destroy(g_ast_arena);
        g_ast_arena = NULL;
    }
}

ASTNode* create_ast_node(ASTNodeType type) {
    ASTNode* node = g_ast_arena ? (ASTNode*)arena_alloc(g_ast_arena, sizeof(ASTNode)) : (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!node) return NULL;
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    return node;
}

ASTNodeList* ast_list_append(ASTNodeList* head, ASTNode* node) {
    ASTNodeList* item;
    ASTNodeList* cursor;
    if (!node) return head;
    item = g_ast_arena ? (ASTNodeList*)arena_alloc(g_ast_arena, sizeof(ASTNodeList)) : (ASTNodeList*)calloc(1, sizeof(ASTNodeList));
    if (!item) return head;
    memset(item, 0, sizeof(ASTNodeList));
    item->node = node;
    if (!head) return item;
    cursor = head;
    while (cursor->next) cursor = cursor->next;
    cursor->next = item;
    return head;
}

static void free_list(ASTNodeList* list) {
    ASTNodeList* cursor = list;
    while (cursor) {
        ASTNodeList* next = cursor->next;
        if (cursor->node) free_ast(cursor->node);
        if (!g_ast_arena) free(cursor);
        cursor = next;
    }
}

void free_ast(ASTNode* node) {
    size_t i;
    if (!node) return;
    if (g_ast_arena) return; // arena will be released once after compile

    switch (node->type) {
        case AST_PACKAGE:
            free(node->data.package.name);
            break;
        case AST_IMPORT:
            free(node->data.import_stmt.path);
            break;
        case AST_FUNCTION:
            free(node->data.function.name);
            if (node->data.function.params) {
                for (i = 0; i < node->data.function.param_count; i++) {
                    free(node->data.function.params[i]);
                }
                free(node->data.function.params);
            }
            free_ast(node->data.function.body);
            break;
        case AST_BLOCK:
            free_list(node->data.block.statements);
            break;
        case AST_VARIABLE_DECLARATION:
            free(node->data.variable_declaration.identifier);
            free_ast(node->data.variable_declaration.value);
            break;
        case AST_ASSIGNMENT:
            free(node->data.assignment.identifier);
            free_ast(node->data.assignment.value);
            break;
        case AST_IDENTIFIER:
            free(node->data.identifier.name);
            break;
        case AST_CALL_EXPR:
            free(node->data.call_expr.callee);
            free_list(node->data.call_expr.arguments);
            break;
        case AST_UNARY_OP:
            free_ast(node->data.unary_op.operand);
            break;
        case AST_STRING:
            free(node->data.string_lit.value);
            break;
        case AST_BINARY_OP:
            free_ast(node->data.binary_op.left);
            free_ast(node->data.binary_op.right);
            break;
        case AST_PRINT_STATEMENT:
            free_ast(node->data.print_statement.expression);
            break;
        case AST_EXPR_STATEMENT:
            free_ast(node->data.expr_stmt.expression);
            break;
        case AST_IF:
            free_ast(node->data.if_stmt.condition);
            free_ast(node->data.if_stmt.then_branch);
            free_ast(node->data.if_stmt.else_branch);
            break;
        case AST_WHILE:
            free_ast(node->data.while_stmt.condition);
            free_ast(node->data.while_stmt.body);
            break;
        case AST_FOR:
            free_ast(node->data.for_stmt.init);
            free_ast(node->data.for_stmt.condition);
            free_ast(node->data.for_stmt.update);
            free_ast(node->data.for_stmt.body);
            break;
        case AST_RETURN:
            free_ast(node->data.return_stmt.value);
            break;
        case AST_BREAK:
        case AST_CONTINUE:
            break;
        case AST_WINDOW_STMT:
            free(node->data.window_stmt.title);
            break;
        case AST_TEXT_STMT:
            free_ast(node->data.text_stmt.x);
            free_ast(node->data.text_stmt.y);
            free(node->data.text_stmt.msg);
            break;
        case AST_PROGRAM:
            if (node->data.program.package) free_ast(node->data.program.package);
            free_list(node->data.program.declarations);
            break;
        case AST_NUMBER:
        case AST_FLOAT:
        case AST_BOOL:
        case AST_EOF:
        default:
            break;
    }

    free(node);
}
