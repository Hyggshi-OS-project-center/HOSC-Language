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

static Var vars[MAX_VARS];
static size_t var_count = 0;
static ASTNode *g_program = NULL;
static int g_returning = 0;
static RuntimeValue g_return_value;
static int g_breaking = 0;
static int g_continuing = 0;

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

static void clear_vars(void) {
    size_t i;
    for (i = 0; i < var_count; i++) {
        free(vars[i].name);
        vars[i].name = NULL;
        rv_free(&vars[i].value);
    }
    var_count = 0;
}

static Var *find_var(const char *name) {
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < var_count; i++) {
        if (vars[i].name && strcmp(vars[i].name, name) == 0) return &vars[i];
    }
    return NULL;
}

static void unset_var(const char *name) {
    size_t i;
    if (!name) return;

    for (i = 0; i < var_count; i++) {
        if (vars[i].name && strcmp(vars[i].name, name) == 0) {
            size_t j;
            free(vars[i].name);
            rv_free(&vars[i].value);
            for (j = i; j + 1 < var_count; j++) {
                vars[j] = vars[j + 1];
            }
            memset(&vars[var_count - 1], 0, sizeof(vars[var_count - 1]));
            var_count--;
            return;
        }
    }
}

static int set_var_value(const char *name, const RuntimeValue *value) {
    Var *existing;

    if (!name || !value) return 0;

    existing = find_var(name);
    if (existing) {
        return rv_copy(&existing->value, value);
    }

    if (var_count >= MAX_VARS) {
        fprintf(stderr, "Runtime warning: variable table full, cannot set '%s'\n", name);
        return 0;
    }

    vars[var_count].name = strdup(name);
    if (!vars[var_count].name) {
        fprintf(stderr, "Runtime error: out of memory while storing variable name\n");
        return 0;
    }
    memset(&vars[var_count].value, 0, sizeof(vars[var_count].value));

    if (!rv_copy(&vars[var_count].value, value)) {
        free(vars[var_count].name);
        vars[var_count].name = NULL;
        return 0;
    }

    var_count++;
    return 1;
}

static ASTNode *find_function(const char *name) {
    ASTNodeList *cur;

    if (!g_program || g_program->type != AST_PROGRAM || !name) return NULL;

    cur = g_program->data.program.declarations;
    while (cur) {
        ASTNode *decl = cur->node;
        if (decl && decl->type == AST_FUNCTION && decl->data.function.name && strcmp(decl->data.function.name, name) == 0) {
            return decl;
        }
        cur = cur->next;
    }

    return NULL;
}

static int eval_expr(ASTNode *expr);
static RuntimeValue eval_expr_value(ASTNode *expr);
static void exec_statement(ASTNode *stmt);

static void exec_block(ASTNode *block) {
    ASTNodeList *cur;
    if (!block || block->type != AST_BLOCK) return;

    cur = block->data.block.statements;
    while (cur && !g_returning && !g_breaking && !g_continuing) {
        exec_statement(cur->node);
        cur = cur->next;
    }
}

static RuntimeValue exec_call_value(ASTNode *fn, ASTNodeList *args) {
    RuntimeValue ret = rv_int(0);
    int old_returning = g_returning;
    RuntimeValue old_return_value = rv_int(0);
    int old_breaking = g_breaking;
    int old_continuing = g_continuing;
    size_t param_count;
    RuntimeValue *arg_values = NULL;
    int *had_prev = NULL;
    RuntimeValue *prev_values = NULL;
    size_t i;
    ASTNodeList *arg_node;

    if (!fn || fn->type != AST_FUNCTION) return ret;

    if (!rv_copy(&old_return_value, &g_return_value)) {
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
            arg_values[i] = eval_expr_value(arg_node->node);
            arg_node = arg_node->next;
        } else {
            arg_values[i] = rv_int(0);
        }
    }

    for (i = 0; i < param_count; i++) {
        const char *param = fn->data.function.params ? fn->data.function.params[i] : NULL;
        Var *old_var;
        if (!param) continue;

        old_var = find_var(param);
        had_prev[i] = old_var ? 1 : 0;
        if (old_var && !rv_copy(&prev_values[i], &old_var->value)) {
            goto call_cleanup;
        }

        if (!set_var_value(param, &arg_values[i])) {
            goto call_cleanup;
        }
    }

    g_returning = 0;
    rv_free(&g_return_value);
    g_return_value = rv_int(0);
    g_breaking = 0;
    g_continuing = 0;
    exec_block(fn->data.function.body);

    if (g_returning) {
        if (!rv_copy(&ret, &g_return_value)) {
            ret = rv_int(0);
        }
    }

