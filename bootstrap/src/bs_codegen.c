/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_codegen.c
 * Purpose: AST-to-Bytecode compiler implementation
 */

#include "bs_codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bs_compiler_init(BsCompiler *compiler, BsChunk *chunk, BsCommandRegistry *registry) {
    compiler->chunk = chunk;
    compiler->registry = registry;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->had_error = false;
    compiler->error_message[0] = '\0';
}

static void emit_byte(BsCompiler *c, uint8_t byte, int line) {
    bs_chunk_write(c->chunk, byte, line);
}

static void emit_two(BsCompiler *c, uint8_t a, uint8_t b, int line) {
    emit_byte(c, a, line);
    emit_byte(c, b, line);
}

static size_t emit_jump(BsCompiler *c, BsOpCode op, int line) {
    emit_byte(c, (uint8_t)op, line);
    emit_byte(c, 0xFF, line); /* high byte placeholder */
    emit_byte(c, 0xFF, line); /* low byte placeholder */
    return c->chunk->count - 2;
}

static void patch_jump(BsCompiler *c, size_t offset) {
    size_t jump = c->chunk->count - offset - 2;
    c->chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_loop(BsCompiler *c, size_t loop_start, int line) {
    emit_byte(c, (uint8_t)OP_LOOP, line);
    size_t offset = c->chunk->count - loop_start + 2;
    emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
    emit_byte(c, (uint8_t)(offset & 0xFF), line);
}

static uint8_t add_constant(BsCompiler *c, BsValue val, int line) {
    size_t idx = bs_chunk_add_constant(c->chunk, val);
    return (uint8_t)idx;
}

static int resolve_local(BsCompiler *c, const char *name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (strcmp(c->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void begin_scope(BsCompiler *c) {
    c->scope_depth++;
}

static void end_scope(BsCompiler *c, int line) {
    c->scope_depth--;
    while (c->local_count > 0 && c->locals[c->local_count - 1].depth > c->scope_depth) {
        emit_byte(c, OP_POP, line);
        free(c->locals[c->local_count - 1].name);
        c->locals[c->local_count - 1].name = NULL;
        c->local_count--;
    }
}

static void add_local(BsCompiler *c, const char *name) {
    if (c->local_count >= 256) return;
    c->locals[c->local_count].name = strdup(name);
    c->locals[c->local_count].depth = c->scope_depth;
    c->local_count++;
}

static bool compile_expr(BsCompiler *c, ASTNode *expr);
static bool compile_stmt(BsCompiler *c, ASTNode *stmt);

static bool compile_expr(BsCompiler *c, ASTNode *expr) {
    if (!expr) {
        return false;
    }

    switch (expr->type) {
        case AST_EXPR_LITERAL: {
            int line = expr->line;
            BsValue v = expr->as.literal.value;
            if (v.type == BS_VAL_NULL) {
                emit_byte(c, OP_NULL, line);
            } else if (v.type == BS_VAL_BOOL) {
                emit_byte(c, v.as.boolean ? OP_TRUE : OP_FALSE, line);
            } else {
                uint8_t idx = add_constant(c, v, line);
                emit_two(c, OP_CONSTANT, idx, line);
            }
            break;
        }

        case AST_EXPR_IDENTIFIER: {
            int line = expr->line;
            const char *name = expr->as.identifier.name;
            int local_idx = resolve_local(c, name);
            if (local_idx >= 0) {
                emit_two(c, OP_GET_LOCAL, (uint8_t)local_idx, line);
            } else {
                BsValue name_val = bs_string(name);
                uint8_t idx = add_constant(c, name_val, line);
                bs_free_value(name_val);
                emit_two(c, OP_GET_GLOBAL, idx, line);
            }
            break;
        }

        case AST_EXPR_UNARY: {
            int line = expr->line;
            if (!compile_expr(c, expr->as.unary.operand)) return false;
            if (expr->as.unary.op == BS_TOK_BANG) emit_byte(c, OP_NOT, line);
            else if (expr->as.unary.op == BS_TOK_MINUS) emit_byte(c, OP_NEGATE, line);
            break;
        }

        case AST_EXPR_BINARY: {
            int line = expr->line;
            BsTokenType op = expr->as.binary.op;

            /* Short-circuit: && */
            if (op == BS_TOK_AND_AND) {
                if (!compile_expr(c, expr->as.binary.left)) return false;
                size_t jump = emit_jump(c, OP_JUMP_IF_FALSE, line);
                emit_byte(c, OP_POP, line);
                if (!compile_expr(c, expr->as.binary.right)) return false;
                patch_jump(c, jump);
                break;
            }

            /* Short-circuit: || */
            if (op == BS_TOK_OR_OR) {
                if (!compile_expr(c, expr->as.binary.left)) return false;
                size_t jump_false = emit_jump(c, OP_JUMP_IF_FALSE, line);
                size_t jump_true = emit_jump(c, OP_JUMP, line);
                patch_jump(c, jump_false);
                emit_byte(c, OP_POP, line);
                if (!compile_expr(c, expr->as.binary.right)) return false;
                patch_jump(c, jump_true);
                break;
            }

            if (!compile_expr(c, expr->as.binary.left)) return false;
            if (!compile_expr(c, expr->as.binary.right)) return false;

            switch (op) {
                case BS_TOK_PLUS:           emit_byte(c, OP_ADD, line); break;
                case BS_TOK_MINUS:          emit_byte(c, OP_SUB, line); break;
                case BS_TOK_STAR:           emit_byte(c, OP_MUL, line); break;
                case BS_TOK_SLASH:          emit_byte(c, OP_DIV, line); break;
                case BS_TOK_PERCENT:        emit_byte(c, OP_MOD, line); break;
                case BS_TOK_EQUAL_EQUAL:    emit_byte(c, OP_EQUAL, line); break;
                case BS_TOK_BANG_EQUAL:     emit_byte(c, OP_NOT_EQUAL, line); break;
                case BS_TOK_LESS:           emit_byte(c, OP_LESS, line); break;
                case BS_TOK_LESS_EQUAL:     emit_byte(c, OP_LESS_EQUAL, line); break;
                case BS_TOK_GREATER:        emit_byte(c, OP_GREATER, line); break;
                case BS_TOK_GREATER_EQUAL:  emit_byte(c, OP_GREATER_EQUAL, line); break;
                default: break;
            }
            break;
        }

        case AST_EXPR_ASSIGN: {
            int line = expr->line;
            if (!compile_expr(c, expr->as.assign.value)) return false;
            const char *name = expr->as.assign.target_name;
            int local_idx = resolve_local(c, name);
            if (local_idx >= 0) {
                emit_two(c, OP_SET_LOCAL, (uint8_t)local_idx, line);
            } else {
                BsValue name_val = bs_string(name);
                uint8_t idx = add_constant(c, name_val, line);
                bs_free_value(name_val);
                emit_two(c, OP_SET_GLOBAL, idx, line);
            }
            break;
        }

        case AST_EXPR_CALL: {
            int line = expr->line;
            const char *callee = expr->as.call.callee;

            /* Check if it's a command in registry */
            BsCommand *cmd = bs_command_registry_lookup(c->registry, callee);
            if (cmd && cmd->kind == COMMAND_NATIVE) {
                size_t argc = expr->as.call.args.count;
                for (size_t i = 0; i < argc; i++) {
                    if (!compile_expr(c, expr->as.call.args.items[i])) return false;
                }
                BsValue name_val = bs_string(callee);
                uint8_t name_idx = add_constant(c, name_val, line);
                bs_free_value(name_val);
                emit_byte(c, OP_CALL_COMMAND, line);
                emit_byte(c, name_idx, line);
                emit_byte(c, (uint8_t)argc, line);
            } else {
                /* Push arguments */
                size_t argc = expr->as.call.args.count;
                for (size_t i = 0; i < argc; i++) {
                    if (!compile_expr(c, expr->as.call.args.items[i])) return false;
                }
                /* Load callee */
                int local_idx = resolve_local(c, callee);
                if (local_idx >= 0) {
                    emit_two(c, OP_GET_LOCAL, (uint8_t)local_idx, line);
                } else {
                    BsValue name_val = bs_string(callee);
                    uint8_t idx = add_constant(c, name_val, line);
                    bs_free_value(name_val);
                    emit_two(c, OP_GET_GLOBAL, idx, line);
                }
                emit_two(c, OP_CALL, (uint8_t)argc, line);
            }
            break;
        }

        default:
            break;
    }

    return !c->had_error;
}

static bool compile_stmt(BsCompiler *c, ASTNode *stmt) {
    if (!stmt) return true;

    switch (stmt->type) {
        case AST_STMT_VAR_DECL: {
            int line = stmt->line;
            if (stmt->as.var_decl.initializer) {
                if (!compile_expr(c, stmt->as.var_decl.initializer)) return false;
            } else {
                emit_byte(c, OP_NULL, line);
            }
            const char *name = stmt->as.var_decl.name;
            if (c->scope_depth > 0) {
                add_local(c, name);
            } else {
                BsValue name_val = bs_string(name);
                uint8_t idx = add_constant(c, name_val, line);
                bs_free_value(name_val);
                emit_two(c, OP_DEFINE_GLOBAL, idx, line);
            }
            break;
        }

        case AST_STMT_EXPR: {
            if (!compile_expr(c, stmt->as.expr_stmt.expr)) return false;
            emit_byte(c, OP_POP, stmt->line);
            break;
        }

        case AST_STMT_BLOCK: {
            begin_scope(c);
            for (size_t i = 0; i < stmt->as.block.statements.count; i++) {
                if (!compile_stmt(c, stmt->as.block.statements.items[i])) return false;
            }
            end_scope(c, stmt->line);
            break;
        }

        case AST_STMT_IF: {
            int line = stmt->line;
            if (!compile_expr(c, stmt->as.if_stmt.condition)) return false;
            size_t then_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);
            emit_byte(c, OP_POP, line);

            if (!compile_stmt(c, stmt->as.if_stmt.then_branch)) return false;

            if (stmt->as.if_stmt.else_branch) {
                size_t else_jump = emit_jump(c, OP_JUMP, line);
                patch_jump(c, then_jump);
                emit_byte(c, OP_POP, line);
                if (!compile_stmt(c, stmt->as.if_stmt.else_branch)) return false;
                patch_jump(c, else_jump);
            } else {
                patch_jump(c, then_jump);
                emit_byte(c, OP_POP, line);
            }
            break;
        }

        case AST_STMT_WHILE: {
            int line = stmt->line;
            size_t loop_start = c->chunk->count;

            if (!compile_expr(c, stmt->as.while_stmt.condition)) return false;
            size_t exit_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);
            emit_byte(c, OP_POP, line);

            if (!compile_stmt(c, stmt->as.while_stmt.body)) return false;

            emit_loop(c, loop_start, line);
            patch_jump(c, exit_jump);
            emit_byte(c, OP_POP, line);
            break;
        }

        case AST_STMT_PRINT: {
            int line = stmt->line;
            if (stmt->as.print_stmt.expr) {
                if (!compile_expr(c, stmt->as.print_stmt.expr)) return false;
            } else {
                emit_byte(c, OP_NULL, line);
            }
            emit_byte(c, stmt->as.print_stmt.is_raw ? OP_PRINTS : OP_PRINT, line);
            break;
        }

        case AST_STMT_RETURN: {
            int line = stmt->line;
            if (stmt->as.return_stmt.value) {
                if (!compile_expr(c, stmt->as.return_stmt.value)) return false;
            } else {
                emit_byte(c, OP_NULL, line);
            }
            emit_byte(c, OP_RETURN, line);
            break;
        }

        case AST_STMT_COMMAND_DEF:
        case AST_STMT_MACRO_DEF:
        case AST_STMT_FUNC_DECL:
        case AST_STMT_PACKAGE:
        case AST_STMT_IMPORT:
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            /* Handled by interpreter pass or skipped in bytecode */
            break;

        case AST_STMT_CUSTOM_CALL: {
            int line = stmt->line;
            const char *name = stmt->as.custom_call.command_name;
            size_t argc = stmt->as.custom_call.args.count;
            for (size_t i = 0; i < argc; i++) {
                if (!compile_expr(c, stmt->as.custom_call.args.items[i])) return false;
            }
            BsValue name_val = bs_string(name);
            uint8_t name_idx = add_constant(c, name_val, line);
            bs_free_value(name_val);
            emit_byte(c, OP_CALL_COMMAND, line);
            emit_byte(c, name_idx, line);
            emit_byte(c, (uint8_t)argc, line);
            emit_byte(c, OP_POP, line);
            break;
        }

        default:
            break;
    }

    return !c->had_error;
}

bool bs_compile_ast(BsCompiler *compiler, ASTNode *node) {
    if (!node) return false;

    if (node->type == AST_STMT_BLOCK) {
        for (size_t i = 0; i < node->as.block.statements.count; i++) {
            if (!compile_stmt(compiler, node->as.block.statements.items[i])) return false;
        }
    } else {
        if (!compile_stmt(compiler, node)) return false;
    }

    emit_byte(compiler, OP_HALT, 0);
    return !compiler->had_error;
}
