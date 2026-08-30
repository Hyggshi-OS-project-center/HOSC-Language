/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_interpreter.c
 * Purpose: Tree-walk interpreter implementation supporting commands and functions
 */

#include "bs_interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BsEvalResult result_ok(BsValue v) {
    BsEvalResult r;
    r.status = BS_EVAL_OK;
    r.value = v;
    return r;
}

static BsEvalResult result_return(BsValue v) {
    BsEvalResult r;
    r.status = BS_EVAL_RETURN;
    r.value = v;
    return r;
}

static BsEvalResult result_error(BsRuntime *runtime, const char *msg) {
    BsEvalResult r;
    r.status = BS_EVAL_ERROR;
    r.value = bs_null();
    if (runtime) {
        runtime->runtime_error = true;
        snprintf(runtime->error_message, sizeof(runtime->error_message), "%s", msg);
    }
    return r;
}

static BsValue string_concat(BsValue a, BsValue b) {
    char buf_a[128];
    char buf_b[128];
    const char *str_a = NULL;
    const char *str_b = NULL;

    if (a.type == BS_VAL_STRING) {
        str_a = a.as.string ? a.as.string : "";
    } else if (a.type == BS_VAL_INT) {
        snprintf(buf_a, sizeof(buf_a), "%ld", (long)a.as.integer);
        str_a = buf_a;
    } else if (a.type == BS_VAL_FLOAT) {
        snprintf(buf_a, sizeof(buf_a), "%g", a.as.floating);
        str_a = buf_a;
    } else if (a.type == BS_VAL_BOOL) {
        str_a = a.as.boolean ? "true" : "false";
    } else {
        str_a = "null";
    }

    if (b.type == BS_VAL_STRING) {
        str_b = b.as.string ? b.as.string : "";
    } else if (b.type == BS_VAL_INT) {
        snprintf(buf_b, sizeof(buf_b), "%ld", (long)b.as.integer);
        str_b = buf_b;
    } else if (b.type == BS_VAL_FLOAT) {
        snprintf(buf_b, sizeof(buf_b), "%g", b.as.floating);
        str_b = buf_b;
    } else if (b.type == BS_VAL_BOOL) {
        str_b = b.as.boolean ? "true" : "false";
    } else {
        str_b = "null";
    }

    size_t len_a = strlen(str_a);
    size_t len_b = strlen(str_b);
    char *res = (char*)malloc(len_a + len_b + 1);
    memcpy(res, str_a, len_a);
    memcpy(res + len_a, str_b, len_b);
    res[len_a + len_b] = '\0';
    return bs_string_take(res);
}