call_cleanup:
    for (i = 0; i < param_count; i++) {
        const char *param = fn->data.function.params ? fn->data.function.params[i] : NULL;
        if (!param) continue;

        if (had_prev[i]) {
            set_var_value(param, &prev_values[i]);
        } else {
            unset_var(param);
        }
    }

    g_returning = old_returning;
    rv_free(&g_return_value);
    rv_copy(&g_return_value, &old_return_value);
    g_breaking = old_breaking;
    g_continuing = old_continuing;

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

static int exec_call(ASTNode *fn, ASTNodeList *args) {
    RuntimeValue ret = exec_call_value(fn, args);
    int out = rv_as_int(&ret);
    rv_free(&ret);
    return out;
}

static int eval_expr(ASTNode *expr) {
    if (!expr) return 0;

    switch (expr->type) {
        case AST_NUMBER:
            return expr->data.number.value;

        case AST_BOOL:
            return expr->data.boolean.value ? 1 : 0;

        case AST_FLOAT:
            return (int)expr->data.fnumber.value;

        case AST_IDENTIFIER: {
            Var *v = find_var(expr->data.identifier.name);
            if (!v) return 0;
            switch (v->value.type) {
                case RV_FLOAT: return (int)v->value.float_value;
                case RV_STRING: return 0;
                case RV_INT:
                default: return v->value.int_value;
            }
        }

        case AST_UNARY_OP: {
            int value = eval_expr(expr->data.unary_op.operand);
            switch (expr->data.unary_op.op) {
                case TOKEN_MINUS: return -value;
                case TOKEN_BANG: return !value;
                default: return 0;
            }
        }

        case AST_BINARY_OP: {
            int left = eval_expr(expr->data.binary_op.left);
            int right = eval_expr(expr->data.binary_op.right);

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
            ASTNode *fn = find_function(expr->data.call_expr.callee);
            if (!fn) {
                fprintf(stderr, "Runtime warning: function not found: %s\n", expr->data.call_expr.callee ? expr->data.call_expr.callee : "<null>");
                return 0;
            }
            return exec_call(fn, expr->data.call_expr.arguments);
        }

        default:
            return 0;
    }
}

static RuntimeValue eval_expr_value(ASTNode *expr) {
    RuntimeValue out;
    if (!expr) return rv_int(0);

    switch (expr->type) {
        case AST_FLOAT:
            return rv_float(expr->data.fnumber.value);
        case AST_STRING:
            return rv_string_dup(expr->data.string_lit.value);
        case AST_IDENTIFIER: {
            Var *v = find_var(expr->data.identifier.name);
            if (!v) return rv_int(0);
            out = rv_int(0);
            if (!rv_copy(&out, &v->value)) return rv_int(0);
            return out;
        }
        case AST_CALL_EXPR: {
            ASTNode *fn = find_function(expr->data.call_expr.callee);
            if (!fn) {
                fprintf(stderr, "Runtime warning: function not found: %s\n", expr->data.call_expr.callee ? expr->data.call_expr.callee : "<null>");
                return rv_int(0);
            }
            return exec_call_value(fn, expr->data.call_expr.arguments);
        }
        default:
            return rv_int(eval_expr(expr));
    }
}

