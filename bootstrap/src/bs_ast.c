/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_ast.c
 * Purpose: AST memory management, construction, cloning, and formatting
 */

#include "bs_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bs_ast_list_init(ASTNodeList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void bs_ast_list_append(ASTNodeList *list, ASTNode *node) {
    if (!node) return;
    if (list->count + 1 > list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        list->items = (ASTNode**)realloc(list->items, sizeof(ASTNode*) * new_cap);
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

void bs_ast_list_free(ASTNodeList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        bs_ast_free(list->items[i]);
    }
    if (list->items) free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static ASTNode* alloc_node(ASTNodeType type, int line, int col) {
    ASTNode *n = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!n) {
        fprintf(stderr, "Fatal error: Out of memory allocating ASTNode\n");
        exit(1);
    }
    n->type = type;
    n->line = line;
    n->column = col;
    return n;
}

ASTNode* bs_ast_new_literal(BsValue val, int line, int col) {
    ASTNode *n = alloc_node(AST_EXPR_LITERAL, line, col);
    n->as.literal.value = bs_clone_value(val);
    return n;
}

ASTNode* bs_ast_new_identifier(const char *name, int line, int col) {
    ASTNode *n = alloc_node(AST_EXPR_IDENTIFIER, line, col);
    n->as.identifier.name = name ? strdup(name) : NULL;
    return n;
}

ASTNode* bs_ast_new_unary(BsTokenType op, ASTNode *operand, int line, int col) {
    ASTNode *n = alloc_node(AST_EXPR_UNARY, line, col);
    n->as.unary.op = op;
    n->as.unary.operand = operand;
    return n;
}

ASTNode* bs_ast_new_binary(BsTokenType op, ASTNode *left, ASTNode *right, int line, int col) {
    ASTNode *n = alloc_node(AST_EXPR_BINARY, line, col);
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

ASTNode* bs_ast_new_assign(const char *target, ASTNode *value, int line, int col) {
    ASTNode *n = alloc_node(AST_EXPR_ASSIGN, line, col);
    n->as.assign.target_name = target ? strdup(target) : NULL;
    n->as.assign.value = value;
    return n;
}

ASTNode* bs_ast_new_call(const char *callee, ASTNodeList args, int line, int col) {
    ASTNode *n = alloc_node(AST_EXPR_CALL, line, col);
    n->as.call.callee = callee ? strdup(callee) : NULL;
    n->as.call.args = args;
    return n;
}

ASTNode* bs_ast_new_var_decl(const char *name, bool is_const, ASTNode *init, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_VAR_DECL, line, col);
    n->as.var_decl.name = name ? strdup(name) : NULL;
    n->as.var_decl.is_const = is_const;
    n->as.var_decl.initializer = init;
    return n;
}

ASTNode* bs_ast_new_expr_stmt(ASTNode *expr, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_EXPR, line, col);
    n->as.expr_stmt.expr = expr;
    return n;
}

ASTNode* bs_ast_new_block(ASTNodeList statements, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_BLOCK, line, col);
    n->as.block.statements = statements;
    return n;
}

ASTNode* bs_ast_new_if(ASTNode *cond, ASTNode *then_b, ASTNode *else_b, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_IF, line, col);
    n->as.if_stmt.condition = cond;
    n->as.if_stmt.then_branch = then_b;
    n->as.if_stmt.else_branch = else_b;
    return n;
}

ASTNode* bs_ast_new_while(ASTNode *cond, ASTNode *body, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_WHILE, line, col);
    n->as.while_stmt.condition = cond;
    n->as.while_stmt.body = body;
    return n;
}

ASTNode* bs_ast_new_for(ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_FOR, line, col);
    n->as.for_stmt.init = init;
    n->as.for_stmt.condition = cond;
    n->as.for_stmt.update = update;
    n->as.for_stmt.body = body;
    return n;
}

ASTNode* bs_ast_new_return(ASTNode *val, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_RETURN, line, col);
    n->as.return_stmt.value = val;
    return n;
}

ASTNode* bs_ast_new_break(int line, int col) {
    return alloc_node(AST_STMT_BREAK, line, col);
}

ASTNode* bs_ast_new_continue(int line, int col) {
    return alloc_node(AST_STMT_CONTINUE, line, col);
}