BsEvalResult bs_eval_expression(BsRuntime *runtime, ASTNode *expr) {
    if (!expr) return result_ok(bs_null());

    switch (expr->type) {
        case AST_EXPR_LITERAL:
            return result_ok(bs_clone_value(expr->as.literal.value));

        case AST_EXPR_IDENTIFIER: {
            BsValue val;
            if (bs_env_get(runtime->current_env, expr->as.identifier.name, &val)) {
                return result_ok(val);
            }
            char err[128];
            snprintf(err, sizeof(err), "Undefined variable '%s' at line %d", expr->as.identifier.name, expr->line);
            return result_error(runtime, err);
        }

        case AST_EXPR_UNARY: {
            BsEvalResult r = bs_eval_expression(runtime, expr->as.unary.operand);
            if (r.status != BS_EVAL_OK) return r;

            if (expr->as.unary.op == BS_TOK_BANG) {
                bool b = !bs_is_truthy(r.value);
                bs_free_value(r.value);
                return result_ok(bs_bool(b));
            } else if (expr->as.unary.op == BS_TOK_MINUS) {
                if (r.value.type == BS_VAL_INT) {
                    r.value.as.integer = -r.value.as.integer;
                    return r;
                } else if (r.value.type == BS_VAL_FLOAT) {
                    r.value.as.floating = -r.value.as.floating;
                    return r;
                }
                bs_free_value(r.value);
                return result_error(runtime, "Unary '-' requires numeric operand");
            }
            return r;
        }

        case AST_EXPR_BINARY: {
            /* Short-circuit for logical operators */
            if (expr->as.binary.op == BS_TOK_AND_AND) {
                BsEvalResult left = bs_eval_expression(runtime, expr->as.binary.left);
                if (left.status != BS_EVAL_OK) return left;
                if (!bs_is_truthy(left.value)) {
                    bs_free_value(left.value);
                    return result_ok(bs_bool(false));
                }
                bs_free_value(left.value);
                BsEvalResult right = bs_eval_expression(runtime, expr->as.binary.right);
                if (right.status != BS_EVAL_OK) return right;
                bool res = bs_is_truthy(right.value);
                bs_free_value(right.value);
                return result_ok(bs_bool(res));
            }

            if (expr->as.binary.op == BS_TOK_OR_OR) {
                BsEvalResult left = bs_eval_expression(runtime, expr->as.binary.left);
                if (left.status != BS_EVAL_OK) return left;
                if (bs_is_truthy(left.value)) {
                    bs_free_value(left.value);
                    return result_ok(bs_bool(true));
                }
                bs_free_value(left.value);
                BsEvalResult right = bs_eval_expression(runtime, expr->as.binary.right);
                if (right.status != BS_EVAL_OK) return right;
                bool res = bs_is_truthy(right.value);
                bs_free_value(right.value);
                return result_ok(bs_bool(res));
            }

            BsEvalResult left = bs_eval_expression(runtime, expr->as.binary.left);
            if (left.status != BS_EVAL_OK) return left;
            BsEvalResult right = bs_eval_expression(runtime, expr->as.binary.right);
            if (right.status != BS_EVAL_OK) {
                bs_free_value(left.value);
                return right;
            }

            BsValue res = bs_null();

            /* String concatenation */
            if (expr->as.binary.op == BS_TOK_PLUS &&
                (left.value.type == BS_VAL_STRING || right.value.type == BS_VAL_STRING)) {
                res = string_concat(left.value, right.value);
                bs_free_value(left.value);
                bs_free_value(right.value);
                return result_ok(res);
            }

            /* Numeric operations */
            if (left.value.type == BS_VAL_INT && right.value.type == BS_VAL_INT) {
                int64_t a = left.value.as.integer;
                int64_t b = right.value.as.integer;
                switch (expr->as.binary.op) {
                    case BS_TOK_PLUS: res = bs_int(a + b); break;
                    case BS_TOK_MINUS: res = bs_int(a - b); break;
                    case BS_TOK_STAR: res = bs_int(a * b); break;
                    case BS_TOK_SLASH: res = b != 0 ? bs_int(a / b) : bs_int(0); break;
                    case BS_TOK_PERCENT: res = b != 0 ? bs_int(a % b) : bs_int(0); break;
                    case BS_TOK_EQUAL_EQUAL: res = bs_bool(a == b); break;
                    case BS_TOK_BANG_EQUAL: res = bs_bool(a != b); break;
                    case BS_TOK_LESS: res = bs_bool(a < b); break;
                    case BS_TOK_LESS_EQUAL: res = bs_bool(a <= b); break;
                    case BS_TOK_GREATER: res = bs_bool(a > b); break;
                    case BS_TOK_GREATER_EQUAL: res = bs_bool(a >= b); break;
                    default: break;
                }
            } else if ((left.value.type == BS_VAL_FLOAT || left.value.type == BS_VAL_INT) &&
                       (right.value.type == BS_VAL_FLOAT || right.value.type == BS_VAL_INT)) {
                double a = left.value.type == BS_VAL_FLOAT ? left.value.as.floating : (double)left.value.as.integer;
                double b = right.value.type == BS_VAL_FLOAT ? right.value.as.floating : (double)right.value.as.integer;
                switch (expr->as.binary.op) {
                    case BS_TOK_PLUS: res = bs_float(a + b); break;
                    case BS_TOK_MINUS: res = bs_float(a - b); break;
                    case BS_TOK_STAR: res = bs_float(a * b); break;
                    case BS_TOK_SLASH: res = b != 0.0 ? bs_float(a / b) : bs_float(0.0); break;
                    case BS_TOK_EQUAL_EQUAL: res = bs_bool(a == b); break;
                    case BS_TOK_BANG_EQUAL: res = bs_bool(a != b); break;
                    case BS_TOK_LESS: res = bs_bool(a < b); break;
                    case BS_TOK_LESS_EQUAL: res = bs_bool(a <= b); break;
                    case BS_TOK_GREATER: res = bs_bool(a > b); break;
                    case BS_TOK_GREATER_EQUAL: res = bs_bool(a >= b); break;
                    default: break;
                }
            } else if (expr->as.binary.op == BS_TOK_EQUAL_EQUAL) {
                res = bs_bool(bs_values_equal(left.value, right.value));
            } else if (expr->as.binary.op == BS_TOK_BANG_EQUAL) {
                res = bs_bool(!bs_values_equal(left.value, right.value));
            }

            bs_free_value(left.value);
            bs_free_value(right.value);
            return result_ok(res);
        }

        case AST_EXPR_ASSIGN: {
            BsEvalResult val = bs_eval_expression(runtime, expr->as.assign.value);
            if (val.status != BS_EVAL_OK) return val;

            if (!bs_env_assign(runtime->current_env, expr->as.assign.target_name, val.value)) {
                // If not found in scopes, define in current scope
                bs_env_define(runtime->current_env, expr->as.assign.target_name, val.value, false);
            }
            return val;
        }

        case AST_EXPR_CALL: {
            const char *callee = expr->as.call.callee;

            /* Check command registry first for Native or AST command */
            BsCommand *cmd = bs_command_registry_lookup(&runtime->command_registry, callee);
            if (cmd) {
                size_t argc = expr->as.call.args.count;
                BsValue *arg_vals = argc > 0 ? (BsValue*)calloc(argc, sizeof(BsValue)) : NULL;
                for (size_t i = 0; i < argc; i++) {
                    BsEvalResult arg_res = bs_eval_expression(runtime, expr->as.call.args.items[i]);
                    if (arg_res.status != BS_EVAL_OK) {
                        for (size_t j = 0; j < i; j++) bs_free_value(arg_vals[j]);
                        if (arg_vals) free(arg_vals);
                        return arg_res;
                    }
                    arg_vals[i] = arg_res.value;
                }

                BsValue ret_val = bs_null();
                if (cmd->kind == COMMAND_NATIVE && cmd->native) {
                    ret_val = cmd->native(runtime, arg_vals, argc);
                } else if (cmd->kind == COMMAND_AST && cmd->body) {
                    bs_runtime_push_scope(runtime);
                    for (size_t i = 0; i < cmd->parameter_count && i < argc; i++) {
                        bs_env_define(runtime->current_env, cmd->param_names[i], arg_vals[i], false);
                    }
                    BsEvalResult b_res = bs_eval_statement(runtime, cmd->body);
                    bs_runtime_pop_scope(runtime);
                    ret_val = (b_res.status == BS_EVAL_RETURN || b_res.status == BS_EVAL_OK) ? b_res.value : bs_null();
                }

                for (size_t i = 0; i < argc; i++) bs_free_value(arg_vals[i]);
                if (arg_vals) free(arg_vals);
                return result_ok(ret_val);
            }

            /* Check function in environment */
            BsValue fn_val;
            if (bs_env_get(runtime->current_env, callee, &fn_val) && fn_val.type == BS_VAL_FUNCTION) {
                size_t argc = expr->as.call.args.count;
                BsValue *arg_vals = argc > 0 ? (BsValue*)calloc(argc, sizeof(BsValue)) : NULL;
                for (size_t i = 0; i < argc; i++) {
                    BsEvalResult arg_res = bs_eval_expression(runtime, expr->as.call.args.items[i]);
                    if (arg_res.status != BS_EVAL_OK) {
                        for (size_t j = 0; j < i; j++) bs_free_value(arg_vals[j]);
                        if (arg_vals) free(arg_vals);
                        bs_free_value(fn_val);
                        return arg_res;
                    }
                    arg_vals[i] = arg_res.value;
                }

                bs_runtime_push_scope(runtime);
                for (size_t i = 0; i < fn_val.as.function.param_count && i < argc; i++) {
                    bs_env_define(runtime->current_env, fn_val.as.function.param_names[i], arg_vals[i], false);
                }

                BsEvalResult body_res = bs_eval_statement(runtime, fn_val.as.function.body);
                bs_runtime_pop_scope(runtime);

                for (size_t i = 0; i < argc; i++) bs_free_value(arg_vals[i]);
                if (arg_vals) free(arg_vals);
                bs_free_value(fn_val);

                BsValue ret = (body_res.status == BS_EVAL_RETURN) ? body_res.value : bs_null();
                return result_ok(ret);
            }

            char err[128];
            snprintf(err, sizeof(err), "Call to undefined function or command '%s' at line %d", callee, expr->line);
            return result_error(runtime, err);
        }

        default:
            return result_ok(bs_null());
    }
}