static void exec_statement(ASTNode *stmt) {
    if (!stmt || g_returning) return;

    switch (stmt->type) {
        case AST_BLOCK:
            exec_block(stmt);
            break;

        case AST_VARIABLE_DECLARATION: {
            RuntimeValue value = eval_expr_value(stmt->data.variable_declaration.value);
            set_var_value(stmt->data.variable_declaration.identifier, &value);
            rv_free(&value);
            break;
        }

        case AST_ASSIGNMENT: {
            RuntimeValue value = eval_expr_value(stmt->data.assignment.value);
            set_var_value(stmt->data.assignment.identifier, &value);
            rv_free(&value);
            break;
        }

        case AST_PRINT_STATEMENT: {
            RuntimeValue value = eval_expr_value(stmt->data.print_statement.expression);
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
            (void)eval_expr(stmt->data.expr_stmt.expression);
            break;

        case AST_IF:
            if (eval_expr(stmt->data.if_stmt.condition)) {
                exec_statement(stmt->data.if_stmt.then_branch);
            } else if (stmt->data.if_stmt.else_branch) {
                exec_statement(stmt->data.if_stmt.else_branch);
            }
            break;

        case AST_WHILE:
            while (!g_returning && eval_expr(stmt->data.while_stmt.condition)) {
                exec_statement(stmt->data.while_stmt.body);
                if (g_returning) break;
                if (g_breaking) {
                    g_breaking = 0;
                    break;
                }
                if (g_continuing) {
                    g_continuing = 0;
                    continue;
                }
            }
            break;

        case AST_FOR:
            if (stmt->data.for_stmt.init) {
                exec_statement(stmt->data.for_stmt.init);
            }
            while (!g_returning) {
                int cond_value = stmt->data.for_stmt.condition ? eval_expr(stmt->data.for_stmt.condition) : 1;
                if (!cond_value) break;

                exec_statement(stmt->data.for_stmt.body);
                if (g_returning) break;
                if (g_breaking) {
                    g_breaking = 0;
                    break;
                }

                if (stmt->data.for_stmt.update) {
                    exec_statement(stmt->data.for_stmt.update);
                    if (g_returning) break;
                    if (g_breaking) {
                        g_breaking = 0;
                        break;
                    }
                }

                if (g_continuing) {
                    g_continuing = 0;
                }
            }
            break;

        case AST_RETURN:
            rv_free(&g_return_value);
            if (stmt->data.return_stmt.value) {
                RuntimeValue value = eval_expr_value(stmt->data.return_stmt.value);
                if (!rv_copy(&g_return_value, &value)) {
                    g_return_value = rv_int(0);
                }
                rv_free(&value);
            } else {
                g_return_value = rv_int(0);
            }
            g_returning = 1;
            break;

        case AST_BREAK:
            g_breaking = 1;
            break;

        case AST_CONTINUE:
            g_continuing = 1;
            break;

        default:
            break;
    }
}

void runtime_execute(ASTNode *ast) {
    ASTNode *main_fn = NULL;

    if (!ast) return;

    clear_vars();
    g_returning = 0;
    rv_free(&g_return_value);
    g_return_value = rv_int(0);
    g_breaking = 0;
    g_continuing = 0;

    if (ast->type == AST_PROGRAM) {
        ASTNodeList *cur;
        g_program = ast;

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
            (void)exec_call(main_fn, NULL);
        } else {
            cur = ast->data.program.declarations;
            while (cur) {
                exec_statement(cur->node);
                cur = cur->next;
            }
        }
    } else {
        g_program = NULL;
        exec_statement(ast);
    }

    clear_vars();
    g_program = NULL;
    g_returning = 0;
    rv_free(&g_return_value);
    g_return_value = rv_int(0);
    g_breaking = 0;
    g_continuing = 0;
}