ASTNode* bs_ast_new_print(ASTNode *expr, bool is_raw, bool add_newline, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_PRINT, line, col);
    n->as.print_stmt.expr = expr;
    n->as.print_stmt.is_raw = is_raw;
    n->as.print_stmt.add_newline = add_newline;
    return n;
}

ASTNode* bs_ast_new_func_decl(const char *name, size_t param_count, char **param_names, ASTNode *body, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_FUNC_DECL, line, col);
    n->as.func_decl.name = name ? strdup(name) : NULL;
    n->as.func_decl.param_count = param_count;
    if (param_count > 0 && param_names) {
        n->as.func_decl.param_names = (char**)malloc(sizeof(char*) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            n->as.func_decl.param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    } else {
        n->as.func_decl.param_names = NULL;
    }
    n->as.func_decl.body = body;
    return n;
}

ASTNode* bs_ast_new_package(const char *name, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_PACKAGE, line, col);
    n->as.module_stmt.path_or_name = name ? strdup(name) : NULL;
    return n;
}

ASTNode* bs_ast_new_import(const char *path, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_IMPORT, line, col);
    n->as.module_stmt.path_or_name = path ? strdup(path) : NULL;
    return n;
}

ASTNode* bs_ast_new_command_def(const char *name, size_t param_count, char **param_names, ASTNode *body, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_COMMAND_DEF, line, col);
    n->as.extension_def.name = name ? strdup(name) : NULL;
    n->as.extension_def.param_count = param_count;
    if (param_count > 0 && param_names) {
        n->as.extension_def.param_names = (char**)malloc(sizeof(char*) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            n->as.extension_def.param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    } else {
        n->as.extension_def.param_names = NULL;
    }
    n->as.extension_def.body = body;
    return n;
}

ASTNode* bs_ast_new_macro_def(const char *name, size_t param_count, char **param_names, ASTNode *body, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_MACRO_DEF, line, col);
    n->as.extension_def.name = name ? strdup(name) : NULL;
    n->as.extension_def.param_count = param_count;
    if (param_count > 0 && param_names) {
        n->as.extension_def.param_names = (char**)malloc(sizeof(char*) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            n->as.extension_def.param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    } else {
        n->as.extension_def.param_names = NULL;
    }
    n->as.extension_def.body = body;
    return n;
}

ASTNode* bs_ast_new_custom_call(const char *name, ASTNodeList args, ASTNode *body_block, int line, int col) {
    ASTNode *n = alloc_node(AST_STMT_CUSTOM_CALL, line, col);
    n->as.custom_call.command_name = name ? strdup(name) : NULL;
    n->as.custom_call.args = args;
    n->as.custom_call.body_block = body_block;
    return n;
}

