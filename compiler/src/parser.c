/*
 * File: compiler\src\parser.c
 * Purpose: HOSC source file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "arena.h"

#define AST_ARENA_DEFAULT_SIZE (1024 * 1024)

static char g_last_parser_error[512];
static int g_last_parser_line = 1;
static int g_last_parser_column = 1;
static int g_last_parser_end_column = 1;

const char* parser_get_last_error(void) {
    return g_last_parser_error[0] ? g_last_parser_error : NULL;
}

void parser_get_last_error_span(int* line, int* col, int* end_line, int* end_col) {
    if (line) *line = g_last_parser_line > 0 ? g_last_parser_line : 1;
    if (col) *col = g_last_parser_column > 0 ? g_last_parser_column : 1;
    if (end_line) *end_line = g_last_parser_line > 0 ? g_last_parser_line : 1;
    if (end_col) *end_col = g_last_parser_end_column > 0 ? g_last_parser_end_column : (g_last_parser_column > 0 ? g_last_parser_column : 1);
}

static int min_edit_distance(const char* s1, const char* s2) {
    int m = (int)strlen(s1);
    int n = (int)strlen(s2);
    int dp[64][64];
    int i, j;

    if (m > 60) m = 60;
    if (n > 60) n = 60;

    for (i = 0; i <= m; i++) dp[i][0] = i;
    for (j = 0; j <= n; j++) dp[0][j] = j;

    for (i = 1; i <= m; i++) {
        for (j = 1; j <= n; j++) {
            if (s1[i - 1] == s2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                int a = dp[i - 1][j] + 1;
                int b = dp[i][j - 1] + 1;
                int c = dp[i - 1][j - 1] + 1;
                int min = a < b ? a : b;
                dp[i][j] = min < c ? min : c;
            }
        }
    }
    return dp[m][n];
}

static const char* HOSC_KEYWORDS[] = {
    "var", "let", "const", "func", "package", "import", "print", "prints",
    "if", "else", "while", "for", "return", "break", "continue", "switch",
    "case", "default", NULL
};

static const char* find_closest_keyword(const char* ident) {
    if (!ident || !*ident) return NULL;
    const char* best_match = NULL;
    int best_dist = 999;
    int i;

    for (i = 0; HOSC_KEYWORDS[i] != NULL; i++) {
        const char* kw = HOSC_KEYWORDS[i];
        int dist = min_edit_distance(ident, kw);
        if (dist < best_dist && dist <= 2) {
            best_dist = dist;
            best_match = kw;
        }
    }
    return best_match;
}

static const char* find_closest_function_or_keyword(Parser* p, const char* name, int* is_kw) {
    if (!name || !*name) return NULL;
    const char* best_match = NULL;
    int best_dist = 999;
    static const char* BUILTIN_FUNCS[] = {
        "print", "prints", "window.create", "audio.play", "text.create", NULL
    };
    int i;
    if (is_kw) *is_kw = 0;

    for (i = 0; BUILTIN_FUNCS[i] != NULL; i++) {
        int dist = min_edit_distance(name, BUILTIN_FUNCS[i]);
        if (dist < best_dist && dist <= 2) {
            best_dist = dist;
            best_match = BUILTIN_FUNCS[i];
            if (is_kw) *is_kw = 0;
        }
    }

    for (i = 0; HOSC_KEYWORDS[i] != NULL; i++) {
        int dist = min_edit_distance(name, HOSC_KEYWORDS[i]);
        if (dist < best_dist && dist <= 2) {
            best_dist = dist;
            best_match = HOSC_KEYWORDS[i];
            if (is_kw) *is_kw = 1;
        }
    }

    if (p && p->tokens) {
        for (i = 0; i < p->token_count - 1; i++) {
            if (p->tokens[i].type == TOKEN_FUNC && p->tokens[i+1].type == TOKEN_IDENTIFIER) {
                const char* fn_name = p->tokens[i+1].value.identifier;
                if (fn_name && strcmp(fn_name, name) != 0) {
                    int dist = min_edit_distance(name, fn_name);
                    if (dist < best_dist && dist <= 2) {
                        best_dist = dist;
                        best_match = fn_name;
                        if (is_kw) *is_kw = 0;
                    }
                }
            }
        }
    }

    return best_match;
}



static Token* current_token(Parser *p) { return &p->tokens[p->current]; }
static int is_at_end(Parser *p) { return current_token(p)->type == TOKEN_EOF; }
static Token* advance_tok(Parser *p) { if (!is_at_end(p)) p->current++; return &p->tokens[p->current - 1]; }
static int check(Parser *p, TokenType t) { return current_token(p)->type == t; }
static int match(Parser *p, TokenType t) { if (check(p, t)) { advance_tok(p); return 1; } return 0; }
static TokenType peek_type(Parser *p, int offset) {
    int index = p->current + offset;
    if (index < 0 || index >= p->token_count) return TOKEN_EOF;
    return p->tokens[index].type;
}

static char *dup_str(Parser *p, const char *s) {
    size_t len = strlen(s);
    char *out = p->arena ? (char *)arena_alloc(p->arena, len + 1) : (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static char *dup_str_n(Parser *p, const char *s, size_t len) {
    char *out = p->arena ? (char *)arena_alloc(p->arena, len + 1) : (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

const char* parser_get_error(Parser* p) {
    return p ? p->error_message : NULL;
}

void parser_set_error(Parser* p, const char* message) {
    if (!p || !message) return;
    if (p->error_message[0] != '\0') return; /* Preserve the first encountered error */
    snprintf(p->error_message, sizeof(p->error_message), "%s", message);
    if (p->tokens && p->current >= 0) {
        int idx = p->current;
        if (idx >= p->token_count && p->token_count > 0) {
            idx = p->token_count - 1;
        }
        if (idx >= 0 && idx < p->token_count) {
            p->error_line = p->tokens[idx].line;
            p->error_column = p->tokens[idx].column;
            p->error_end_column = p->tokens[idx].column + 1;
            if (p->tokens[idx].type == TOKEN_IDENTIFIER && p->tokens[idx].value.identifier) {
                p->error_end_column = p->tokens[idx].column + (int)strlen(p->tokens[idx].value.identifier);
            } else if (p->tokens[idx].type == TOKEN_STRING && p->tokens[idx].value.string_lit) {
                p->error_end_column = p->tokens[idx].column + (int)strlen(p->tokens[idx].value.string_lit) + 2;
            }
        }
    }
}



