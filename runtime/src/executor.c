/* executor.c - Simple AST execution dispatcher for HOSC runtime */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "token.h"

#define MAX_VARS 128

typedef enum {
    RV_INT = 0,
    RV_FLOAT,
    RV_STRING
} RuntimeValueType;

typedef struct {
    RuntimeValueType type;
    int int_value;
    double float_value;
    char *string_value;
} RuntimeValue;

typedef struct {
    char *name;
    RuntimeValue value;
} Var;

typedef struct {
    Var vars[MAX_VARS];
    size_t var_count;
    ASTNode *program;
    int returning;
    RuntimeValue return_value;
    int breaking;
    int continuing;
} RuntimeContext;

static RuntimeValue rv_int(int v) {
    RuntimeValue out;
    memset(&out, 0, sizeof(out));
    out.type = RV_INT;
    out.int_value = v;
    return out;
}

static int rv_as_int(const RuntimeValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case RV_FLOAT: return (int)v->float_value;
        case RV_STRING: return 0;
        case RV_INT:
        default: return v->int_value;
    }
}

static RuntimeValue rv_float(double v) {
    RuntimeValue out;
    memset(&out, 0, sizeof(out));
    out.type = RV_FLOAT;
    out.float_value = v;
    return out;
}

static RuntimeValue rv_string_dup(const char *s) {
    RuntimeValue out;
    memset(&out, 0, sizeof(out));
    out.type = RV_STRING;
    out.string_value = strdup(s ? s : "");
    if (!out.string_value) {
        out.type = RV_INT;
        out.int_value = 0;
    }
    return out;
}

static void rv_free(RuntimeValue *v) {
    if (!v) return;
    if (v->type == RV_STRING) {
        free(v->string_value);
        v->string_value = NULL;
    }
    v->type = RV_INT;
    v->int_value = 0;
    v->float_value = 0.0;
}

static int rv_copy(RuntimeValue *dst, const RuntimeValue *src) {
    if (!dst || !src) return 0;
    rv_free(dst);
    dst->type = src->type;
    dst->int_value = src->int_value;
    dst->float_value = src->float_value;
    dst->string_value = NULL;

    if (src->type == RV_STRING) {
        dst->string_value = strdup(src->string_value ? src->string_value : "");
        if (!dst->string_value) {
            dst->type = RV_INT;
            dst->int_value = 0;
            return 0;
        }
    }
    return 1;
}

static void clear_vars(RuntimeContext *ctx) {
    size_t i;
    if (!ctx) return;
    for (i = 0; i < ctx->var_count; i++) {
        free(ctx->vars[i].name);
        ctx->vars[i].name = NULL;
        rv_free(&ctx->vars[i].value);
    }
    ctx->var_count = 0;
}

static Var *find_var(RuntimeContext *ctx, const char *name) {
    size_t i;
    if (!ctx || !name) return NULL;
    for (i = 0; i < ctx->var_count; i++) {
        if (ctx->vars[i].name && strcmp(ctx->vars[i].name, name) == 0) return &ctx->vars[i];
    }
    return NULL;
}

static void unset_var(RuntimeContext *ctx, const char *name) {
    size_t i;
    if (!ctx || !name) return;

    for (i = 0; i < ctx->var_count; i++) {
        if (ctx->vars[i].name && strcmp(ctx->vars[i].name, name) == 0) {
            size_t j;
            free(ctx->vars[i].name);
            rv_free(&ctx->vars[i].value);
            for (j = i; j + 1 < ctx->var_count; j++) {
                ctx->vars[j] = ctx->vars[j + 1];
            }
            memset(&ctx->vars[ctx->var_count - 1], 0, sizeof(ctx->vars[ctx->var_count - 1]));
            ctx->var_count--;
            return;
        }
    }
}

static int set_var_value(RuntimeContext *ctx, const char *name, const RuntimeValue *value) {
    Var *existing;

    if (!ctx || !name || !value) return 0;

    existing = find_var(ctx, name);
    if (existing) {
        return rv_copy(&existing->value, value);
    }

    if (ctx->var_count >= MAX_VARS) {
        fprintf(stderr, "Runtime warning: variable table full, cannot set '%s'\n", name);
        return 0;
    }

    ctx->vars[ctx->var_count].name = strdup(name);
    if (!ctx->vars[ctx->var_count].name) {
        fprintf(stderr, "Runtime error: out of memory while storing variable name\n");
        return 0;
    }
    memset(&ctx->vars[ctx->var_count].value, 0, sizeof(ctx->vars[ctx->var_count].value));

    if (!rv_copy(&ctx->vars[ctx->var_count].value, value)) {
        free(ctx->vars[ctx->var_count].name);
        ctx->vars[ctx->var_count].name = NULL;
        return 0;
    }

    ctx->var_count++;
    return 1;
}

