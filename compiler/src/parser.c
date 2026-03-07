#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "arena.h"

#define AST_ARENA_DEFAULT_SIZE (1024 * 1024)

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

static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p);

static int token_starts_statement(TokenType t) {
    return t == TOKEN_IMPORT ||
           t == TOKEN_LET ||
           t == TOKEN_VAR ||
           t == TOKEN_PRINT ||
           t == TOKEN_IF ||
           t == TOKEN_WHILE ||
           t == TOKEN_FOR ||
           t == TOKEN_RETURN ||
           t == TOKEN_BREAK ||
           t == TOKEN_CONTINUE ||
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

static ASTNode *make_noop(void) {
    ASTNode *n = create_ast_node(AST_BLOCK);
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

static ASTNode *parse_primary(Parser *p) {
    if (match(p, TOKEN_NUMBER)) {
        ASTNode *n = create_ast_node(AST_NUMBER);
        if (!n) return NULL;
        n->data.number.value = p->tokens[p->current - 1].value.number;
        return n;
    }

    if (match(p, TOKEN_FLOAT)) {
        ASTNode *n = create_ast_node(AST_FLOAT);
        if (!n) return NULL;
        n->data.fnumber.value = p->tokens[p->current - 1].value.fnumber;
        return n;
    }

    if (match(p, TOKEN_STRING)) {
        ASTNode *n = create_ast_node(AST_STRING);
        if (!n) return NULL;
        n->data.string_lit.value = dup_str(p, p->tokens[p->current - 1].value.string_lit);
        return n;
    }

    if (match(p, TOKEN_BOOL_TRUE) || match(p, TOKEN_BOOL_FALSE)) {
        Token *t = &p->tokens[p->current - 1];
        ASTNode *n = create_ast_node(AST_BOOL);
        if (!n) return NULL;
        n->data.boolean.value = (t->type == TOKEN_BOOL_TRUE);
        return n;
    }

    if (check(p, TOKEN_IDENTIFIER)) {
        ASTNode *node;
        char *path = parse_identifier_path(p);
        if (!path) return NULL;

        if (match(p, TOKEN_LPAREN)) {
            ASTNodeList *args = NULL;
            if (!check(p, TOKEN_RPAREN)) {
                while (1) {
                    ASTNode *arg = parse_expression(p);
                    if (!arg) return NULL;
                    args = ast_list_append(args, arg);
                    if (!match(p, TOKEN_COMMA)) break;
                }
            }
            if (!match(p, TOKEN_RPAREN)) return NULL;

            node = create_ast_node(AST_CALL_EXPR);
            if (!node) return NULL;
            node->data.call_expr.callee = path;
            node->data.call_expr.arguments = args;
            return node;
        }

        node = create_ast_node(AST_IDENTIFIER);
        if (!node) return NULL;
        node->data.identifier.name = path;
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

static ASTNode *parse_expression(Parser *p) { return parse_logic_or(p); }

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
    return node;
}

static ASTNode *parse_assignment_core(Parser *p, int require_end) {
    Token *id;
    ASTNode *value;
    ASTNode *node;

    if (!check(p, TOKEN_IDENTIFIER)) return NULL;
    id = advance_tok(p);
    if (!match(p, TOKEN_ASSIGN)) {
        p->current--;
        return NULL;
    }

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
    return node;
}

static ASTNode *parse_print(Parser *p) {
    ASTNode *expr;
    ASTNode *node;

    if (match(p, TOKEN_LPAREN)) {
        expr = parse_expression(p);
        if (!expr) return NULL;
        if (!match(p, TOKEN_RPAREN)) {
            free_ast(expr);
            return NULL;
        }
    } else {
        expr = parse_expression(p);
        if (!expr) return NULL;
    }

    if (!consume_statement_end(p)) {
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

static ASTNode *parse_if(Parser *p) {
    ASTNode *cond;
    ASTNode *thenb;
    ASTNode *elseb = NULL;
    ASTNode *node;

    if (!match(p, TOKEN_IF)) return NULL;
    cond = parse_expression(p);
    if (!cond) return NULL;

    thenb = parse_block(p);
    if (!thenb) {
        free_ast(cond);
        return NULL;
    }

    if (match(p, TOKEN_ELSE)) {
        if (check(p, TOKEN_IF)) elseb = parse_if(p);
        else elseb = parse_block(p);
        if (!elseb) {
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

    if (!match(p, TOKEN_FOR)) return NULL;

    if (match(p, TOKEN_LPAREN)) {
        if (!check(p, TOKEN_SEMICOLON)) {
            if (match(p, TOKEN_LET)) init = parse_var_decl_core(p, 0, 0);
            else if (match(p, TOKEN_VAR)) init = parse_var_decl_core(p, 1, 0);
            else if (is_assignment_start(p)) init = parse_assignment_core(p, 0);
            else init = parse_expr_statement_core(p, 0);
            if (!init) return NULL;
        }
        if (!match(p, TOKEN_SEMICOLON)) return NULL;

        if (!check(p, TOKEN_SEMICOLON)) {
            cond = parse_expression(p);
            if (!cond) return NULL;
        }
        if (!match(p, TOKEN_SEMICOLON)) {
            free_ast(init);
            free_ast(cond);
            return NULL;
        }

        if (!check(p, TOKEN_RPAREN)) {
            if (is_assignment_start(p)) update = parse_assignment_core(p, 0);
            else update = parse_expr_statement_core(p, 0);
            if (!update) {
                free_ast(init);
                free_ast(cond);
                return NULL;
            }
        }

        if (!match(p, TOKEN_RPAREN)) {
            free_ast(init);
            free_ast(cond);
            free_ast(update);
            return NULL;
        }
    } else {
        cond = parse_expression(p);
        if (!cond) return NULL;
    }

    body = parse_block(p);
    if (!body) {
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

static ASTNode *parse_statement(Parser *p) {
    if (check(p, TOKEN_IMPORT)) return parse_import(p);

    if (match(p, TOKEN_LET)) return parse_var_decl_core(p, 0, 1);
    if (match(p, TOKEN_VAR)) return parse_var_decl_core(p, 1, 1);

    if (match(p, TOKEN_PRINT)) return parse_print(p);
    if (match(p, TOKEN_RETURN)) return parse_return(p);
    if (match(p, TOKEN_BREAK)) return parse_break_or_continue(p, 0);
    if (match(p, TOKEN_CONTINUE)) return parse_break_or_continue(p, 1);

    if (check(p, TOKEN_IF)) return parse_if(p);
    if (check(p, TOKEN_WHILE)) return parse_while(p);
    if (check(p, TOKEN_FOR)) return parse_for(p);

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
        p->current = save;
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
        free_ast(block);
        return NULL;
    }

    while (!check(p, TOKEN_RBRACE) && !is_at_end(p)) {
        ASTNode *stmt = parse_statement(p);
        if (!stmt) {
            free_ast(block);
            return NULL;
        }
        stmts = ast_list_append(stmts, stmt);
    }

    if (!match(p, TOKEN_RBRACE)) {
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

    if (!match(p, TOKEN_FUNC)) return NULL;
    if (!check(p, TOKEN_IDENTIFIER)) return NULL;
    name_tok = advance_tok(p);
    if (!match(p, TOKEN_LPAREN)) return NULL;

    if (!check(p, TOKEN_RPAREN)) {
        while (1) {
            char *param_name;
            if (!check(p, TOKEN_IDENTIFIER)) {
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
        free(tmp_params);
        return NULL;
    }

    body = parse_block(p);
    if (!body) {
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
    }

    parser_free(parser);
    free_tokens(tokens);

    return result;
}