static int check_depth_enter(Parser *p) {
    if (!p) return 0;
    if (p->depth >= MAX_PARSE_DEPTH) {
        parser_set_error(p, "Maximum parse depth exceeded");
        return 0;
    }
    p->depth++;
    return 1;
}

static void check_depth_leave(Parser *p) {
    if (p && p->depth > 0) {
        p->depth--;
    }
}

static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p);

static void free_node_list(ASTNodeList *list) {
    while (list) {
        ASTNodeList *next = list->next;
        free_ast(list->node);
        list = next;
    }
}

static int token_starts_statement(TokenType t) {
    return t == TOKEN_IMPORT ||
           t == TOKEN_LET ||
           t == TOKEN_VAR ||
           t == TOKEN_CONST ||
           t == TOKEN_PRINT ||
           t == TOKEN_PRINTS ||
           t == TOKEN_IF ||
           t == TOKEN_WHILE ||
           t == TOKEN_FOR ||
           t == TOKEN_RETURN ||
           t == TOKEN_BREAK ||
           t == TOKEN_CONTINUE ||
           t == TOKEN_SWITCH ||
           t == TOKEN_WINDOW ||
           t == TOKEN_TEXT ||
           t == TOKEN_FUNC ||
           t == TOKEN_PACKAGE ||
           t == TOKEN_IDENTIFIER ||
           t == TOKEN_LBRACE ||
           t == TOKEN_ELSE;
}

static int consume_statement_end(Parser *p) {
    if (match(p, TOKEN_SEMICOLON)) return 1;
    if (check(p, TOKEN_RBRACE) || check(p, TOKEN_EOF)) return 1;
    if (token_starts_statement(current_token(p)->type)) return 1;
    return 0;
}

static void skip_statement(Parser *p) {
    int paren_depth = 0;
    int consumed_any = 0;
    while (!is_at_end(p)) {
        TokenType t = current_token(p)->type;
        if (t == TOKEN_SEMICOLON) {
            advance_tok(p);
            return;
        }
        if (paren_depth == 0 && t == TOKEN_RBRACE) {
            if (!consumed_any) advance_tok(p);
            return;
        }
        if (consumed_any && paren_depth == 0 && token_starts_statement(t)) {
            return;
        }
        if (t == TOKEN_LPAREN) paren_depth++;
        else if (t == TOKEN_RPAREN && paren_depth > 0) paren_depth--;
        advance_tok(p);
        consumed_any = 1;
    }
}

static ASTNode *create_ast_node_at(Parser *p, ASTNodeType type) {
    ASTNode *node = create_ast_node(type);
    if (node && p && p->tokens && p->current >= 0 && p->current < p->token_count) {
        Token *tok = &p->tokens[p->current];
        node->line = tok->line > 0 ? tok->line : 1;
        node->column = tok->column > 0 ? tok->column : 1;
        node->end_column = node->column + 1;
    }
    return node;
}

static ASTNode *make_noop(void) {
    ASTNode *n = create_ast_node_at(NULL, AST_BLOCK);
    if (!n) return NULL;
    n->data.block.statements = NULL;
    return n;
}