static ASTNode *find_function(RuntimeContext *ctx, const char *name) {
    ASTNodeList *cur;

    if (!ctx || !ctx->program || ctx->program->type != AST_PROGRAM || !name) return NULL;

    cur = ctx->program->data.program.declarations;
    while (cur) {
        ASTNode *decl = cur->node;
        if (decl && decl->type == AST_FUNCTION && decl->data.function.name && strcmp(decl->data.function.name, name) == 0) {
            return decl;
        }
        cur = cur->next;
    }

    return NULL;
}

static int eval_expr(RuntimeContext *ctx, ASTNode *expr);
static RuntimeValue eval_expr_value(RuntimeContext *ctx, ASTNode *expr);
static void exec_statement(RuntimeContext *ctx, ASTNode *stmt);

static void exec_block(RuntimeContext *ctx, ASTNode *block) {
    ASTNodeList *cur;
    if (!ctx || !block || block->type != AST_BLOCK) return;

    cur = block->data.block.statements;
    while (cur && !ctx->returning && !ctx->breaking && !ctx->continuing) {
        exec_statement(ctx, cur->node);
        cur = cur->next;
    }
}

static RuntimeValue exec_call_value(RuntimeContext *ctx, ASTNode *fn, ASTNodeList *args) {
    RuntimeValue ret = rv_int(0);
    int old_returning;
    RuntimeValue old_return_value = rv_int(0);
    int old_breaking;
    int old_continuing;
    size_t param_count;
    RuntimeValue *arg_values = NULL;
    int *had_prev = NULL;
    RuntimeValue *prev_values = NULL;
    size_t i;
    ASTNodeList *arg_node;

    if (!ctx || !fn || fn->type != AST_FUNCTION) return ret;

    old_returning = ctx->returning;
    old_breaking = ctx->breaking;
    old_continuing = ctx->continuing;

    if (!rv_copy(&old_return_value, &ctx->return_value)) {
        return ret;
    }

    param_count = fn->data.function.param_count;
    if (param_count > 0) {
        arg_values = (RuntimeValue *)calloc(param_count, sizeof(RuntimeValue));
        had_prev = (int *)calloc(param_count, sizeof(int));
        prev_values = (RuntimeValue *)calloc(param_count, sizeof(RuntimeValue));
        if (!arg_values || !had_prev || !prev_values) {
            free(arg_values);
            free(had_prev);
            free(prev_values);
            rv_free(&old_return_value);
            fprintf(stderr, "Runtime error: out of memory while preparing function call\n");
            return rv_int(0);
        }
    }

    arg_node = args;
    for (i = 0; i < param_count; i++) {
        if (arg_node) {
            arg_values[i] = eval_expr_value(ctx, arg_node->node);
            arg_node = arg_node->next;
        } else {
            arg_values[i] = rv_int(0);
        }
    }

    for (i = 0; i < param_count; i++) {
        const char *param = fn->data.function.params ? fn->data.function.params[i] : NULL;
        Var *old_var;
        if (!param) continue;

        old_var = find_var(ctx, param);
        had_prev[i] = old_var ? 1 : 0;
        if (old_var && !rv_copy(&prev_values[i], &old_var->value)) {
            goto call_cleanup;
        }

        if (!set_var_value(ctx, param, &arg_values[i])) {
            goto call_cleanup;
        }
    }

    ctx->returning = 0;
    rv_free(&ctx->return_value);
    ctx->return_value = rv_int(0);
    ctx->breaking = 0;
    ctx->continuing = 0;
    exec_block(ctx, fn->data.function.body);

    if (ctx->returning) {
        if (!rv_copy(&ret, &ctx->return_value)) {
            ret = rv_int(0);
        }
    }

call_cleanup:
    for (i = 0; i < param_count; i++) {
        const char *param = fn->data.function.params ? fn->data.function.params[i] : NULL;
        if (!param) continue;

        if (had_prev[i]) {
            set_var_value(ctx, param, &prev_values[i]);
        } else {
            unset_var(ctx, param);
        }
    }

    ctx->returning = old_returning;
    rv_free(&ctx->return_value);
    rv_copy(&ctx->return_value, &old_return_value);
    ctx->breaking = old_breaking;
    ctx->continuing = old_continuing;

    for (i = 0; i < param_count; i++) {
        rv_free(&arg_values[i]);
        rv_free(&prev_values[i]);
    }
    free(arg_values);
    free(had_prev);
    free(prev_values);
    rv_free(&old_return_value);

    return ret;
}