ASTNode* bs_ast_clone(const ASTNode *node) {
    if (!node) return NULL;
    ASTNode *c = alloc_node(node->type, node->line, node->column);
    switch (node->type) {
        case AST_EXPR_LITERAL:
            c->as.literal.value = bs_clone_value(node->as.literal.value);
            break;
        case AST_EXPR_IDENTIFIER:
            c->as.identifier.name = node->as.identifier.name ? strdup(node->as.identifier.name) : NULL;
            break;
        case AST_EXPR_UNARY:
            c->as.unary.op = node->as.unary.op;
            c->as.unary.operand = bs_ast_clone(node->as.unary.operand);
            break;
        case AST_EXPR_BINARY:
            c->as.binary.op = node->as.binary.op;
            c->as.binary.left = bs_ast_clone(node->as.binary.left);
            c->as.binary.right = bs_ast_clone(node->as.binary.right);
            break;
        case AST_EXPR_ASSIGN:
            c->as.assign.target_name = node->as.assign.target_name ? strdup(node->as.assign.target_name) : NULL;
            c->as.assign.value = bs_ast_clone(node->as.assign.value);
            break;
        case AST_EXPR_CALL:
            c->as.call.callee = node->as.call.callee ? strdup(node->as.call.callee) : NULL;
            bs_ast_list_init(&c->as.call.args);
            for (size_t i = 0; i < node->as.call.args.count; i++) {
                bs_ast_list_append(&c->as.call.args, bs_ast_clone(node->as.call.args.items[i]));
            }
            break;
        case AST_STMT_VAR_DECL:
            c->as.var_decl.name = node->as.var_decl.name ? strdup(node->as.var_decl.name) : NULL;
            c->as.var_decl.is_const = node->as.var_decl.is_const;
            c->as.var_decl.initializer = bs_ast_clone(node->as.var_decl.initializer);
            break;
        case AST_STMT_EXPR:
            c->as.expr_stmt.expr = bs_ast_clone(node->as.expr_stmt.expr);
            break;
        case AST_STMT_BLOCK:
            bs_ast_list_init(&c->as.block.statements);
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                bs_ast_list_append(&c->as.block.statements, bs_ast_clone(node->as.block.statements.items[i]));
            }
            break;
        case AST_STMT_IF:
            c->as.if_stmt.condition = bs_ast_clone(node->as.if_stmt.condition);
            c->as.if_stmt.then_branch = bs_ast_clone(node->as.if_stmt.then_branch);
            c->as.if_stmt.else_branch = bs_ast_clone(node->as.if_stmt.else_branch);
            break;
        case AST_STMT_WHILE:
            c->as.while_stmt.condition = bs_ast_clone(node->as.while_stmt.condition);
            c->as.while_stmt.body = bs_ast_clone(node->as.while_stmt.body);
            break;
        case AST_STMT_FOR:
            c->as.for_stmt.init = bs_ast_clone(node->as.for_stmt.init);
            c->as.for_stmt.condition = bs_ast_clone(node->as.for_stmt.condition);
            c->as.for_stmt.update = bs_ast_clone(node->as.for_stmt.update);
            c->as.for_stmt.body = bs_ast_clone(node->as.for_stmt.body);
            break;
        case AST_STMT_RETURN:
            c->as.return_stmt.value = bs_ast_clone(node->as.return_stmt.value);
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        case AST_STMT_PRINT:
            c->as.print_stmt.expr = bs_ast_clone(node->as.print_stmt.expr);
            c->as.print_stmt.is_raw = node->as.print_stmt.is_raw;
            c->as.print_stmt.add_newline = node->as.print_stmt.add_newline;
            break;
        case AST_STMT_FUNC_DECL:
            c->as.func_decl.name = node->as.func_decl.name ? strdup(node->as.func_decl.name) : NULL;
            c->as.func_decl.param_count = node->as.func_decl.param_count;
            if (node->as.func_decl.param_count > 0 && node->as.func_decl.param_names) {
                c->as.func_decl.param_names = (char**)malloc(sizeof(char*) * node->as.func_decl.param_count);
                for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                    c->as.func_decl.param_names[i] = node->as.func_decl.param_names[i] ? strdup(node->as.func_decl.param_names[i]) : NULL;
                }
            }
            c->as.func_decl.body = bs_ast_clone(node->as.func_decl.body);
            break;
        case AST_STMT_PACKAGE:
        case AST_STMT_IMPORT:
            c->as.module_stmt.path_or_name = node->as.module_stmt.path_or_name ? strdup(node->as.module_stmt.path_or_name) : NULL;
            break;
        case AST_STMT_COMMAND_DEF:
        case AST_STMT_MACRO_DEF:
            c->as.extension_def.name = node->as.extension_def.name ? strdup(node->as.extension_def.name) : NULL;
            c->as.extension_def.param_count = node->as.extension_def.param_count;
            if (node->as.extension_def.param_count > 0 && node->as.extension_def.param_names) {
                c->as.extension_def.param_names = (char**)malloc(sizeof(char*) * node->as.extension_def.param_count);
                for (size_t i = 0; i < node->as.extension_def.param_count; i++) {
                    c->as.extension_def.param_names[i] = node->as.extension_def.param_names[i] ? strdup(node->as.extension_def.param_names[i]) : NULL;
                }
            }
            c->as.extension_def.body = bs_ast_clone(node->as.extension_def.body);
            break;
        case AST_STMT_CUSTOM_CALL:
            c->as.custom_call.command_name = node->as.custom_call.command_name ? strdup(node->as.custom_call.command_name) : NULL;
            bs_ast_list_init(&c->as.custom_call.args);
            for (size_t i = 0; i < node->as.custom_call.args.count; i++) {
                bs_ast_list_append(&c->as.custom_call.args, bs_ast_clone(node->as.custom_call.args.items[i]));
            }
            c->as.custom_call.body_block = bs_ast_clone(node->as.custom_call.body_block);
            break;
    }
    return c;
}

