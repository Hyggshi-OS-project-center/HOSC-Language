/*
 * File: compiler/src/sema/type_checker.c
 * Purpose: HOSC Semantic Analysis and Type Checker implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hosc_type_checker.h"
#include "hosc_symbol_table.h"
#include "ast.h"

typedef struct SemaContext {
    SymbolTable *global_scope;
    SymbolTable *current_scope;
    HDiagnosticBag *diagnostics;
    const char *file_path;
    bool has_errors;
} SemaContext;

static HoscSourceSpan node_span(ASTNode *node) {
    int line = (node && node->line > 0) ? node->line : 1;
    int col = (node && node->column > 0) ? node->column : 1;
    int end_col = (node && node->end_column > col) ? node->end_column : col + 1;
    return (HoscSourceSpan){line, col, line, end_col};
}

static const char* token_op_to_string(TokenType op) {
    switch (op) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_PERCENT: return "%";
        case TOKEN_EQUAL_EQUAL: return "==";
        case TOKEN_BANG_EQUAL: return "!=";
        case TOKEN_LESS: return "<";
        case TOKEN_LESS_EQUAL: return "<=";
        case TOKEN_GREATER: return ">";
        case TOKEN_GREATER_EQUAL: return ">=";
        default: return "op";
    }
}

static HoscType check_expression(SemaContext *ctx, ASTNode *node);
static void check_statement(SemaContext *ctx, ASTNode *node);

static HoscType check_expression(SemaContext *ctx, ASTNode *node) {
    if (!node) return TYPE_VOID;

    switch (node->type) {
        case AST_NUMBER:
            return TYPE_INT;
        case AST_FLOAT:
            return TYPE_FLOAT;
        case AST_STRING:
            return TYPE_STRING;
        case AST_BOOL:
            return TYPE_BOOL;

        case AST_IDENTIFIER: {
            const char *name = node->data.identifier.name;
            Symbol *sym = hosc_symbol_table_lookup(ctx->current_scope, name);
            if (!sym) {
                const char *sugg = hosc_symbol_table_find_closest(ctx->current_scope, name);
                char msg[256];
                if (sugg) {
                    snprintf(msg, sizeof(msg), "identifier '%s' is undeclared; did you mean '%s'?", name, sugg);
                } else {
                    snprintf(msg, sizeof(msg), "identifier '%s' is undeclared", name);
                }
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H201", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
                return TYPE_ERROR;
            }
            return sym->type;
        }

        case AST_BINARY_OP: {
            HoscType left_t = check_expression(ctx, node->data.binary_op.left);
            HoscType right_t = check_expression(ctx, node->data.binary_op.right);
            TokenType op = node->data.binary_op.op;

            if (left_t == TYPE_ERROR || right_t == TYPE_ERROR) {
                return TYPE_ERROR;
            }

            if (op == TOKEN_PLUS) {
                if (left_t == TYPE_STRING || right_t == TYPE_STRING) return TYPE_STRING;
                if (left_t == TYPE_FLOAT || right_t == TYPE_FLOAT) return TYPE_FLOAT;
                return TYPE_INT;
            }

            if (op == TOKEN_MINUS || op == TOKEN_STAR || op == TOKEN_SLASH || op == TOKEN_PERCENT) {
                if (left_t == TYPE_STRING || right_t == TYPE_STRING || left_t == TYPE_BOOL || right_t == TYPE_BOOL) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "binary operator '%s' cannot be applied to types '%s' and '%s'",
                        token_op_to_string(op), hosc_type_to_string(left_t), hosc_type_to_string(right_t));
                    hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H204", ctx->file_path, node_span(node), msg);
                    ctx->has_errors = true;
                    return TYPE_ERROR;
                }
                if (left_t == TYPE_FLOAT || right_t == TYPE_FLOAT) return TYPE_FLOAT;
                return TYPE_INT;
            }

            if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL ||
                op == TOKEN_LESS || op == TOKEN_LESS_EQUAL ||
                op == TOKEN_GREATER || op == TOKEN_GREATER_EQUAL) {
                return TYPE_BOOL;
            }

            return TYPE_INT;
        }

        case AST_CALL_EXPR: {
            const char *callee = node->data.call_expr.callee;
            Symbol *sym = hosc_symbol_table_lookup(ctx->current_scope, callee);
            ASTNodeList *arg;
            size_t arg_count = 0;

            for (arg = node->data.call_expr.arguments; arg != NULL; arg = arg->next) {
                check_expression(ctx, arg->node);
                arg_count++;
            }

            if (!sym) {
                const char *sugg = hosc_symbol_table_find_closest(ctx->current_scope, callee);
                char msg[256];
                if (sugg) {
                    snprintf(msg, sizeof(msg), "unknown function '%s'; did you mean '%s'?", callee, sugg);
                } else {
                    snprintf(msg, sizeof(msg), "unknown function '%s'", callee);
                }
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H205", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
                return TYPE_ERROR;
            }

            if (sym->kind != SYMBOL_FUNC && sym->kind != SYMBOL_BUILTIN) {
                char msg[256];
                snprintf(msg, sizeof(msg), "'%s' is not a function", callee);
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H205", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
                return TYPE_ERROR;
            }

            if (sym->kind == SYMBOL_FUNC && sym->param_count != arg_count) {
                char msg[256];
                snprintf(msg, sizeof(msg), "function '%s' expects %zu arguments, but %zu were provided",
                    callee, sym->param_count, arg_count);
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H206", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
                return TYPE_ERROR;
            }

            return sym->type;
        }

        default:
            return TYPE_ANY;
    }
}

static void check_statement(SemaContext *ctx, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_BLOCK: {
            SymbolTable *block_scope = hosc_symbol_table_create(ctx->current_scope);
            ASTNodeList *stmt;
            ctx->current_scope = block_scope;
            for (stmt = node->data.block.statements; stmt != NULL; stmt = stmt->next) {
                check_statement(ctx, stmt->node);
            }
            ctx->current_scope = block_scope->parent;
            hosc_symbol_table_free(block_scope);
            break;
        }

        case AST_VARIABLE_DECLARATION: {
            const char *ident = node->data.variable_declaration.identifier;
            bool is_var = node->data.variable_declaration.is_var;
            HoscType val_t = check_expression(ctx, node->data.variable_declaration.value);

            if (hosc_symbol_table_lookup_current(ctx->current_scope, ident)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "symbol '%s' is already declared in this scope", ident);
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H202", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
            } else {
                hosc_symbol_table_define(ctx->current_scope, ident,
                    is_var ? SYMBOL_VAR : SYMBOL_CONST,
                    val_t == TYPE_ERROR ? TYPE_ANY : val_t,
                    !is_var, 0, node->line, node->column);
            }
            break;
        }

        case AST_ASSIGNMENT: {
            const char *ident = node->data.assignment.identifier;
            Symbol *sym = hosc_symbol_table_lookup(ctx->current_scope, ident);
            check_expression(ctx, node->data.assignment.value);

            if (!sym) {
                const char *sugg = hosc_symbol_table_find_closest(ctx->current_scope, ident);
                char msg[256];
                if (sugg) {
                    snprintf(msg, sizeof(msg), "identifier '%s' is undeclared; did you mean '%s'?", ident, sugg);
                } else {
                    snprintf(msg, sizeof(msg), "identifier '%s' is undeclared", ident);
                }
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H201", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
            } else if (sym->is_constant) {
                char msg[256];
                snprintf(msg, sizeof(msg), "cannot reassign to constant '%s'", ident);
                hosc_diag_bag_add(ctx->diagnostics, HOSC_DIAG_ERROR, "H203", ctx->file_path, node_span(node), msg);
                ctx->has_errors = true;
            }
            break;
        }

        case AST_PRINT_STATEMENT:
            check_expression(ctx, node->data.print_statement.expression);
            break;

        case AST_EXPR_STATEMENT:
            check_expression(ctx, node->data.expr_stmt.expression);
            break;

        case AST_IF:
            check_expression(ctx, node->data.if_stmt.condition);
            check_statement(ctx, node->data.if_stmt.then_branch);
            if (node->data.if_stmt.else_branch) {
                check_statement(ctx, node->data.if_stmt.else_branch);
            }
            break;

        case AST_WHILE:
            check_expression(ctx, node->data.while_stmt.condition);
            check_statement(ctx, node->data.while_stmt.body);
            break;

        case AST_FOR: {
            SymbolTable *for_scope = hosc_symbol_table_create(ctx->current_scope);
            ctx->current_scope = for_scope;
            if (node->data.for_stmt.init) check_statement(ctx, node->data.for_stmt.init);
            if (node->data.for_stmt.condition) check_expression(ctx, node->data.for_stmt.condition);
            if (node->data.for_stmt.update) check_statement(ctx, node->data.for_stmt.update);
            if (node->data.for_stmt.body) check_statement(ctx, node->data.for_stmt.body);
            ctx->current_scope = for_scope->parent;
            hosc_symbol_table_free(for_scope);
            break;
        }

        case AST_RETURN:
            if (node->data.return_stmt.value) {
                check_expression(ctx, node->data.return_stmt.value);
            }
            break;

        default:
            break;
    }
}

static void register_builtins(SymbolTable *scope) {
    hosc_symbol_table_define(scope, "print", SYMBOL_BUILTIN, TYPE_VOID, true, 1, 0, 0);
    hosc_symbol_table_define(scope, "prints", SYMBOL_BUILTIN, TYPE_VOID, true, 1, 0, 0);
    hosc_symbol_table_define(scope, "window.create", SYMBOL_BUILTIN, TYPE_VOID, true, 2, 0, 0);
    hosc_symbol_table_define(scope, "audio.play", SYMBOL_BUILTIN, TYPE_VOID, true, 1, 0, 0);
    hosc_symbol_table_define(scope, "text.create", SYMBOL_BUILTIN, TYPE_VOID, true, 3, 0, 0);
}

bool hosc_type_check(ASTNode *ast, HDiagnosticBag *diagnostics, const char *file_path) {
    SemaContext ctx;
    ASTNodeList *decl;

    if (!ast) return false;

    ctx.global_scope = hosc_symbol_table_create(NULL);
    ctx.current_scope = ctx.global_scope;
    ctx.diagnostics = diagnostics;
    ctx.file_path = file_path ? file_path : "";
    ctx.has_errors = false;

    register_builtins(ctx.global_scope);

    /* Pass 1: Global Symbol Registration */
    if (ast->type == AST_PROGRAM) {
        for (decl = ast->data.program.declarations; decl != NULL; decl = decl->next) {
            if (decl->node && decl->node->type == AST_FUNCTION) {
                const char *fn_name = decl->node->data.function.name;
                size_t param_cnt = decl->node->data.function.param_count;

                if (hosc_symbol_table_lookup_current(ctx.global_scope, fn_name)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "symbol '%s' is already declared in this scope", fn_name);
                    hosc_diag_bag_add(diagnostics, HOSC_DIAG_ERROR, "H202", ctx.file_path, node_span(decl->node), msg);
                    ctx.has_errors = true;
                } else {
                    hosc_symbol_table_define(ctx.global_scope, fn_name, SYMBOL_FUNC, TYPE_ANY, true, param_cnt, decl->node->line, decl->node->column);
                }
            }
        }

        /* Pass 2: Function Bodies */
        for (decl = ast->data.program.declarations; decl != NULL; decl = decl->next) {
            if (decl->node && decl->node->type == AST_FUNCTION) {
                SymbolTable *fn_scope = hosc_symbol_table_create(ctx.global_scope);
                size_t i;
                ctx.current_scope = fn_scope;

                for (i = 0; i < decl->node->data.function.param_count; i++) {
                    const char *pname = decl->node->data.function.params[i];
                    hosc_symbol_table_define(fn_scope, pname, SYMBOL_VAR, TYPE_ANY, false, 0, decl->node->line, decl->node->column);
                }

                check_statement(&ctx, decl->node->data.function.body);
                ctx.current_scope = ctx.global_scope;
                hosc_symbol_table_free(fn_scope);
            } else if (decl->node) {
                check_statement(&ctx, decl->node);
            }
        }
    }

    hosc_symbol_table_free(ctx.global_scope);
    return !ctx.has_errors;
}
