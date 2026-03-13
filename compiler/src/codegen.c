/*
 * File: compiler\src\codegen.c
 * Purpose: HOSC source file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"
#include "token.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buffer;

typedef struct VarType {
    char *name;
    const char *type_str;
    struct VarType *next;
} VarType;

static VarType *g_var_types = NULL;

static int buf_init(Buffer *b, size_t cap) {
    b->data = (char *)malloc(cap);
    if (!b->data) return 0;
    b->len = 0;
    b->cap = cap;
    b->data[0] = '\0';
    return 1;
}

static int buf_ensure(Buffer *b, size_t more) {
    size_t new_cap;
    char *nd;
    if (b->len + more + 1 <= b->cap) return 1;
    new_cap = b->cap ? b->cap * 2 : 512;
    while (new_cap < b->len + more + 1) new_cap *= 2;
    nd = (char *)realloc(b->data, new_cap);
    if (!nd) return 0;
    b->data = nd;
    b->cap = new_cap;
    return 1;
}

static int buf_appendf(Buffer *b, const char *fmt, ...) {
    va_list args;
    int needed;

    va_start(args, fmt);
    needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) return 0;
    if (!buf_ensure(b, (size_t)needed)) return 0;

    va_start(args, fmt);
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, args);
    va_end(args);

    b->len += (size_t)needed;
    return 1;
}

static void clear_var_types(void) {
    VarType *cur = g_var_types;
    while (cur) {
        VarType *next = cur->next;
        free(cur->name);
        free(cur);
        cur = next;
    }
    g_var_types = NULL;
}

static int remember_var_type(const char *name, const char *type_str) {
    VarType *cur;
    VarType *node;

    if (!name || !type_str) return 0;

    cur = g_var_types;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            cur->type_str = type_str;
            return 1;
        }
        cur = cur->next;
    }

    node = (VarType *)calloc(1, sizeof(VarType));
    if (!node) return 0;
    node->name = strdup(name);
    if (!node->name) {
        free(node);
        return 0;
    }
    node->type_str = type_str;
    node->next = g_var_types;
    g_var_types = node;
    return 1;
}

static const char *lookup_var_type(const char *name) {
    VarType *cur = g_var_types;
    while (cur) {
        if (strcmp(cur->name, name) == 0) return cur->type_str;
        cur = cur->next;
    }
    return NULL;
}

static const char *op_to_str(TokenType op) {
    switch (op) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_PERCENT: return "%";
        case TOKEN_AND: return "&&";
        case TOKEN_OR: return "||";
        case TOKEN_EQUAL_EQUAL: return "==";
        case TOKEN_BANG_EQUAL: return "!=";
        case TOKEN_LESS: return "<";
        case TOKEN_LESS_EQUAL: return "<=";
        case TOKEN_GREATER: return ">";
        case TOKEN_GREATER_EQUAL: return ">=";
        default: return NULL;
    }
}

static char *gen_expr(ASTNode *node);

static const char *expr_c_type(ASTNode *expr) {
    const char *lt;
    const char *rt;

    if (!expr) return "int";

    switch (expr->type) {
        case AST_FLOAT:
            return "double";
        case AST_STRING:
            return "const char *";
        case AST_NUMBER:
        case AST_BOOL:
            return "int";
        case AST_IDENTIFIER: {
            const char *found = lookup_var_type(expr->data.identifier.name);
            return found ? found : "int";
        }
        case AST_UNARY_OP:
            if (expr->data.unary_op.op == TOKEN_BANG) return "int";
            return expr_c_type(expr->data.unary_op.operand);
        case AST_BINARY_OP:
            if (expr->data.binary_op.op == TOKEN_AND || expr->data.binary_op.op == TOKEN_OR ||
                expr->data.binary_op.op == TOKEN_EQUAL_EQUAL || expr->data.binary_op.op == TOKEN_BANG_EQUAL ||
                expr->data.binary_op.op == TOKEN_LESS || expr->data.binary_op.op == TOKEN_LESS_EQUAL ||
                expr->data.binary_op.op == TOKEN_GREATER || expr->data.binary_op.op == TOKEN_GREATER_EQUAL) {
                return "int";
            }
            lt = expr_c_type(expr->data.binary_op.left);
            rt = expr_c_type(expr->data.binary_op.right);
            if (strcmp(lt, "double") == 0 || strcmp(rt, "double") == 0) return "double";
            if (strcmp(lt, "const char *") == 0 || strcmp(rt, "const char *") == 0) return "const char *";
            return "int";
        case AST_CALL_EXPR:
            return "int";
        default:
            return "int";
    }
}

static const char *print_fmt_for_expr(ASTNode *expr) {
    const char *type_str = expr_c_type(expr);
    if (strcmp(type_str, "double") == 0) return "%f";
    if (strcmp(type_str, "const char *") == 0) return "%s";
    return "%d";
}

static char *escape_c_string(const char *src) {
    size_t i;
    size_t len;
    char *out;
    size_t o = 0;

    if (!src) src = "";
    len = strlen(src);

    out = (char *)malloc(len * 2 + 3);
    if (!out) return NULL;

    out[o++] = '"';
    for (i = 0; i < len; i++) {
        char c = src[i];
        switch (c) {
            case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
            case '"': out[o++] = '\\'; out[o++] = '"'; break;
            case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
            case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
            case '\t': out[o++] = '\\'; out[o++] = 't'; break;
            default: out[o++] = c; break;
        }
    }
    out[o++] = '"';
    out[o] = '\0';

    return out;
}

static char *gen_for_clause(ASTNode *node) {
    if (!node) return strdup("");

    switch (node->type) {
        case AST_VARIABLE_DECLARATION: {
            char *expr = gen_expr(node->data.variable_declaration.value);
            const char *type_str;
            char *out;
            int needed;
            if (!expr) return NULL;
            type_str = expr_c_type(node->data.variable_declaration.value);
            needed = snprintf(NULL, 0, "%s %s = %s", type_str, node->data.variable_declaration.identifier, expr);
            out = (char *)malloc((size_t)needed + 1);
            if (!out) {
                free(expr);
                return NULL;
            }
            snprintf(out, (size_t)needed + 1, "%s %s = %s", type_str, node->data.variable_declaration.identifier, expr);
            free(expr);
            if (!remember_var_type(node->data.variable_declaration.identifier, type_str)) {
                free(out);
                return NULL;
            }
            return out;
        }
        case AST_ASSIGNMENT: {
            char *expr = gen_expr(node->data.assignment.value);
            char *out;
            int needed;
            if (!expr) return NULL;
            needed = snprintf(NULL, 0, "%s = %s", node->data.assignment.identifier, expr);
            out = (char *)malloc((size_t)needed + 1);
            if (!out) {
                free(expr);
                return NULL;
            }
            snprintf(out, (size_t)needed + 1, "%s = %s", node->data.assignment.identifier, expr);
            free(expr);
            return out;
        }
        case AST_EXPR_STATEMENT:
            return gen_expr(node->data.expr_stmt.expression);
        default:
            return NULL;
    }
}

static char *gen_expr(ASTNode *node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_NUMBER: {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%d", node->data.number.value);
            return strdup(tmp);
        }

        case AST_FLOAT: {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%f", node->data.fnumber.value);
            return strdup(tmp);
        }

        case AST_BOOL:
            return strdup(node->data.boolean.value ? "1" : "0");

        case AST_STRING:
            return escape_c_string(node->data.string_lit.value);

        case AST_IDENTIFIER:
            return strdup(node->data.identifier.name ? node->data.identifier.name : "");

        case AST_BINARY_OP: {
            char *l = gen_expr(node->data.binary_op.left);
            char *r = gen_expr(node->data.binary_op.right);
            const char *op = op_to_str(node->data.binary_op.op);
            char *out;
            int needed;

            if (!l || !r || !op) {
                fprintf(stderr, "Codegen error: unsupported binary operator in expression\n");
                free(l);
                free(r);
                return NULL;
            }

            needed = snprintf(NULL, 0, "(%s %s %s)", l, op, r);
            out = (char *)malloc((size_t)needed + 1);
            if (!out) {
                free(l);
                free(r);
                return NULL;
            }

            snprintf(out, (size_t)needed + 1, "(%s %s %s)", l, op, r);
            free(l);
            free(r);
            return out;
        }

        case AST_CALL_EXPR: {
            Buffer tmp;
            ASTNodeList *arg;
            int first = 1;

            if (!buf_init(&tmp, 64)) return NULL;
            if (!buf_appendf(&tmp, "%s(", node->data.call_expr.callee ? node->data.call_expr.callee : "")) {
                free(tmp.data);
                return NULL;
            }

            arg = node->data.call_expr.arguments;
            while (arg) {
                char *a = gen_expr(arg->node);
                if (!a) {
                    free(tmp.data);
                    return NULL;
                }
                if (!first && !buf_appendf(&tmp, ", ")) {
                    free(a);
                    free(tmp.data);
                    return NULL;
                }
                if (!buf_appendf(&tmp, "%s", a)) {
                    free(a);
                    free(tmp.data);
                    return NULL;
                }
                free(a);
                first = 0;
                arg = arg->next;
            }

            if (!buf_appendf(&tmp, ")")) {
                free(tmp.data);
                return NULL;
            }

            return tmp.data;
        }

        case AST_UNARY_OP: {
            char *operand = gen_expr(node->data.unary_op.operand);
            const char *op;
            char *out;
            int needed;

            if (!operand) return NULL;

            switch (node->data.unary_op.op) {
                case TOKEN_MINUS: op = "-"; break;
                case TOKEN_BANG: op = "!"; break;
                default:
                    fprintf(stderr, "Codegen error: unsupported unary operator in expression\n");
                    free(operand);
                    return NULL;
            }

            needed = snprintf(NULL, 0, "(%s%s)", op, operand);
            out = (char *)malloc((size_t)needed + 1);
            if (!out) {
                free(operand);
                return NULL;
            }

            snprintf(out, (size_t)needed + 1, "(%s%s)", op, operand);
            free(operand);
            return out;
        }

        default:
            fprintf(stderr, "Codegen error: unsupported expression node type %d\n", (int)node->type);
            return NULL;
    }
}

static void build_indent(int indent, char *out, size_t out_size) {
    int spaces;
    if (!out || out_size == 0) return;
    if (indent <= 0) {
        out[0] = '\0';
        return;
    }
    spaces = indent * 4;
    if (spaces > (int)out_size - 1) spaces = (int)out_size - 1;
    memset(out, ' ', (size_t)spaces);
    out[spaces] = '\0';
}

static int gen_statement(Buffer *b, ASTNode *stmt, int indent);

static int gen_block(Buffer *b, ASTNode *block, int indent) {
    ASTNodeList *cur;
    if (!block || block->type != AST_BLOCK) return 0;
    cur = block->data.block.statements;
    while (cur) {
        if (!gen_statement(b, cur->node, indent)) return 0;
        cur = cur->next;
    }
    return 1;
}

static int gen_statement(Buffer *b, ASTNode *stmt, int indent) {
    char pad[256];
    build_indent(indent, pad, sizeof(pad));

    if (!stmt) return 1;

    switch (stmt->type) {
        case AST_BLOCK:
            return gen_block(b, stmt, indent);

        case AST_VARIABLE_DECLARATION: {
            char *expr = gen_expr(stmt->data.variable_declaration.value);
            const char *type_str;
            int ok;
            if (!expr) return 0;
            type_str = expr_c_type(stmt->data.variable_declaration.value);
            if (!remember_var_type(stmt->data.variable_declaration.identifier, type_str)) {
                free(expr);
                return 0;
            }
            ok = buf_appendf(b, "%s%s %s = %s;\n", pad, type_str, stmt->data.variable_declaration.identifier, expr);
            free(expr);
            return ok;
        }

        case AST_ASSIGNMENT: {
            char *expr = gen_expr(stmt->data.assignment.value);
            int ok;
            if (!expr) return 0;
            ok = buf_appendf(b, "%s%s = %s;\n", pad, stmt->data.assignment.identifier, expr);
            free(expr);
            return ok;
        }

        case AST_PRINT_STATEMENT: {
            ASTNode *val = stmt->data.print_statement.expression;
            const char *fmt = print_fmt_for_expr(val);
            char *expr = gen_expr(val);
            int ok;
            if (!expr) return 0;
            ok = buf_appendf(b, "%sprintf(\"%s\\n\", %s);\n", pad, fmt, expr);
            free(expr);
            return ok;
        }

        case AST_EXPR_STATEMENT: {
            char *expr = gen_expr(stmt->data.expr_stmt.expression);
            int ok;
            if (!expr) return 0;
            ok = buf_appendf(b, "%s%s;\n", pad, expr);
            free(expr);
            return ok;
        }

        case AST_IF: {
            char *cond = gen_expr(stmt->data.if_stmt.condition);
            char close_pad[256];
            if (!cond) return 0;

            if (!buf_appendf(b, "%sif (%s) {\n", pad, cond)) {
                free(cond);
                return 0;
            }
            free(cond);

            if (stmt->data.if_stmt.then_branch) {
                if (!gen_statement(b, stmt->data.if_stmt.then_branch, indent + 1)) return 0;
            }

            build_indent(indent, close_pad, sizeof(close_pad));
            if (!buf_appendf(b, "%s}", close_pad)) return 0;

            if (stmt->data.if_stmt.else_branch) {
                if (!buf_appendf(b, " else {\n")) return 0;
                if (!gen_statement(b, stmt->data.if_stmt.else_branch, indent + 1)) return 0;
                build_indent(indent, close_pad, sizeof(close_pad));
                if (!buf_appendf(b, "%s}\n", close_pad)) return 0;
            } else {
                if (!buf_appendf(b, "\n")) return 0;
            }
            return 1;
        }

        case AST_WHILE: {
            char *cond = gen_expr(stmt->data.while_stmt.condition);
            char close_pad[256];
            if (!cond) return 0;

            if (!buf_appendf(b, "%swhile (%s) {\n", pad, cond)) {
                free(cond);
                return 0;
            }
            free(cond);

            if (stmt->data.while_stmt.body) {
                if (!gen_statement(b, stmt->data.while_stmt.body, indent + 1)) return 0;
            }

            build_indent(indent, close_pad, sizeof(close_pad));
            if (!buf_appendf(b, "%s}\n", close_pad)) return 0;
            return 1;
        }

        case AST_FOR: {
            char *init = gen_for_clause(stmt->data.for_stmt.init);
            char *cond = stmt->data.for_stmt.condition ? gen_expr(stmt->data.for_stmt.condition) : strdup("1");
            char *update = gen_for_clause(stmt->data.for_stmt.update);
            char close_pad[256];
            int ok = 1;

            if (!cond || (!init && stmt->data.for_stmt.init) || (!update && stmt->data.for_stmt.update)) {
                free(init);
                free(cond);
                free(update);
                return 0;
            }

            if (!buf_appendf(b, "%sfor (%s; %s; %s) {\n", pad, init ? init : "", cond, update ? update : "")) ok = 0;
            if (ok && stmt->data.for_stmt.body) {
                if (!gen_statement(b, stmt->data.for_stmt.body, indent + 1)) ok = 0;
            }
            build_indent(indent, close_pad, sizeof(close_pad));
            if (ok && !buf_appendf(b, "%s}\n", close_pad)) ok = 0;

            free(init);
            free(cond);
            free(update);
            return ok;
        }

        case AST_BREAK:
            return buf_appendf(b, "%sbreak;\n", pad);

        case AST_CONTINUE:
            return buf_appendf(b, "%scontinue;\n", pad);

        case AST_RETURN: {
            if (stmt->data.return_stmt.value) {
                char *expr = gen_expr(stmt->data.return_stmt.value);
                int ok;
                if (!expr) return 0;
                ok = buf_appendf(b, "%sreturn %s;\n", pad, expr);
                free(expr);
                return ok;
            }
            return buf_appendf(b, "%sreturn 0;\n", pad);
        }

        default:
            fprintf(stderr, "Codegen error: unsupported statement node type %d\n", (int)stmt->type);
            return 0;
    }
}

static int emit_function_signature(Buffer *b, ASTNode *fn, int with_names) {
    size_t i;
    if (!fn || fn->type != AST_FUNCTION) return 0;

    if (!buf_appendf(b, "int %s(", fn->data.function.name ? fn->data.function.name : "")) return 0;

    for (i = 0; i < fn->data.function.param_count; i++) {
        const char *name = fn->data.function.params ? fn->data.function.params[i] : "arg";
        if (i > 0 && !buf_appendf(b, ", ")) return 0;
        if (with_names) {
            if (!buf_appendf(b, "int %s", name ? name : "arg")) return 0;
            if (!remember_var_type(name ? name : "arg", "int")) return 0;
        } else {
            if (!buf_appendf(b, "int")) return 0;
        }
    }

    if (!buf_appendf(b, ")")) return 0;
    return 1;
}

static int gen_function(Buffer *b, ASTNode *fn) {
    int ok;

    clear_var_types();

    if (!emit_function_signature(b, fn, 1)) {
        clear_var_types();
        return 0;
    }
    if (!buf_appendf(b, " {\n")) {
        clear_var_types();
        return 0;
    }
    ok = gen_block(b, fn->data.function.body, 1);
    if (!ok) {
        clear_var_types();
        return 0;
    }
    if (!buf_appendf(b, "    return 0;\n")) {
        clear_var_types();
        return 0;
    }
    if (!buf_appendf(b, "}\n\n")) {
        clear_var_types();
        return 0;
    }

    clear_var_types();
    return 1;
}

static int gen_program(Buffer *b, ASTNode *program) {
    ASTNodeList *cur;

    if (!buf_appendf(b, "#include <stdio.h>\n\n")) return 0;

    cur = program->data.program.declarations;
    while (cur) {
        ASTNode *decl = cur->node;
        if (decl && decl->type == AST_FUNCTION) {
            if (!emit_function_signature(b, decl, 0)) return 0;
            if (!buf_appendf(b, ";\n")) return 0;
        } else if (decl && decl->type != AST_IMPORT) {
            fprintf(stderr, "Codegen error: top-level non-function statement is not supported\n");
            return 0;
        }
        cur = cur->next;
    }

    if (!buf_appendf(b, "\n")) return 0;

    cur = program->data.program.declarations;
    while (cur) {
        ASTNode *decl = cur->node;
        if (decl && decl->type == AST_FUNCTION) {
            if (!gen_function(b, decl)) return 0;
        }
        cur = cur->next;
    }

    return 1;
}

void init_codegen(void) {}
void finalize_codegen(void) {}
void generate_code(ASTNode *root) { (void)root; }

char *codegen_generate(ASTNode *ast) {
    Buffer b;
    char *out;
    if (!ast) return NULL;
    if (!buf_init(&b, 512)) return NULL;

    if (ast->type == AST_PROGRAM) {
        if (!gen_program(&b, ast)) {
            free(b.data);
            clear_var_types();
            return NULL;
        }
    } else {
        clear_var_types();
        if (!gen_statement(&b, ast, 0)) {
            free(b.data);
            clear_var_types();
            return NULL;
        }
    }

    out = b.data;
    clear_var_types();
    return out;
}