void bs_ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_EXPR_LITERAL:
            bs_free_value(node->as.literal.value);
            break;
        case AST_EXPR_IDENTIFIER:
            if (node->as.identifier.name) free(node->as.identifier.name);
            break;
        case AST_EXPR_UNARY:
            bs_ast_free(node->as.unary.operand);
            break;
        case AST_EXPR_BINARY:
            bs_ast_free(node->as.binary.left);
            bs_ast_free(node->as.binary.right);
            break;
        case AST_EXPR_ASSIGN:
            if (node->as.assign.target_name) free(node->as.assign.target_name);
            bs_ast_free(node->as.assign.value);
            break;
        case AST_EXPR_CALL:
            if (node->as.call.callee) free(node->as.call.callee);
            bs_ast_list_free(&node->as.call.args);
            break;
        case AST_STMT_VAR_DECL:
            if (node->as.var_decl.name) free(node->as.var_decl.name);
            bs_ast_free(node->as.var_decl.initializer);
            break;
        case AST_STMT_EXPR:
            bs_ast_free(node->as.expr_stmt.expr);
            break;
        case AST_STMT_BLOCK:
            bs_ast_list_free(&node->as.block.statements);
            break;
        case AST_STMT_IF:
            bs_ast_free(node->as.if_stmt.condition);
            bs_ast_free(node->as.if_stmt.then_branch);
            bs_ast_free(node->as.if_stmt.else_branch);
            break;
        case AST_STMT_WHILE:
            bs_ast_free(node->as.while_stmt.condition);
            bs_ast_free(node->as.while_stmt.body);
            break;
        case AST_STMT_FOR:
            bs_ast_free(node->as.for_stmt.init);
            bs_ast_free(node->as.for_stmt.condition);
            bs_ast_free(node->as.for_stmt.update);
            bs_ast_free(node->as.for_stmt.body);
            break;
        case AST_STMT_RETURN:
            bs_ast_free(node->as.return_stmt.value);
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        case AST_STMT_PRINT:
            bs_ast_free(node->as.print_stmt.expr);
            break;
        case AST_STMT_FUNC_DECL:
            if (node->as.func_decl.name) free(node->as.func_decl.name);
            if (node->as.func_decl.param_names) {
                for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                    if (node->as.func_decl.param_names[i]) free(node->as.func_decl.param_names[i]);
                }
                free(node->as.func_decl.param_names);
            }
            bs_ast_free(node->as.func_decl.body);
            break;
        case AST_STMT_PACKAGE:
        case AST_STMT_IMPORT:
            if (node->as.module_stmt.path_or_name) free(node->as.module_stmt.path_or_name);
            break;
        case AST_STMT_COMMAND_DEF:
        case AST_STMT_MACRO_DEF:
            if (node->as.extension_def.name) free(node->as.extension_def.name);
            if (node->as.extension_def.param_names) {
                for (size_t i = 0; i < node->as.extension_def.param_count; i++) {
                    if (node->as.extension_def.param_names[i]) free(node->as.extension_def.param_names[i]);
                }
                free(node->as.extension_def.param_names);
            }
            bs_ast_free(node->as.extension_def.body);
            break;
        case AST_STMT_CUSTOM_CALL:
            if (node->as.custom_call.command_name) free(node->as.custom_call.command_name);
            bs_ast_list_free(&node->as.custom_call.args);
            bs_ast_free(node->as.custom_call.body_block);
            break;
    }
    free(node);
}