BsEvalResult bs_eval_statement(BsRuntime *runtime, ASTNode *stmt) {
    if (!stmt) return result_ok(bs_null());

    switch (stmt->type) {
        case AST_STMT_VAR_DECL: {
            BsValue init_val = bs_null();
            if (stmt->as.var_decl.initializer) {
                BsEvalResult r = bs_eval_expression(runtime, stmt->as.var_decl.initializer);
                if (r.status != BS_EVAL_OK) return r;
                init_val = r.value;
            }
            bs_env_define(runtime->current_env, stmt->as.var_decl.name, init_val, stmt->as.var_decl.is_const);
            bs_free_value(init_val);
            return result_ok(bs_null());
        }

        case AST_STMT_EXPR: {
            BsEvalResult r = bs_eval_expression(runtime, stmt->as.expr_stmt.expr);
            if (r.status != BS_EVAL_OK) return r;
            bs_free_value(r.value);
            return result_ok(bs_null());
        }

        case AST_STMT_BLOCK: {
            bs_runtime_push_scope(runtime);
            BsEvalResult res = result_ok(bs_null());
            for (size_t i = 0; i < stmt->as.block.statements.count; i++) {
                res = bs_eval_statement(runtime, stmt->as.block.statements.items[i]);
                if (res.status != BS_EVAL_OK) {
                    break;
                }
            }
            bs_runtime_pop_scope(runtime);
            return res;
        }

        case AST_STMT_IF: {
            BsEvalResult cond = bs_eval_expression(runtime, stmt->as.if_stmt.condition);
            if (cond.status != BS_EVAL_OK) return cond;
            bool truthy = bs_is_truthy(cond.value);
            bs_free_value(cond.value);

            if (truthy) {
                return bs_eval_statement(runtime, stmt->as.if_stmt.then_branch);
            } else if (stmt->as.if_stmt.else_branch) {
                return bs_eval_statement(runtime, stmt->as.if_stmt.else_branch);
            }
            return result_ok(bs_null());
        }

        case AST_STMT_WHILE: {
            while (true) {
                BsEvalResult cond = bs_eval_expression(runtime, stmt->as.while_stmt.condition);
                if (cond.status != BS_EVAL_OK) return cond;
                bool truthy = bs_is_truthy(cond.value);
                bs_free_value(cond.value);
                if (!truthy) break;

                BsEvalResult body_res = bs_eval_statement(runtime, stmt->as.while_stmt.body);
                if (body_res.status == BS_EVAL_BREAK) break;
                if (body_res.status == BS_EVAL_RETURN || body_res.status == BS_EVAL_ERROR) return body_res;
            }
            return result_ok(bs_null());
        }

        case AST_STMT_FOR: {
            bs_runtime_push_scope(runtime);
            if (stmt->as.for_stmt.init) {
                BsEvalResult init_res = bs_eval_statement(runtime, stmt->as.for_stmt.init);
                if (init_res.status != BS_EVAL_OK) {
                    bs_runtime_pop_scope(runtime);
                    return init_res;
                }
            }

            while (true) {
                if (stmt->as.for_stmt.condition) {
                    BsEvalResult cond = bs_eval_expression(runtime, stmt->as.for_stmt.condition);
                    if (cond.status != BS_EVAL_OK) {
                        bs_runtime_pop_scope(runtime);
                        return cond;
                    }
                    bool truthy = bs_is_truthy(cond.value);
                    bs_free_value(cond.value);
                    if (!truthy) break;
                }

                BsEvalResult body_res = bs_eval_statement(runtime, stmt->as.for_stmt.body);
                if (body_res.status == BS_EVAL_BREAK) break;
                if (body_res.status == BS_EVAL_RETURN || body_res.status == BS_EVAL_ERROR) {
                    bs_runtime_pop_scope(runtime);
                    return body_res;
                }

                if (stmt->as.for_stmt.update) {
                    BsEvalResult upd = bs_eval_expression(runtime, stmt->as.for_stmt.update);
                    if (upd.status != BS_EVAL_OK) {
                        bs_runtime_pop_scope(runtime);
                        return upd;
                    }
                    bs_free_value(upd.value);
                }
            }

            bs_runtime_pop_scope(runtime);
            return result_ok(bs_null());
        }

        case AST_STMT_RETURN: {
            BsValue val = bs_null();
            if (stmt->as.return_stmt.value) {
                BsEvalResult r = bs_eval_expression(runtime, stmt->as.return_stmt.value);
                if (r.status != BS_EVAL_OK) return r;
                val = r.value;
            }
            return result_return(val);
        }

        case AST_STMT_BREAK: {
            BsEvalResult r;
            r.status = BS_EVAL_BREAK;
            r.value = bs_null();
            return r;
        }

        case AST_STMT_CONTINUE: {
            BsEvalResult r;
            r.status = BS_EVAL_CONTINUE;
            r.value = bs_null();
            return r;
        }

        case AST_STMT_PRINT: {
            if (stmt->as.print_stmt.expr) {
                BsEvalResult r = bs_eval_expression(runtime, stmt->as.print_stmt.expr);
                if (r.status != BS_EVAL_OK) return r;
                bs_print_value(r.value);
                bs_free_value(r.value);
            }
            if (stmt->as.print_stmt.add_newline) {
                printf("\n");
            }
            return result_ok(bs_null());
        }

        case AST_STMT_FUNC_DECL: {
            BsValue fn = bs_function(stmt->as.func_decl.name,
                                     stmt->as.func_decl.param_count,
                                     stmt->as.func_decl.param_names,
                                     stmt->as.func_decl.body);
            bs_env_define(runtime->current_env, stmt->as.func_decl.name, fn, false);
            bs_free_value(fn);
            return result_ok(bs_null());
        }

        case AST_STMT_COMMAND_DEF: {
            bs_register_ast_command(&runtime->command_registry,
                                    stmt->as.extension_def.name,
                                    stmt->as.extension_def.param_count,
                                    stmt->as.extension_def.param_names,
                                    stmt->as.extension_def.body);
            return result_ok(bs_null());
        }

        case AST_STMT_MACRO_DEF: {
            bs_register_macro(&runtime->command_registry,
                              stmt->as.extension_def.name,
                              stmt->as.extension_def.param_count,
                              stmt->as.extension_def.param_names,
                              stmt->as.extension_def.body);
            return result_ok(bs_null());
        }

        case AST_STMT_CUSTOM_CALL: {
            const char *name = stmt->as.custom_call.command_name;
            BsCommand *cmd = bs_command_registry_lookup(&runtime->command_registry, name);
            if (cmd) {
                size_t argc = stmt->as.custom_call.args.count;
                BsValue *arg_vals = argc > 0 ? (BsValue*)calloc(argc, sizeof(BsValue)) : NULL;
                for (size_t i = 0; i < argc; i++) {
                    BsEvalResult arg_res = bs_eval_expression(runtime, stmt->as.custom_call.args.items[i]);
                    if (arg_res.status != BS_EVAL_OK) {
                        for (size_t j = 0; j < i; j++) bs_free_value(arg_vals[j]);
                        if (arg_vals) free(arg_vals);
                        return arg_res;
                    }
                    arg_vals[i] = arg_res.value;
                }

                if (cmd->kind == COMMAND_NATIVE && cmd->native) {
                    BsValue r = cmd->native(runtime, arg_vals, argc);
                    bs_free_value(r);
                } else if (cmd->kind == COMMAND_AST && cmd->body) {
                    bs_runtime_push_scope(runtime);
                    for (size_t i = 0; i < cmd->parameter_count && i < argc; i++) {
                        bs_env_define(runtime->current_env, cmd->param_names[i], arg_vals[i], false);
                    }
                    BsEvalResult b_res = bs_eval_statement(runtime, cmd->body);
                    bs_runtime_pop_scope(runtime);
                    if (b_res.status == BS_EVAL_RETURN) bs_free_value(b_res.value);
                }

                for (size_t i = 0; i < argc; i++) bs_free_value(arg_vals[i]);
                if (arg_vals) free(arg_vals);

                /* If there is a trailing block, execute it */
                if (stmt->as.custom_call.body_block) {
                    return bs_eval_statement(runtime, stmt->as.custom_call.body_block);
                }
                return result_ok(bs_null());
            }

            /* Also check regular function */
            BsValue fn_val;
            if (bs_env_get(runtime->current_env, name, &fn_val) && fn_val.type == BS_VAL_FUNCTION) {
                size_t argc = stmt->as.custom_call.args.count;
                BsValue *arg_vals = argc > 0 ? (BsValue*)calloc(argc, sizeof(BsValue)) : NULL;
                for (size_t i = 0; i < argc; i++) {
                    BsEvalResult arg_res = bs_eval_expression(runtime, stmt->as.custom_call.args.items[i]);
                    if (arg_res.status != BS_EVAL_OK) {
                        for (size_t j = 0; j < i; j++) bs_free_value(arg_vals[j]);
                        if (arg_vals) free(arg_vals);
                        bs_free_value(fn_val);
                        return arg_res;
                    }
                    arg_vals[i] = arg_res.value;
                }

                bs_runtime_push_scope(runtime);
                for (size_t i = 0; i < fn_val.as.function.param_count && i < argc; i++) {
                    bs_env_define(runtime->current_env, fn_val.as.function.param_names[i], arg_vals[i], false);
                }
                BsEvalResult body_res = bs_eval_statement(runtime, fn_val.as.function.body);
                bs_runtime_pop_scope(runtime);

                for (size_t i = 0; i < argc; i++) bs_free_value(arg_vals[i]);
                if (arg_vals) free(arg_vals);
                bs_free_value(fn_val);

                if (body_res.status == BS_EVAL_RETURN) bs_free_value(body_res.value);
                return result_ok(bs_null());
            }

            char err[128];
            snprintf(err, sizeof(err), "Unknown command or function '%s' at line %d", name, stmt->line);
            return result_error(runtime, err);
        }

        case AST_STMT_PACKAGE:
        case AST_STMT_IMPORT:
            return result_ok(bs_null());

        default:
            return result_ok(bs_null());
    }
}