static int append_text(char **buf, size_t *len, size_t *cap, const char *text, size_t text_len) {
    if (*len + text_len + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 64 : *cap;
        while (new_cap < *len + text_len + 1) new_cap *= 2;
        char *new_buf = (char *)realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    memcpy(*buf + *len, text, text_len);
    *len += text_len;
    (*buf)[*len] = '\0';
    return 1;
}

static char *parse_identifier_path(Parser *p) {
    char *tmp = NULL;
    size_t len = 0;
    size_t cap = 0;
    char *final_name;

    if (!check(p, TOKEN_IDENTIFIER)) return NULL;

    while (1) {
        Token *id = advance_tok(p);
        size_t id_len = strlen(id->value.identifier);
        if (!append_text(&tmp, &len, &cap, id->value.identifier, id_len)) {
            free(tmp);
            return NULL;
        }

        if (!match(p, TOKEN_DOT)) break;
        if (!append_text(&tmp, &len, &cap, ".", 1)) {
            free(tmp);
            return NULL;
        }
        if (!check(p, TOKEN_IDENTIFIER)) {
            free(tmp);
            return NULL;
        }
    }

    final_name = dup_str_n(p, tmp, len);
    free(tmp);
    return final_name;
}

static ASTNode *make_string_expr(Parser *p, const char *value) {
    ASTNode *n = create_ast_node(AST_STRING);
    if (!n) return NULL;
    n->data.string_lit.value = dup_str(p, value ? value : "");
    return n;
}

static ASTNodeList *parse_audio_play_config_args(Parser *p) {
    ASTNodeList *args = NULL;
    ASTNode *src_expr = NULL;

    if (!match(p, TOKEN_LBRACE)) return NULL;

    while (!check(p, TOKEN_RBRACE) && !is_at_end(p)) {
        Token *property;
        int is_src;

        if (!check(p, TOKEN_IDENTIFIER)) {
            free_ast(src_expr);
            return NULL;
        }

        property = advance_tok(p);
        is_src = (strcmp(property->value.identifier, "src") == 0);

        if (!match(p, TOKEN_COLON)) {
            free_ast(src_expr);
            return NULL;
        }

        if (is_src) {
            if (!check(p, TOKEN_STRING)) {
                free_ast(src_expr);
                return NULL;
            }
            free_ast(src_expr);
            src_expr = make_string_expr(p, current_token(p)->value.string_lit);
            advance_tok(p);
            if (!src_expr) return NULL;
        } else if (check(p, TOKEN_STRING) || check(p, TOKEN_NUMBER) || check(p, TOKEN_FLOAT) ||
                   check(p, TOKEN_BOOL_TRUE) || check(p, TOKEN_BOOL_FALSE)) {
            advance_tok(p);
        } else {
            free_ast(src_expr);
            return NULL;
        }

        if (!match(p, TOKEN_COMMA)) break;
    }

    if (!match(p, TOKEN_RBRACE) || !src_expr) {
        free_ast(src_expr);
        return NULL;
    }

    return ast_list_append(args, src_expr);
}

static ASTNode *parse_primary(Parser *p) {
    if (match(p, TOKEN_NUMBER)) {
        Token *t = &p->tokens[p->current - 1];
        ASTNode *n = create_ast_node_at(p, AST_NUMBER);
        if (!n) return NULL;
        n->data.number.value = t->value.number;
        return n;
    }

    if (match(p, TOKEN_FLOAT)) {
        Token *t = &p->tokens[p->current - 1];
        ASTNode *n = create_ast_node_at(p, AST_FLOAT);
        if (!n) return NULL;
        n->data.fnumber.value = t->value.fnumber;
        return n;
    }


    if (check(p, TOKEN_STRING)) {
        Token *t = advance_tok(p);
        ASTNode *n = create_ast_node_at(p, AST_STRING);
        if (!n) return NULL;
        n->data.string_lit.value = dup_str(p, t->value.string_lit);
        return n;
    }

    if (check(p, TOKEN_BOOL_TRUE) || check(p, TOKEN_BOOL_FALSE)) {
        Token *t = advance_tok(p);
        ASTNode *n = create_ast_node_at(p, AST_BOOL);
        if (!n) return NULL;
        n->data.boolean.value = (t->type == TOKEN_BOOL_TRUE);
        return n;
    }

    if (check(p, TOKEN_IDENTIFIER)) {
        ASTNode *node;
        int tok_line = current_token(p)->line;
        int tok_col = current_token(p)->column;
        char *path = parse_identifier_path(p);
        if (!path) return NULL;

        if (match(p, TOKEN_LPAREN)) {
            int tok_line = p->tokens[p->current - 2].line;
            int tok_col = p->tokens[p->current - 2].column;
            ASTNodeList *args = NULL;

            int is_known = (strcmp(path, "print") == 0 || strcmp(path, "prints") == 0 ||
                            strcmp(path, "window.create") == 0 || strcmp(path, "audio.play") == 0 ||
                            strcmp(path, "text.create") == 0);
            if (!is_known && p->tokens) {
                int i;
                for (i = 0; i < p->token_count - 1; i++) {
                    if (p->tokens[i].type == TOKEN_FUNC && p->tokens[i+1].type == TOKEN_IDENTIFIER) {
                        if (p->tokens[i+1].value.identifier && strcmp(p->tokens[i+1].value.identifier, path) == 0) {
                            is_known = 1;
                            break;
                        }
                    }
                }
            }

            if (!is_known && strlen(path) >= 3) {
                int is_kw = 0;
                const char* kw = find_closest_function_or_keyword(p, path, &is_kw);
                if (kw && strlen(kw) >= 3) {
                    int dist = min_edit_distance(path, kw);
                    int max_allowed = (strlen(path) <= 4) ? 1 : 2;
                    if (dist <= max_allowed) {
                        char msg[256];
                        if (is_kw) {
                            snprintf(msg, sizeof(msg), "unknown keyword '%s', did you mean '%s'?", path, kw);
                        } else {
                            snprintf(msg, sizeof(msg), "unknown function '%s', did you mean '%s'?", path, kw);
                        }
                        parser_set_error(p, msg);
                        p->error_line = tok_line;
                        p->error_column = tok_col;
                        p->error_end_column = tok_col + (int)strlen(path);
                        return NULL;
                    }
                }
            }



            if (strcmp(path, "audio.play") == 0 && check(p, TOKEN_LBRACE)) {
                args = parse_audio_play_config_args(p);
                if (!args) return NULL;
            } else if (!check(p, TOKEN_RPAREN)) {
                while (1) {
                    ASTNode *arg = parse_expression(p);
                    if (!arg) return NULL;
                    args = ast_list_append(args, arg);
                    if (!match(p, TOKEN_COMMA)) break;
                }
            }
            if (!match(p, TOKEN_RPAREN)) return NULL;

            node = create_ast_node_at(p, AST_CALL_EXPR);
            if (!node) return NULL;
            node->data.call_expr.callee = path;
            node->data.call_expr.arguments = args;
            node->line = tok_line;
            node->column = tok_col;
            node->end_column = tok_col + (int)strlen(path);
            return node;
        }


        node = create_ast_node_at(p, AST_IDENTIFIER);
        if (!node) return NULL;
        node->data.identifier.name = path;
        node->line = tok_line;
        node->column = tok_col;
        node->end_column = tok_col + (int)strlen(path);
        return node;
    }

    if (match(p, TOKEN_LPAREN)) {
        ASTNode *expr = parse_expression(p);
        if (!expr) return NULL;
        if (!match(p, TOKEN_RPAREN)) {
            free_ast(expr);
            return NULL;
        }
        return expr;
    }

    return NULL;
}

static ASTNode *parse_unary(Parser *p) {
    if (match(p, TOKEN_MINUS) || match(p, TOKEN_BANG)) {
        TokenType op = p->tokens[p->current - 1].type;
        ASTNode *right = parse_unary(p);
        ASTNode *unary;
        if (!right) return NULL;

        unary = create_ast_node(AST_UNARY_OP);
        if (!unary) {
            free_ast(right);
            return NULL;
        }

        unary->data.unary_op.op = op;
        unary->data.unary_op.operand = right;
        return unary;
    }
    return parse_primary(p);
}

static ASTNode *parse_factor(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (!left) return NULL;

    while (check(p, TOKEN_STAR) || check(p, TOKEN_SLASH) || check(p, TOKEN_PERCENT)) {
        TokenType op = current_token(p)->type;
        ASTNode *right;
        ASTNode *bin;
        advance_tok(p);
        right = parse_unary(p);
        if (!right) {
            free_ast(left);
            return NULL;
        }
        bin = create_ast_node_at(p, AST_BINARY_OP);
        if (!bin) {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        bin->data.binary_op.left = left;
        bin->data.binary_op.right = right;
        bin->data.binary_op.op = op;
        if (left) {
            bin->line = left->line;
            bin->column = left->column;
            bin->end_column = (right && right->end_column > 0) ? right->end_column : left->end_column;
        }
        left = bin;
    }


    return left;
}

static ASTNode *parse_term(Parser *p) {
    ASTNode *left = parse_factor(p);
    if (!left) return NULL;

    while (check(p, TOKEN_PLUS) || check(p, TOKEN_MINUS)) {
        TokenType op = current_token(p)->type;
        ASTNode *right;
        ASTNode *bin;
        advance_tok(p);
        right = parse_factor(p);
        if (!right) {
            free_ast(left);
            return NULL;
        }
        bin = create_ast_node_at(p, AST_BINARY_OP);
        if (!bin) {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        bin->data.binary_op.left = left;
        bin->data.binary_op.right = right;
        bin->data.binary_op.op = op;
        if (left) {
            bin->line = left->line;
            bin->column = left->column;
            bin->end_column = (right && right->end_column > 0) ? right->end_column : left->end_column;
        }
        left = bin;
    }


    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_term(p);
    if (!left) return NULL;

    while (check(p, TOKEN_LESS) || check(p, TOKEN_LESS_EQUAL) ||
           check(p, TOKEN_GREATER) || check(p, TOKEN_GREATER_EQUAL)) {
        TokenType op = current_token(p)->type;
        ASTNode *right;
        ASTNode *bin;
        advance_tok(p);
        right = parse_term(p);
        if (!right) {
            free_ast(left);
            return NULL;
        }
        bin = create_ast_node(AST_BINARY_OP);
        if (!bin) {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        bin->data.binary_op.left = left;
        bin->data.binary_op.right = right;
        bin->data.binary_op.op = op;
        left = bin;
    }

    return left;
}

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_comparison(p);
    if (!left) return NULL;

    while (check(p, TOKEN_EQUAL_EQUAL) || check(p, TOKEN_BANG_EQUAL)) {
        TokenType op = current_token(p)->type;
        ASTNode *right;
        ASTNode *bin;
        advance_tok(p);
        right = parse_comparison(p);
        if (!right) {
            free_ast(left);
            return NULL;
        }
        bin = create_ast_node(AST_BINARY_OP);
        if (!bin) {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        bin->data.binary_op.left = left;
        bin->data.binary_op.right = right;
        bin->data.binary_op.op = op;
        left = bin;
    }

    return left;
}

static ASTNode *parse_logic_and(Parser *p) {
    ASTNode *left = parse_equality(p);
    if (!left) return NULL;

    while (check(p, TOKEN_AND)) {
        TokenType op = current_token(p)->type;
        ASTNode *right;
        ASTNode *bin;
        advance_tok(p);
        right = parse_equality(p);
        if (!right) {
            free_ast(left);
            return NULL;
        }
        bin = create_ast_node(AST_BINARY_OP);
        if (!bin) {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        bin->data.binary_op.left = left;
        bin->data.binary_op.right = right;
        bin->data.binary_op.op = op;
        left = bin;
    }

    return left;
}

static ASTNode *parse_logic_or(Parser *p) {
    ASTNode *left = parse_logic_and(p);
    if (!left) return NULL;

    while (check(p, TOKEN_OR)) {
        TokenType op = current_token(p)->type;
        ASTNode *right;
        ASTNode *bin;
        advance_tok(p);
        right = parse_logic_and(p);
        if (!right) {
            free_ast(left);
            return NULL;
        }
        bin = create_ast_node(AST_BINARY_OP);
        if (!bin) {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        bin->data.binary_op.left = left;
        bin->data.binary_op.right = right;
        bin->data.binary_op.op = op;
        left = bin;
    }

    return left;
}

static ASTNode *parse_expression(Parser *p) {
    if (!check_depth_enter(p)) return NULL;
    ASTNode *res = parse_logic_or(p);
    check_depth_leave(p);
    return res;
}

static int is_assignment_start(Parser *p) {
    return check(p, TOKEN_IDENTIFIER) && peek_type(p, 1) == TOKEN_ASSIGN;
}

static ASTNode *parse_var_decl_core(Parser *p, int is_var, int require_end) {
    Token *id_tok;
    ASTNode *value;
    ASTNode *node;

    if (!check(p, TOKEN_IDENTIFIER)) return NULL;
    id_tok = advance_tok(p);
    if (!match(p, TOKEN_ASSIGN)) return NULL;

    value = parse_expression(p);
    if (!value) return NULL;

    if (require_end && !consume_statement_end(p)) {
        free_ast(value);
        return NULL;
    }

    node = create_ast_node(AST_VARIABLE_DECLARATION);
    if (!node) {
        free_ast(value);
        return NULL;
    }

    node->data.variable_declaration.identifier = dup_str(p, id_tok->value.identifier);
    node->data.variable_declaration.value = value;
    node->data.variable_declaration.is_var = is_var;
    node->line = id_tok->line;
    node->column = id_tok->column;
    node->end_column = id_tok->column + (int)strlen(id_tok->value.identifier);
    return node;
}

static ASTNode *parse_assignment_core(Parser *p, int require_end) {
    Token *id;
    ASTNode *value;
    ASTNode *node;

    if (!check(p, TOKEN_IDENTIFIER)) return NULL;
    id = advance_tok(p);

    if (!match(p, TOKEN_ASSIGN)) return NULL;

    value = parse_expression(p);
    if (!value) return NULL;

    if (require_end && !consume_statement_end(p)) {
        free_ast(value);
        return NULL;
    }

    node = create_ast_node(AST_ASSIGNMENT);
    if (!node) {
        free_ast(value);
        return NULL;
    }

    node->data.assignment.identifier = dup_str(p, id->value.identifier);
    node->data.assignment.value = value;
    node->line = id->line;
    node->column = id->column;
    node->end_column = id->column + (int)strlen(id->value.identifier);
    return node;
}

static ASTNode *parse_print(Parser *p) {
    ASTNode *expr;
    ASTNode *node;

    if (match(p, TOKEN_LPAREN)) {
        expr = parse_expression(p);
        if (!expr) {
            parser_set_error(p, "expected expression in print()");
            return NULL;
        }
        if (!match(p, TOKEN_RPAREN)) {
            parser_set_error(p, "expected ')' after print expression");
            free_ast(expr);
            return NULL;
        }
    } else {
        expr = parse_expression(p);
        if (!expr) {
            parser_set_error(p, "expected expression after print");
            return NULL;
        }
    }

    if (!consume_statement_end(p)) {
        parser_set_error(p, "expected ';' or newline after print statement");
        free_ast(expr);
        return NULL;
    }

    node = create_ast_node(AST_PRINT_STATEMENT);
    if (!node) {
        free_ast(expr);
        return NULL;
    }
    node->data.print_statement.expression = expr;
    return node;
}

static ASTNode *parse_prints(Parser *p) {
    Token *text;
    ASTNode *expr;
    ASTNode *node;

    if (!match(p, TOKEN_LBRACKET)) {
        parser_set_error(p, "expected '[' after 'prints'");
        return NULL;
    }

    if (!check(p, TOKEN_STRING)) {
        parser_set_error(p, "prints[...] expects a raw string literal delimited by backticks");
        return NULL;
    }
    text = advance_tok(p);

    if (!match(p, TOKEN_RBRACKET)) {
        if (check(p, TOKEN_IDENTIFIER)) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "unexpected token '%s' after raw string; did you mean ']'?\n\nNote: backtick raw strings use \\` to embed a literal backtick",
                current_token(p)->value.identifier ? current_token(p)->value.identifier : "?");
            parser_set_error(p, msg);
        } else {
            parser_set_error(p, "expected ']' to close prints[...]");
        }
        return NULL;
    }

    if (!consume_statement_end(p)) return NULL;

    expr = create_ast_node(AST_STRING);
    if (!expr) return NULL;
    expr->data.string_lit.value = dup_str(p, text->value.string_lit);

    node = create_ast_node(AST_PRINT_STATEMENT);
    if (!node) {
        free_ast(expr);
        return NULL;
    }
    node->data.print_statement.expression = expr;
    return node;
}

static ASTNode *parse_break_or_continue(Parser *p, int is_continue) {
    ASTNode *node = create_ast_node(is_continue ? AST_CONTINUE : AST_BREAK);
    if (!node) return NULL;
    if (!consume_statement_end(p)) {
        free_ast(node);
        return NULL;
    }
    return node;
}
static ASTNode *parse_return(Parser *p) {
    ASTNode *node = create_ast_node(AST_RETURN);
    if (!node) return NULL;

    if (!check(p, TOKEN_SEMICOLON) && !check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        node->data.return_stmt.value = parse_expression(p);
        if (!node->data.return_stmt.value) {
            free_ast(node);
            return NULL;
        }
    }

    if (!consume_statement_end(p)) {
        free_ast(node);
        return NULL;
    }

    return node;
}

static ASTNode *parse_expr_statement_core(Parser *p, int require_end) {
    ASTNode *expr = parse_expression(p);
    ASTNode *stmt;
    if (!expr) return NULL;
    if (require_end && !consume_statement_end(p)) {
        free_ast(expr);
        return NULL;
    }

    stmt = create_ast_node(AST_EXPR_STATEMENT);
    if (!stmt) {
        free_ast(expr);
        return NULL;
    }
    stmt->data.expr_stmt.expression = expr;
    return stmt;
}

static ASTNode *parse_switch(Parser *p) {
    ASTNode *expression, *node;
    ASTNodeList *cases = NULL;

    if (!match(p, TOKEN_SWITCH) || !match(p, TOKEN_LPAREN)) return NULL;
    expression = parse_expression(p);
    if (!expression || !match(p, TOKEN_RPAREN) || !match(p, TOKEN_LBRACE)) {
        free_ast(expression);
        return NULL;
    }
    while (!check(p, TOKEN_RBRACE) && !is_at_end(p)) {
        ASTNode *case_node, *value = NULL, *body;
        ASTNodeList *statements = NULL;
        int is_default = match(p, TOKEN_DEFAULT);
        if (!is_default) {
            if (!match(p, TOKEN_CASE)) goto fail;
            value = parse_expression(p);
            if (!value) goto fail;
        }
        if (!match(p, TOKEN_COLON)) goto fail;
        body = create_ast_node(AST_BLOCK);
        if (!body) goto fail;
        while (!check(p, TOKEN_CASE) && !check(p, TOKEN_DEFAULT) && !check(p, TOKEN_RBRACE) && !is_at_end(p)) {
            ASTNode *statement = parse_statement(p);
            if (!statement) { free_ast(body); goto fail; }
            statements = ast_list_append(statements, statement);
        }
        body->data.block.statements = statements;
        case_node = create_ast_node(AST_CASE);
        if (!case_node) { free_ast(body); goto fail; }
        case_node->data.case_stmt.value = value;
        case_node->data.case_stmt.body = body;
        cases = ast_list_append(cases, case_node);
        continue;
fail:
        free_ast(value);
        free_ast(expression);
        free_node_list(cases);
        return NULL;
    }
    if (!match(p, TOKEN_RBRACE)) { free_ast(expression); free_node_list(cases); return NULL; }
    node = create_ast_node(AST_SWITCH);
    if (!node) { free_ast(expression); free_node_list(cases); return NULL; }
    node->data.switch_stmt.expression = expression;
    node->data.switch_stmt.cases = cases;
    return node;
}

static ASTNode *parse_if(Parser *p) {
    ASTNode *cond;
    ASTNode *thenb;
    ASTNode *elseb = NULL;
    ASTNode *node;

    if (!match(p, TOKEN_IF)) {
        parser_set_error(p, "expected 'if' keyword");
        return NULL;
    }
    cond = parse_expression(p);
    if (!cond) {
        parser_set_error(p, "expected condition after 'if'");
        return NULL;
    }

    thenb = parse_block(p);
    if (!thenb) {
        parser_set_error(p, "expected block '{...}' after if condition");
        free_ast(cond);
        return NULL;
    }

    if (match(p, TOKEN_ELSE)) {
        if (check(p, TOKEN_IF)) elseb = parse_if(p);
        else elseb = parse_block(p);
        if (!elseb) {
            parser_set_error(p, "expected block or 'if' after 'else'");
            free_ast(cond);
            free_ast(thenb);
            return NULL;
        }
    }

    node = create_ast_node(AST_IF);
    if (!node) {
        free_ast(cond);
        free_ast(thenb);
        free_ast(elseb);
        return NULL;
    }
    node->data.if_stmt.condition = cond;
    node->data.if_stmt.then_branch = thenb;
    node->data.if_stmt.else_branch = elseb;
    return node;
}

static ASTNode *parse_while(Parser *p) {
    ASTNode *cond;
    ASTNode *body;
    ASTNode *node;

    if (!match(p, TOKEN_WHILE)) return NULL;
    cond = parse_expression(p);
    if (!cond) return NULL;

    body = parse_block(p);
    if (!body) {
        free_ast(cond);
        return NULL;
    }

    node = create_ast_node(AST_WHILE);
    if (!node) {
        free_ast(cond);
        free_ast(body);
        return NULL;
    }
    node->data.while_stmt.condition = cond;
    node->data.while_stmt.body = body;
    return node;
}

static ASTNode *parse_for(Parser *p) {
    ASTNode *init = NULL;
    ASTNode *cond = NULL;
    ASTNode *update = NULL;
    ASTNode *body;
    ASTNode *node;

    if (!match(p, TOKEN_FOR)) {
        parser_set_error(p, "expected 'for' keyword");
        return NULL;
    }

    if (match(p, TOKEN_LPAREN)) {
        if (!check(p, TOKEN_SEMICOLON)) {
            if (match(p, TOKEN_LET)) init = parse_var_decl_core(p, 0, 0);
            else if (match(p, TOKEN_VAR)) init = parse_var_decl_core(p, 1, 0);
            else if (is_assignment_start(p)) init = parse_assignment_core(p, 0);
            else {
                if (check(p, TOKEN_IDENTIFIER)) {
                    Token* tok = current_token(p);
                    const char* kw = find_closest_keyword(tok->value.identifier);
                    if (kw) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "unknown keyword '%s', did you mean '%s'?", tok->value.identifier, kw);
                        parser_set_error(p, msg);
                        return NULL;
                    }
                }
                init = parse_expr_statement_core(p, 0);
            }
            if (!init) {
                if (p->error_message[0] == '\0') {
                    parser_set_error(p, "invalid for loop initialization");
                }
                return NULL;
            }
        }

        if (!match(p, TOKEN_SEMICOLON)) {
            parser_set_error(p, "expected ';' after for loop init");
            free_ast(init);
            return NULL;
        }

        if (!check(p, TOKEN_SEMICOLON)) {
            cond = parse_expression(p);
            if (!cond) {
                parser_set_error(p, "expected condition in for loop");
                free_ast(init);
                return NULL;
            }
        }
        if (!match(p, TOKEN_SEMICOLON)) {
            parser_set_error(p, "expected ';' after for loop condition");
            free_ast(init);
            free_ast(cond);
            return NULL;
        }

        if (!check(p, TOKEN_RPAREN)) {
            if (is_assignment_start(p)) update = parse_assignment_core(p, 0);
            else update = parse_expr_statement_core(p, 0);
            if (!update) {
                parser_set_error(p, "invalid for loop update");
                free_ast(init);
                free_ast(cond);
                return NULL;
            }
        }

        if (!match(p, TOKEN_RPAREN)) {
            parser_set_error(p, "expected ')' after for loop update");
            free_ast(init);
            free_ast(cond);
            free_ast(update);
            return NULL;
        }
    } else {
        cond = parse_expression(p);
        if (!cond) {
            parser_set_error(p, "expected condition in for loop");
            return NULL;
        }
    }

    body = parse_block(p);
    if (!body) {
        parser_set_error(p, "expected block '{...}' after for loop");
        free_ast(init);
        free_ast(cond);
        free_ast(update);
        return NULL;
    }

    node = create_ast_node(AST_FOR);
    if (!node) {
        free_ast(init);
        free_ast(cond);
        free_ast(update);
        free_ast(body);
        return NULL;
    }

    node->data.for_stmt.init = init;
    node->data.for_stmt.condition = cond;
    node->data.for_stmt.update = update;
    node->data.for_stmt.body = body;
    return node;
}

static ASTNode *parse_import(Parser *p) {
    Token *path;
    ASTNode *n;

    if (!match(p, TOKEN_IMPORT)) return NULL;
    if (!check(p, TOKEN_STRING)) return NULL;
    path = advance_tok(p);
    if (!consume_statement_end(p)) return NULL;

    n = create_ast_node(AST_IMPORT);
    if (!n) return NULL;
    n->data.import_stmt.path = dup_str(p, path->value.string_lit);
    return n;
}

static ASTNode *parse_window(Parser *p) {
    Token *title;
    ASTNode *n;

    if (!match(p, TOKEN_WINDOW)) return NULL;
    if (!match(p, TOKEN_LPAREN)) return NULL;
    if (!check(p, TOKEN_STRING)) return NULL;
    title = advance_tok(p);
    if (!match(p, TOKEN_RPAREN)) return NULL;
    if (!consume_statement_end(p)) return NULL;

    n = create_ast_node(AST_WINDOW_STMT);
    if (!n) return NULL;
    n->data.window_stmt.title = dup_str(p, title->value.string_lit);
    return n;
}

static ASTNode *parse_text(Parser *p) {
    ASTNode *x;
    ASTNode *y;
    Token *msg;
    ASTNode *n;

    if (!match(p, TOKEN_TEXT)) return NULL;
    if (!match(p, TOKEN_LPAREN)) return NULL;

    x = parse_expression(p);
    if (!x) return NULL;
    if (!match(p, TOKEN_COMMA)) {
        free_ast(x);
        return NULL;
    }

    y = parse_expression(p);
    if (!y) {
        free_ast(x);
        return NULL;
    }
    if (!match(p, TOKEN_COMMA)) {
        free_ast(x);
        free_ast(y);
        return NULL;
    }

    if (!check(p, TOKEN_STRING)) {
        free_ast(x);
        free_ast(y);
        return NULL;
    }
    msg = advance_tok(p);
    if (!match(p, TOKEN_RPAREN)) {
        free_ast(x);
        free_ast(y);
        return NULL;
    }
    if (!consume_statement_end(p)) {
        free_ast(x);
        free_ast(y);
        return NULL;
    }

    n = create_ast_node(AST_TEXT_STMT);
    if (!n) {
        free_ast(x);
        free_ast(y);
        return NULL;
    }
    n->data.text_stmt.x = x;
    n->data.text_stmt.y = y;
    n->data.text_stmt.msg = dup_str(p, msg->value.string_lit);
    return n;
}

static ASTNode *parse_statement_internal(Parser *p);

static ASTNode *parse_statement(Parser *p) {
    if (!check_depth_enter(p)) return NULL;
    ASTNode *res = parse_statement_internal(p);
    check_depth_leave(p);
    return res;
}

static ASTNode *parse_statement_internal(Parser *p) {
    if (check(p, TOKEN_IMPORT)) return parse_import(p);

    if (match(p, TOKEN_LET)) return parse_var_decl_core(p, 0, 1);
    if (match(p, TOKEN_VAR)) return parse_var_decl_core(p, 1, 1);
    if (match(p, TOKEN_CONST)) return parse_var_decl_core(p, 0, 1);

    if (match(p, TOKEN_PRINT)) return parse_print(p);
    if (match(p, TOKEN_PRINTS)) return parse_prints(p);
    if (match(p, TOKEN_RETURN)) return parse_return(p);
    if (match(p, TOKEN_BREAK)) return parse_break_or_continue(p, 0);
    if (match(p, TOKEN_CONTINUE)) return parse_break_or_continue(p, 1);

    if (check(p, TOKEN_IF)) return parse_if(p);
    if (check(p, TOKEN_WHILE)) return parse_while(p);
    if (check(p, TOKEN_FOR)) return parse_for(p);
    if (check(p, TOKEN_SWITCH)) return parse_switch(p);

    if (check(p, TOKEN_WINDOW)) return parse_window(p);
    if (check(p, TOKEN_TEXT)) return parse_text(p);

    if (check(p, TOKEN_LBRACE)) return parse_block(p);

    if (is_assignment_start(p)) {
        ASTNode *as = parse_assignment_core(p, 1);
        if (as) return as;
    }

    {
        int save = p->current;
        ASTNode *expr_stmt = parse_expr_statement_core(p, 1);
        if (expr_stmt) return expr_stmt;
        if (p->error_message[0] != '\0') return NULL;
        p->current = save;
    }

    if (check(p, TOKEN_IDENTIFIER)) {
        Token* tok = current_token(p);
        const char* kw = find_closest_keyword(tok->value.identifier);
        if (kw) {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown keyword '%s', did you mean '%s'?", tok->value.identifier, kw);
            parser_set_error(p, msg);
            return NULL;
        }
    }

    skip_statement(p);
    return make_noop();
}

static ASTNode *parse_block(Parser *p) {
    ASTNode *block;
    ASTNodeList *stmts = NULL;

    block = create_ast_node(AST_BLOCK);
    if (!block) return NULL;
    if (!match(p, TOKEN_LBRACE)) {
        parser_set_error(p, "expected '{' to start block");
        free_ast(block);
        return NULL;
    }

    while (!check(p, TOKEN_RBRACE) && !is_at_end(p)) {
        ASTNode *stmt = parse_statement(p);
        if (!stmt) {
            if (p->error_message[0] == '\0') {
                parser_set_error(p, "invalid statement in block");
            }
            free_ast(block);
            return NULL;
        }
        stmts = ast_list_append(stmts, stmt);
    }


    if (!match(p, TOKEN_RBRACE)) {
        parser_set_error(p, "expected '}' to close block");
        free_ast(block);
        return NULL;
    }

    block->data.block.statements = stmts;
    return block;
}

static ASTNode *parse_function(Parser *p) {
    Token *name_tok;
    ASTNode *body;
    ASTNode *fn;
    char **tmp_params = NULL;
    size_t param_count = 0;
    size_t param_cap = 0;
    size_t i;

    if (!match(p, TOKEN_FUNC)) {
        parser_set_error(p, "expected 'func' keyword");
        return NULL;
    }
    if (!check(p, TOKEN_IDENTIFIER)) {
        parser_set_error(p, "expected function name after 'func'");
        return NULL;
    }
    name_tok = advance_tok(p);
    if (!match(p, TOKEN_LPAREN)) {
        parser_set_error(p, "expected '(' after function name");
        return NULL;
    }

    if (!check(p, TOKEN_RPAREN)) {
        while (1) {
            char *param_name;
            if (!check(p, TOKEN_IDENTIFIER)) {
                parser_set_error(p, "expected parameter name");
                free(tmp_params);
                return NULL;
            }
            param_name = dup_str(p, advance_tok(p)->value.identifier);
            if (!param_name) {
                free(tmp_params);
                return NULL;
            }

            if (param_count == param_cap) {
                size_t new_cap = (param_cap == 0) ? 4 : param_cap * 2;
                char **new_params = (char **)realloc(tmp_params, sizeof(char *) * new_cap);
                if (!new_params) {
                    free(tmp_params);
                    return NULL;
                }
                tmp_params = new_params;
                param_cap = new_cap;
            }
            tmp_params[param_count++] = param_name;

            if (!match(p, TOKEN_COMMA)) break;
        }
    }

    if (!match(p, TOKEN_RPAREN)) {
        parser_set_error(p, "expected ')' after parameters");
        free(tmp_params);
        return NULL;
    }

    body = parse_block(p);
    if (!body) {
        if (p->error_message[0] == '\0') {
            parser_set_error(p, "expected '{' after function declaration");
        }
        free(tmp_params);
        return NULL;
    }



    fn = create_ast_node(AST_FUNCTION);
    if (!fn) {
        free_ast(body);
        free(tmp_params);
        return NULL;
    }

    fn->data.function.name = dup_str(p, name_tok->value.identifier);
    fn->data.function.param_count = param_count;
    fn->data.function.params = NULL;
    fn->data.function.body = body;

    if (param_count > 0) {
        char **params = p->arena ? (char **)arena_alloc(p->arena, sizeof(char *) * param_count)
                                 : (char **)malloc(sizeof(char *) * param_count);
        if (!params) {
            free_ast(fn);
            free(tmp_params);
            return NULL;
        }
        for (i = 0; i < param_count; i++) {
            params[i] = tmp_params[i];
        }
        fn->data.function.params = params;
    }

    free(tmp_params);
    return fn;
}

static ASTNode *parse_package(Parser *p) {
    Token *id;
    ASTNode *pkg;

    if (!match(p, TOKEN_PACKAGE)) return NULL;
    if (!check(p, TOKEN_IDENTIFIER)) return NULL;
    id = advance_tok(p);

    pkg = create_ast_node(AST_PACKAGE);
    if (!pkg) return NULL;
    pkg->data.package.name = dup_str(p, id->value.identifier);
    (void)consume_statement_end(p);
    return pkg;
}

static int count_tokens(Token *tokens) {
    int c = 0;
    if (!tokens) return 0;
    while (tokens[c].type != TOKEN_EOF) c++;
    return c + 1;
}

Parser* parser_create(Token* tokens) {
    Parser* parser;
    if (!tokens) return NULL;

    parser = (Parser*)malloc(sizeof(Parser));
    if (!parser) return NULL;

    /* error_message/error_line/error_column must start clean: leaving them
     * uninitialized means any parse function that bails out via a bare
     * "return NULL" without calling parser_set_error() leaks whatever
     * garbage bytes happened to be on the heap as the diagnostic message
     * and location (e.g. "hello.hosc:1:1: error H002: @nRx\"). */
    memset(parser, 0, sizeof(Parser));
    parser->error_line = 1;
    parser->error_column = 1;
    parser->error_end_column = 1;

    parser->tokens = tokens;
    parser->current = 0;
    parser->token_count = count_tokens(tokens);
    parser->arena = arena_create(AST_ARENA_DEFAULT_SIZE);
    if (!parser->arena) {
        free(parser);
        return NULL;
    }

    ast_set_arena(parser->arena);
    return parser;
}

void parser_free(Parser* parser) {
    if (!parser) return;
    if (parser->arena) {
        arena_destroy(parser->arena);
        parser->arena = NULL;
    }
    free(parser);
}

ASTNode* parser_parse_program(Parser* p) {
    ASTNode *program = create_ast_node(AST_PROGRAM);
    ASTNodeList *decls = NULL;
    if (!program) return NULL;

    if (check(p, TOKEN_PACKAGE)) {
        program->data.program.package = parse_package(p);
        if (!program->data.program.package) {
            free_ast(program);
            return NULL;
        }
    }

    while (!is_at_end(p)) {
        ASTNode *decl = NULL;
        if (check(p, TOKEN_FUNC)) decl = parse_function(p);
        else decl = parse_statement(p);

        if (!decl) {
            free_ast(program);
            return NULL;
        }

        decls = ast_list_append(decls, decl);
    }

    program->data.program.declarations = decls;
    return program;
}

ASTNode* parser_parse_from_tokens(Parser* parser) { return parser_parse_program(parser); }
ASTNode* parser_parse_expression(Parser* parser) { return parse_expression(parser); }
ASTNode* parser_parse_statement(Parser* parser) { return parse_statement(parser); }

ASTNode* parser_parse(const char* source) {
    Token *tokens = lexer_tokenize(source);
    Parser *parser;
    ASTNode *result;

    g_last_parser_error[0] = '\0';
    g_last_parser_line = 1;
    g_last_parser_column = 1;

    if (!tokens) return NULL;

    parser = parser_create(tokens);
    if (!parser) {
        free_tokens(tokens);
        return NULL;
    }

    ast_set_arena(parser->arena);
    result = parser_parse_program(parser);

    if (result) {
        /* Transfer arena ownership to AST global holder; caller must call ast_release_arena(). */
        parser->arena = NULL;
    } else {
        ast_set_arena(NULL);
        snprintf(g_last_parser_error, sizeof(g_last_parser_error), "%s", parser->error_message);
        g_last_parser_line = parser->error_line > 0 ? parser->error_line : 1;
        g_last_parser_column = parser->error_column > 0 ? parser->error_column : 1;
        g_last_parser_end_column = parser->error_end_column > parser->error_column ? parser->error_end_column : parser->error_column;
    }


    parser_free(parser);
    free_tokens(tokens);

    return result;
}