void bs_ast_print(const ASTNode *node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) printf("  ");
    switch (node->type) {
        case AST_EXPR_LITERAL:
            printf("Literal: ");
            bs_print_value(node->as.literal.value);
            printf("\n");
            break;
        case AST_EXPR_IDENTIFIER:
            printf("Identifier: %s\n", node->as.identifier.name);
            break;
        case AST_EXPR_UNARY:
            printf("Unary %s\n", bs_token_type_name(node->as.unary.op));
            bs_ast_print(node->as.unary.operand, indent + 1);
            break;
        case AST_EXPR_BINARY:
            printf("Binary %s\n", bs_token_type_name(node->as.binary.op));
            bs_ast_print(node->as.binary.left, indent + 1);
            bs_ast_print(node->as.binary.right, indent + 1);
            break;
        case AST_EXPR_ASSIGN:
            printf("Assign to %s\n", node->as.assign.target_name);
            bs_ast_print(node->as.assign.value, indent + 1);
            break;
        case AST_EXPR_CALL:
            printf("Call %s (args: %zu)\n", node->as.call.callee, node->as.call.args.count);
            for (size_t i = 0; i < node->as.call.args.count; i++) {
                bs_ast_print(node->as.call.args.items[i], indent + 1);
            }
            break;
        case AST_STMT_VAR_DECL:
            printf("VarDecl %s (const: %d)\n", node->as.var_decl.name, node->as.var_decl.is_const);
            if (node->as.var_decl.initializer) {
                bs_ast_print(node->as.var_decl.initializer, indent + 1);
            }
            break;
        case AST_STMT_EXPR:
            printf("ExprStmt\n");
            bs_ast_print(node->as.expr_stmt.expr, indent + 1);
            break;
        case AST_STMT_BLOCK:
            printf("Block (%zu stmts)\n", node->as.block.statements.count);
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                bs_ast_print(node->as.block.statements.items[i], indent + 1);
            }
            break;
        case AST_STMT_IF:
            printf("If\n");
            bs_ast_print(node->as.if_stmt.condition, indent + 1);
            printf("  Then:\n");
            bs_ast_print(node->as.if_stmt.then_branch, indent + 1);
            if (node->as.if_stmt.else_branch) {
                printf("  Else:\n");
                bs_ast_print(node->as.if_stmt.else_branch, indent + 1);
            }
            break;
        case AST_STMT_WHILE:
            printf("While\n");
            bs_ast_print(node->as.while_stmt.condition, indent + 1);
            bs_ast_print(node->as.while_stmt.body, indent + 1);
            break;
        case AST_STMT_FOR:
            printf("For\n");
            if (node->as.for_stmt.init) bs_ast_print(node->as.for_stmt.init, indent + 1);
            if (node->as.for_stmt.condition) bs_ast_print(node->as.for_stmt.condition, indent + 1);
            if (node->as.for_stmt.update) bs_ast_print(node->as.for_stmt.update, indent + 1);
            bs_ast_print(node->as.for_stmt.body, indent + 1);
            break;
        case AST_STMT_RETURN:
            printf("Return\n");
            if (node->as.return_stmt.value) bs_ast_print(node->as.return_stmt.value, indent + 1);
            break;
        case AST_STMT_BREAK:
            printf("Break\n");
            break;
        case AST_STMT_CONTINUE:
            printf("Continue\n");
            break;
        case AST_STMT_PRINT:
            printf("Print (raw: %d, newline: %d)\n", node->as.print_stmt.is_raw, node->as.print_stmt.add_newline);
            bs_ast_print(node->as.print_stmt.expr, indent + 1);
            break;
        case AST_STMT_FUNC_DECL:
            printf("FuncDecl %s (%zu params)\n", node->as.func_decl.name, node->as.func_decl.param_count);
            bs_ast_print(node->as.func_decl.body, indent + 1);
            break;
        case AST_STMT_PACKAGE:
            printf("Package %s\n", node->as.module_stmt.path_or_name);
            break;
        case AST_STMT_IMPORT:
            printf("Import %s\n", node->as.module_stmt.path_or_name);
            break;
        case AST_STMT_COMMAND_DEF:
            printf("CommandDef %s (%zu params)\n", node->as.extension_def.name, node->as.extension_def.param_count);
            bs_ast_print(node->as.extension_def.body, indent + 1);
            break;
        case AST_STMT_MACRO_DEF:
            printf("MacroDef %s (%zu params)\n", node->as.extension_def.name, node->as.extension_def.param_count);
            bs_ast_print(node->as.extension_def.body, indent + 1);
            break;
        case AST_STMT_CUSTOM_CALL:
            printf("CustomCall %s (%zu args, has_block: %d)\n",
                   node->as.custom_call.command_name,
                   node->as.custom_call.args.count,
                   node->as.custom_call.body_block != NULL);
            for (size_t i = 0; i < node->as.custom_call.args.count; i++) {
                bs_ast_print(node->as.custom_call.args.items[i], indent + 1);
            }
            if (node->as.custom_call.body_block) {
                bs_ast_print(node->as.custom_call.body_block, indent + 1);
            }
            break;
    }
}