static int exec_call(RuntimeContext *ctx, ASTNode *fn, ASTNodeList *args) {
    RuntimeValue ret = exec_call_value(ctx, fn, args);
    int out = rv_as_int(&ret);
    rv_free(&ret);
    return out;
}

static int eval_expr(RuntimeContext *ctx, ASTNode *expr) {
    if (!ctx || !expr) return 0;

    switch (expr->type) {
        case AST_NUMBER:
            return expr->data.number.value;

        case AST_BOOL:
            return expr->data.boolean.value ? 1 : 0;

        case AST_FLOAT:
            return (int)expr->data.fnumber.value;

        case AST_IDENTIFIER: {
            Var *v = find_var(ctx, expr->data.identifier.name);
            if (!v) return 0;
            switch (v->value.type) {
                case RV_FLOAT: return (int)v->value.float_value;
                case RV_STRING: return 0;
                case RV_INT:
                default: return v->value.int_value;
            }
        }

        case AST_UNARY_OP: {
            int value = eval_expr(ctx, expr->data.unary_op.operand);
            switch (expr->data.unary_op.op) {
                case TOKEN_MINUS: return -value;
                case TOKEN_BANG: return !value;
                default: return 0;
            }
        }

        case AST_BINARY_OP: {
            int left = eval_expr(ctx, expr->data.binary_op.left);
            int right = eval_expr(ctx, expr->data.binary_op.right);

            switch (expr->data.binary_op.op) {
                case TOKEN_PLUS: return left + right;
                case TOKEN_MINUS: return left - right;
                case TOKEN_STAR: return left * right;
                case TOKEN_SLASH: return right != 0 ? left / right : 0;
                case TOKEN_PERCENT: return right != 0 ? left % right : 0;
                case TOKEN_AND: return (left != 0) && (right != 0);
                case TOKEN_OR: return (left != 0) || (right != 0);
                case TOKEN_EQUAL_EQUAL: return left == right;
                case TOKEN_BANG_EQUAL: return left != right;
                case TOKEN_LESS: return left < right;
                case TOKEN_LESS_EQUAL: return left <= right;
                case TOKEN_GREATER: return left > right;
                case TOKEN_GREATER_EQUAL: return left >= right;
                default: return 0;
            }
        }

        case AST_CALL_EXPR: {
            ASTNode *fn = find_function(ctx, expr->data.call_expr.callee);
            if (!fn) {
                fprintf(stderr, "Runtime warning: function not found: %s\n", expr->data.call_expr.callee ? expr->data.call_expr.callee : "<null>");
                return 0;
            }
            return exec_call(ctx, fn, expr->data.call_expr.arguments);
        }

        default:
            return 0;
    }
}

static RuntimeValue eval_expr_value(RuntimeContext *ctx, ASTNode *expr) {
    RuntimeValue out;
    if (!ctx || !expr) return rv_int(0);

    switch (expr->type) {
        case AST_FLOAT:
            return rv_float(expr->data.fnumber.value);
        case AST_STRING:
            return rv_string_dup(expr->data.string_lit.value);
        case AST_IDENTIFIER: {
            Var *v = find_var(ctx, expr->data.identifier.name);
            if (!v) return rv_int(0);
            out = rv_int(0);
            if (!rv_copy(&out, &v->value)) return rv_int(0);
            return out;
        }
        case AST_CALL_EXPR: {
            ASTNode *fn = find_function(ctx, expr->data.call_expr.callee);
            if (!fn) {
                fprintf(stderr, "Runtime warning: function not found: %s\n", expr->data.call_expr.callee ? expr->data.call_expr.callee : "<null>");
                return rv_int(0);
            }
            return exec_call_value(ctx, fn, expr->data.call_expr.arguments);
        }
        default:
            return rv_int(eval_expr(ctx, expr));
    }
}