BsEvalResult bs_eval_program(BsRuntime *runtime, ASTNode *program) {
    if (!program) return result_ok(bs_null());

    /* Execute top-level statements directly in global scope (no extra push_scope).
     * This ensures var declarations at top level land in global_env and are
     * accessible for inspection by tests and embedding code. */
    BsEvalResult r = result_ok(bs_null());
    if (program->type == AST_STMT_BLOCK) {
        for (size_t i = 0; i < program->as.block.statements.count; i++) {
            bs_free_value(r.value);
            r = bs_eval_statement(runtime, program->as.block.statements.items[i]);
            if (r.status == BS_EVAL_ERROR || r.status == BS_EVAL_RETURN) break;
        }
    } else {
        r = bs_eval_statement(runtime, program);
    }

    if (r.status == BS_EVAL_ERROR) return r;
    bs_free_value(r.value);

    /* If program defines a main() function, invoke it */
    BsValue main_fn;
    if (bs_env_get(runtime->current_env, "main", &main_fn) && main_fn.type == BS_VAL_FUNCTION) {
        bs_runtime_push_scope(runtime);
        BsEvalResult main_res = bs_eval_statement(runtime, main_fn.as.function.body);
        bs_runtime_pop_scope(runtime);
        bs_free_value(main_fn);
        return main_res;
    }

    return result_ok(bs_null());
}
