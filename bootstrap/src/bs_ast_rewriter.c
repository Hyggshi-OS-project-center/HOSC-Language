/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_ast_rewriter.c
 * Purpose: AST Rewriter and Macro Expander implementation
 */

#include "bs_ast_rewriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bs_ast_rewriter_init(BsAstRewriter *rewriter, BsCommandRegistry *registry) {
    rewriter->registry = registry;
    rewriter->has_error = false;
    rewriter->error_message[0] = '\0';
}

/* Helper to substitute identifiers matching parameter names with argument AST trees */
static ASTNode* substitute_ast_params(const ASTNode *node, size_t param_count, char **param_names, ASTNode **arg_nodes) {
    if (!node) return NULL;

    /* If node is an identifier matching a parameter name, replace it with the argument node */
    if (node->type == AST_EXPR_IDENTIFIER && node->as.identifier.name) {
        for (size_t i = 0; i < param_count; i++) {
            if (param_names[i] && strcmp(node->as.identifier.name, param_names[i]) == 0) {
                return bs_ast_clone(arg_nodes[i]);
            }
        }
        return bs_ast_clone(node);
    }

    /* Otherwise, clone and recursively substitute children */
    ASTNode *c = (ASTNode*)calloc(1, sizeof(ASTNode));
    c->type = node->type;
    c->line = node->line;
    c->column = node->column;

    switch (node->type) {
        case AST_EXPR_LITERAL:
            c->as.literal.value = bs_clone_value(node->as.literal.value);
            break;
        case AST_EXPR_IDENTIFIER:
            c->as.identifier.name = node->as.identifier.name ? strdup(node->as.identifier.name) : NULL;
            break;
        case AST_EXPR_UNARY:
            c->as.unary.op = node->as.unary.op;
            c->as.unary.operand = substitute_ast_params(node->as.unary.operand, param_count, param_names, arg_nodes);
            break;
        case AST_EXPR_BINARY:
            c->as.binary.op = node->as.binary.op;
            c->as.binary.left = substitute_ast_params(node->as.binary.left, param_count, param_names, arg_nodes);
            c->as.binary.right = substitute_ast_params(node->as.binary.right, param_count, param_names, arg_nodes);
            break;
        case AST_EXPR_ASSIGN:
            c->as.assign.target_name = node->as.assign.target_name ? strdup(node->as.assign.target_name) : NULL;
            c->as.assign.value = substitute_ast_params(node->as.assign.value, param_count, param_names, arg_nodes);
            break;
        case AST_EXPR_CALL:
            c->as.call.callee = node->as.call.callee ? strdup(node->as.call.callee) : NULL;
            bs_ast_list_init(&c->as.call.args);
            for (size_t i = 0; i < node->as.call.args.count; i++) {
                bs_ast_list_append(&c->as.call.args, substitute_ast_params(node->as.call.args.items[i], param_count, param_names, arg_nodes));
            }
            break;
        case AST_STMT_VAR_DECL:
            c->as.var_decl.name = node->as.var_decl.name ? strdup(node->as.var_decl.name) : NULL;
            c->as.var_decl.is_const = node->as.var_decl.is_const;
            c->as.var_decl.initializer = substitute_ast_params(node->as.var_decl.initializer, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_EXPR:
            c->as.expr_stmt.expr = substitute_ast_params(node->as.expr_stmt.expr, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_BLOCK:
            bs_ast_list_init(&c->as.block.statements);
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                bs_ast_list_append(&c->as.block.statements, substitute_ast_params(node->as.block.statements.items[i], param_count, param_names, arg_nodes));
            }
            break;
        case AST_STMT_IF:
            c->as.if_stmt.condition = substitute_ast_params(node->as.if_stmt.condition, param_count, param_names, arg_nodes);
            c->as.if_stmt.then_branch = substitute_ast_params(node->as.if_stmt.then_branch, param_count, param_names, arg_nodes);
            c->as.if_stmt.else_branch = substitute_ast_params(node->as.if_stmt.else_branch, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_WHILE:
            c->as.while_stmt.condition = substitute_ast_params(node->as.while_stmt.condition, param_count, param_names, arg_nodes);
            c->as.while_stmt.body = substitute_ast_params(node->as.while_stmt.body, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_FOR:
            c->as.for_stmt.init = substitute_ast_params(node->as.for_stmt.init, param_count, param_names, arg_nodes);
            c->as.for_stmt.condition = substitute_ast_params(node->as.for_stmt.condition, param_count, param_names, arg_nodes);
            c->as.for_stmt.update = substitute_ast_params(node->as.for_stmt.update, param_count, param_names, arg_nodes);
            c->as.for_stmt.body = substitute_ast_params(node->as.for_stmt.body, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_RETURN:
            c->as.return_stmt.value = substitute_ast_params(node->as.return_stmt.value, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        case AST_STMT_PRINT:
            c->as.print_stmt.expr = substitute_ast_params(node->as.print_stmt.expr, param_count, param_names, arg_nodes);
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
            c->as.func_decl.body = substitute_ast_params(node->as.func_decl.body, param_count, param_names, arg_nodes);
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
            c->as.extension_def.body = substitute_ast_params(node->as.extension_def.body, param_count, param_names, arg_nodes);
            break;
        case AST_STMT_CUSTOM_CALL:
            c->as.custom_call.command_name = node->as.custom_call.command_name ? strdup(node->as.custom_call.command_name) : NULL;
            bs_ast_list_init(&c->as.custom_call.args);
            for (size_t i = 0; i < node->as.custom_call.args.count; i++) {
                bs_ast_list_append(&c->as.custom_call.args, substitute_ast_params(node->as.custom_call.args.items[i], param_count, param_names, arg_nodes));
            }
            c->as.custom_call.body_block = substitute_ast_params(node->as.custom_call.body_block, param_count, param_names, arg_nodes);
            break;
    }
    return c;
}

ASTNode* bs_ast_expand_macro(BsCommand *macro_cmd, ASTNodeList *args, ASTNode *body_block) {
    if (!macro_cmd || !macro_cmd->body) return NULL;

    size_t total_args = (args ? args->count : 0) + (body_block ? 1 : 0);
    ASTNode **arg_array = NULL;
    if (total_args > 0) {
        arg_array = (ASTNode**)calloc(total_args, sizeof(ASTNode*));
        size_t idx = 0;
        if (args) {
            for (size_t i = 0; i < args->count; i++) {
                arg_array[idx++] = args->items[i];
            }
        }
        if (body_block) {
            arg_array[idx++] = body_block;
        }
    }

    ASTNode *expanded = substitute_ast_params(macro_cmd->body, macro_cmd->parameter_count, macro_cmd->param_names, arg_array);
    if (arg_array) free(arg_array);
    return expanded;
}

ASTNode* bs_ast_rewrite(BsAstRewriter *rewriter, ASTNode *node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_STMT_MACRO_DEF: {
            /* Register macro in registry */
            bs_register_macro(rewriter->registry,
                              node->as.extension_def.name,
                              node->as.extension_def.param_count,
                              node->as.extension_def.param_names,
                              node->as.extension_def.body);
            return node;
        }
        case AST_STMT_COMMAND_DEF: {
            /* Register behavioral AST command in registry */
            bs_register_ast_command(rewriter->registry,
                                    node->as.extension_def.name,
                                    node->as.extension_def.param_count,
                                    node->as.extension_def.param_names,
                                    node->as.extension_def.body);
            return node;
        }
        case AST_STMT_CUSTOM_CALL: {
            const char *name = node->as.custom_call.command_name;
            BsCommand *cmd = bs_command_registry_lookup(rewriter->registry, name);
            if (cmd && cmd->kind == COMMAND_MACRO) {
                /* Perform macro expansion */
                ASTNode *expanded = bs_ast_expand_macro(cmd, &node->as.custom_call.args, node->as.custom_call.body_block);
                bs_ast_free(node);
                /* Recursively rewrite the expanded AST */
                return bs_ast_rewrite(rewriter, expanded);
            }
            /* If it's an AST or Native command, rewrite its children */
            for (size_t i = 0; i < node->as.custom_call.args.count; i++) {
                node->as.custom_call.args.items[i] = bs_ast_rewrite(rewriter, node->as.custom_call.args.items[i]);
            }
            if (node->as.custom_call.body_block) {
                node->as.custom_call.body_block = bs_ast_rewrite(rewriter, node->as.custom_call.body_block);
            }
            return node;
        }
        case AST_EXPR_CALL: {
            /* Check if a call matches a macro */
            const char *callee = node->as.call.callee;
            BsCommand *cmd = bs_command_registry_lookup(rewriter->registry, callee);
            if (cmd && cmd->kind == COMMAND_MACRO) {
                ASTNode *expanded = bs_ast_expand_macro(cmd, &node->as.call.args, NULL);
                bs_ast_free(node);
                return bs_ast_rewrite(rewriter, expanded);
            }
            for (size_t i = 0; i < node->as.call.args.count; i++) {
                node->as.call.args.items[i] = bs_ast_rewrite(rewriter, node->as.call.args.items[i]);
            }
            return node;
        }
        case AST_STMT_BLOCK: {
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                node->as.block.statements.items[i] = bs_ast_rewrite(rewriter, node->as.block.statements.items[i]);
            }
            return node;
        }
        case AST_STMT_EXPR: {
            node->as.expr_stmt.expr = bs_ast_rewrite(rewriter, node->as.expr_stmt.expr);
            return node;
        }
        case AST_STMT_IF: {
            node->as.if_stmt.condition = bs_ast_rewrite(rewriter, node->as.if_stmt.condition);
            node->as.if_stmt.then_branch = bs_ast_rewrite(rewriter, node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch) {
                node->as.if_stmt.else_branch = bs_ast_rewrite(rewriter, node->as.if_stmt.else_branch);
            }
            return node;
        }
        case AST_STMT_WHILE: {
            node->as.while_stmt.condition = bs_ast_rewrite(rewriter, node->as.while_stmt.condition);
            node->as.while_stmt.body = bs_ast_rewrite(rewriter, node->as.while_stmt.body);
            return node;
        }
        case AST_STMT_FOR: {
            if (node->as.for_stmt.init) node->as.for_stmt.init = bs_ast_rewrite(rewriter, node->as.for_stmt.init);
            if (node->as.for_stmt.condition) node->as.for_stmt.condition = bs_ast_rewrite(rewriter, node->as.for_stmt.condition);
            if (node->as.for_stmt.update) node->as.for_stmt.update = bs_ast_rewrite(rewriter, node->as.for_stmt.update);
            node->as.for_stmt.body = bs_ast_rewrite(rewriter, node->as.for_stmt.body);
            return node;
        }
        case AST_STMT_VAR_DECL: {
            if (node->as.var_decl.initializer) {
                node->as.var_decl.initializer = bs_ast_rewrite(rewriter, node->as.var_decl.initializer);
            }
            return node;
        }
        case AST_STMT_PRINT: {
            if (node->as.print_stmt.expr) {
                node->as.print_stmt.expr = bs_ast_rewrite(rewriter, node->as.print_stmt.expr);
            }
            return node;
        }
        case AST_STMT_FUNC_DECL: {
            if (node->as.func_decl.body) {
                node->as.func_decl.body = bs_ast_rewrite(rewriter, node->as.func_decl.body);
            }
            return node;
        }
        case AST_EXPR_UNARY: {
            node->as.unary.operand = bs_ast_rewrite(rewriter, node->as.unary.operand);
            return node;
        }
        case AST_EXPR_BINARY: {
            node->as.binary.left = bs_ast_rewrite(rewriter, node->as.binary.left);
            node->as.binary.right = bs_ast_rewrite(rewriter, node->as.binary.right);
            return node;
        }
        case AST_EXPR_ASSIGN: {
            node->as.assign.value = bs_ast_rewrite(rewriter, node->as.assign.value);
            return node;
        }
        case AST_STMT_RETURN: {
            if (node->as.return_stmt.value) {
                node->as.return_stmt.value = bs_ast_rewrite(rewriter, node->as.return_stmt.value);
            }
            return node;
        }
        default:
            return node;
    }
}