static void exec_statement(RuntimeContext *ctx, ASTNode *stmt) {
    if (!ctx || !stmt || ctx->returning) return;

    switch (stmt->type) {
        case AST_BLOCK:
            exec_block(ctx, stmt);
            break;

        case AST_VARIABLE_DECLARATION: {
            RuntimeValue value = eval_expr_value(ctx, stmt->data.variable_declaration.value);
            set_var_value(ctx, stmt->data.variable_declaration.identifier, &value);
            rv_free(&value);
            break;
        }

        case AST_ASSIGNMENT: {
            RuntimeValue value = eval_expr_value(ctx, stmt->data.assignment.value);
            set_var_value(ctx, stmt->data.assignment.identifier, &value);
            rv_free(&value);
            break;
        }

        case AST_PRINT_STATEMENT: {
            RuntimeValue value = eval_expr_value(ctx, stmt->data.print_statement.expression);
            switch (value.type) {
                case RV_FLOAT:
                    printf("%f\n", value.float_value);
                    break;
                case RV_STRING:
                    printf("%s\n", value.string_value ? value.string_value : "");
                    break;
                case RV_INT:
                default:
                    printf("%d\n", value.int_value);
                    break;
            }
            rv_free(&value);
            break;
        }

        case AST_EXPR_STATEMENT:
            (void)eval_expr(ctx, stmt->data.expr_stmt.expression);
            break;

        case AST_IF:
            if (eval_expr(ctx, stmt->data.if_stmt.condition)) {
                exec_statement(ctx, stmt->data.if_stmt.then_branch);
            } else if (stmt->data.if_stmt.else_branch) {
                exec_statement(ctx, stmt->data.if_stmt.else_branch);
            }
            break;

        case AST_WHILE:
            while (!ctx->returning && eval_expr(ctx, stmt->data.while_stmt.condition)) {
                exec_statement(ctx, stmt->data.while_stmt.body);
                if (ctx->returning) break;
                if (ctx->breaking) {
                    ctx->breaking = 0;
                    break;
                }
                if (ctx->continuing) {
                    ctx->continuing = 0;
                    continue;
                }
            }
            break;

        case AST_FOR:
            if (stmt->data.for_stmt.init) {
                exec_statement(ctx, stmt->data.for_stmt.init);
            }
            while (!ctx->returning) {
                int cond_value = stmt->data.for_stmt.condition ? eval_expr(ctx, stmt->data.for_stmt.condition) : 1;
                if (!cond_value) break;

                exec_statement(ctx, stmt->data.for_stmt.body);
                if (ctx->returning) break;
                if (ctx->breaking) {
                    ctx->breaking = 0;
                    break;
                }

                if (stmt->data.for_stmt.update) {
                    exec_statement(ctx, stmt->data.for_stmt.update);
                    if (ctx->returning) break;
                    if (ctx->breaking) {
                        ctx->breaking = 0;
                        break;
                    }
                }

                if (ctx->continuing) {
                    ctx->continuing = 0;
                }
            }
            break;

        case AST_RETURN:
            rv_free(&ctx->return_value);
            if (stmt->data.return_stmt.value) {
                RuntimeValue value = eval_expr_value(ctx, stmt->data.return_stmt.value);
                if (!rv_copy(&ctx->return_value, &value)) {
                    ctx->return_value = rv_int(0);
                }
                rv_free(&value);
            } else {
                ctx->return_value = rv_int(0);
            }
            ctx->returning = 1;
            break;

        case AST_BREAK:
            ctx->breaking = 1;
            break;

        case AST_CONTINUE:
            ctx->continuing = 1;
            break;

        default:
            break;
    }
}

void runtime_execute(ASTNode *ast) {
    RuntimeContext ctx;
    ASTNode *main_fn = NULL;

    if (!ast) return;

    memset(&ctx, 0, sizeof(ctx));
    ctx.return_value = rv_int(0);

    clear_vars(&ctx);
    ctx.returning = 0;
    rv_free(&ctx.return_value);
    ctx.return_value = rv_int(0);
    ctx.breaking = 0;
    ctx.continuing = 0;

    if (ast->type == AST_PROGRAM) {
        ASTNodeList *cur;
        ctx.program = ast;

        cur = ast->data.program.declarations;
        while (cur) {
            ASTNode *decl = cur->node;
            if (decl && decl->type == AST_FUNCTION && decl->data.function.name && strcmp(decl->data.function.name, "main") == 0) {
                main_fn = decl;
                break;
            }
            cur = cur->next;
        }

        if (main_fn) {
            (void)exec_call(&ctx, main_fn, NULL);
        } else {
            cur = ast->data.program.declarations;
            while (cur) {
                exec_statement(&ctx, cur->node);
                cur = cur->next;
            }
        }
    } else {
        ctx.program = NULL;
        exec_statement(&ctx, ast);
    }

    clear_vars(&ctx);
    ctx.program = NULL;
    ctx.returning = 0;
    rv_free(&ctx.return_value);
    ctx.return_value = rv_int(0);
    ctx.breaking = 0;
    ctx.continuing = 0;
}
