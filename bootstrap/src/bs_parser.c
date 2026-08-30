/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_parser.c
 * Purpose: Deterministic recursive-descent parser implementation
 */

#include "bs_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(BsParser *parser) {
    parser->previous = parser->current;
    parser->current = bs_lexer_next_token(&parser->lexer);

    if (parser->current.type == BS_TOK_ERROR) {
        parser->had_error = true;
        parser->error_line = parser->current.line;
        parser->error_column = parser->current.column;
        snprintf(parser->error_message, sizeof(parser->error_message),
                 "Lexer error at line %d: %s", parser->current.line, parser->current.start);
    }
}

static bool check(BsParser *parser, BsTokenType type) {
    return parser->current.type == type;
}

static bool match(BsParser *parser, BsTokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static bool consume(BsParser *parser, BsTokenType type, const char *message) {
    if (check(parser, type)) {
        advance(parser);
        return true;
    }
    parser->had_error = true;
    parser->error_line = parser->current.line;
    parser->error_column = parser->current.column;
    snprintf(parser->error_message, sizeof(parser->error_message),
             "Parse error at line %d, col %d: %s (got '%s')",
             parser->current.line, parser->current.column, message,
             bs_token_type_name(parser->current.type));
    return false;
}

void bs_parser_init(BsParser *parser, const char *source) {
    bs_lexer_init(&parser->lexer, source);
    parser->had_error = false;
    parser->error_message[0] = '\0';
    parser->error_line = 1;
    parser->error_column = 1;
    advance(parser);
}

/* Forward declarations */
static ASTNode* statement(BsParser *parser);
static ASTNode* expression(BsParser *parser);
static ASTNode* block_statement(BsParser *parser);

/* Expressions */
static ASTNode* primary(BsParser *parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    if (match(parser, BS_TOK_NULL)) {
        return bs_ast_new_literal(bs_null(), line, col);
    }
    if (match(parser, BS_TOK_TRUE)) {
        return bs_ast_new_literal(bs_bool(true), line, col);
    }
    if (match(parser, BS_TOK_FALSE)) {
        return bs_ast_new_literal(bs_bool(false), line, col);
    }
    if (match(parser, BS_TOK_NUMBER_INT)) {
        return bs_ast_new_literal(bs_int(parser->previous.as.int_val), line, col);
    }
    if (match(parser, BS_TOK_NUMBER_FLOAT)) {
        return bs_ast_new_literal(bs_float(parser->previous.as.float_val), line, col);
    }
    if (match(parser, BS_TOK_STRING)) {
        BsValue v = bs_string(parser->previous.as.str_val);
        if (parser->previous.as.str_val) free(parser->previous.as.str_val);
        return bs_ast_new_literal(v, line, col);
    }
    if (match(parser, BS_TOK_IDENTIFIER)) {
        char *name = parser->previous.as.str_val;
        ASTNode *id_node = bs_ast_new_identifier(name, line, col);
        if (name) free(name);
        return id_node;
    }
    if (match(parser, BS_TOK_LPAREN)) {
        ASTNode *expr = expression(parser);
        consume(parser, BS_TOK_RPAREN, "Expected ')' after grouped expression");
        return expr;
    }

    parser->had_error = true;
    snprintf(parser->error_message, sizeof(parser->error_message),
             "Expected expression at line %d, col %d (got '%s')",
             line, col, bs_token_type_name(parser->current.type));
    return NULL;
}

static ASTNode* call_or_member(BsParser *parser) {
    ASTNode *expr = primary(parser);
    if (!expr) return NULL;

    while (true) {
        if (match(parser, BS_TOK_LPAREN)) {
            int line = parser->previous.line;
            int col = parser->previous.column;
            ASTNodeList args;
            bs_ast_list_init(&args);

            if (!check(parser, BS_TOK_RPAREN)) {
                do {
                    ASTNode *arg = expression(parser);
                    if (arg) {
                        bs_ast_list_append(&args, arg);
                    }
                } while (match(parser, BS_TOK_COMMA));
            }
            consume(parser, BS_TOK_RPAREN, "Expected ')' after function arguments");

            if (expr->type == AST_EXPR_IDENTIFIER) {
                char *callee_name = strdup(expr->as.identifier.name);
                bs_ast_free(expr);
                expr = bs_ast_new_call(callee_name, args, line, col);
                free(callee_name);
            } else {
                /* Non-identifier call */
                expr = bs_ast_new_call("<expr>", args, line, col);
            }
        } else {
            break;
        }
    }

    return expr;
}

static ASTNode* unary(BsParser *parser) {
    if (match(parser, BS_TOK_BANG) || match(parser, BS_TOK_MINUS)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = unary(parser);
        return bs_ast_new_unary(op, right, line, col);
    }
    return call_or_member(parser);
}

static ASTNode* multiplicative(BsParser *parser) {
    ASTNode *expr = unary(parser);
    while (match(parser, BS_TOK_STAR) || match(parser, BS_TOK_SLASH) || match(parser, BS_TOK_PERCENT)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = unary(parser);
        expr = bs_ast_new_binary(op, expr, right, line, col);
    }
    return expr;
}

static ASTNode* additive(BsParser *parser) {
    ASTNode *expr = multiplicative(parser);
    while (match(parser, BS_TOK_PLUS) || match(parser, BS_TOK_MINUS)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = multiplicative(parser);
        expr = bs_ast_new_binary(op, expr, right, line, col);
    }
    return expr;
}

static ASTNode* comparison(BsParser *parser) {
    ASTNode *expr = additive(parser);
    while (match(parser, BS_TOK_GREATER) || match(parser, BS_TOK_GREATER_EQUAL) ||
           match(parser, BS_TOK_LESS) || match(parser, BS_TOK_LESS_EQUAL)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = additive(parser);
        expr = bs_ast_new_binary(op, expr, right, line, col);
    }
    return expr;
}

static ASTNode* equality(BsParser *parser) {
    ASTNode *expr = comparison(parser);
    while (match(parser, BS_TOK_EQUAL_EQUAL) || match(parser, BS_TOK_BANG_EQUAL)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = comparison(parser);
        expr = bs_ast_new_binary(op, expr, right, line, col);
    }
    return expr;
}

static ASTNode* logical_and(BsParser *parser) {
    ASTNode *expr = equality(parser);
    while (match(parser, BS_TOK_AND_AND)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = equality(parser);
        expr = bs_ast_new_binary(op, expr, right, line, col);
    }
    return expr;
}

static ASTNode* logical_or(BsParser *parser) {
    ASTNode *expr = logical_and(parser);
    while (match(parser, BS_TOK_OR_OR)) {
        BsTokenType op = parser->previous.type;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *right = logical_and(parser);
        expr = bs_ast_new_binary(op, expr, right, line, col);
    }
    return expr;
}

static ASTNode* assignment(BsParser *parser) {
    ASTNode *expr = logical_or(parser);

    if (match(parser, BS_TOK_ASSIGN)) {
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *value = assignment(parser);

        if (expr && expr->type == AST_EXPR_IDENTIFIER) {
            char *name = strdup(expr->as.identifier.name);
            bs_ast_free(expr);
            ASTNode *node = bs_ast_new_assign(name, value, line, col);
            free(name);
            return node;
        }
        parser->had_error = true;
        snprintf(parser->error_message, sizeof(parser->error_message),
                 "Invalid assignment target at line %d", line);
        return NULL;
    } else if (match(parser, BS_TOK_PLUS_ASSIGN) || match(parser, BS_TOK_MINUS_ASSIGN)) {
        BsTokenType op = parser->previous.type == BS_TOK_PLUS_ASSIGN ? BS_TOK_PLUS : BS_TOK_MINUS;
        int line = parser->previous.line;
        int col = parser->previous.column;
        ASTNode *value = assignment(parser);

        if (expr && expr->type == AST_EXPR_IDENTIFIER) {
            char *name = strdup(expr->as.identifier.name);
            ASTNode *id_copy = bs_ast_new_identifier(name, line, col);
            ASTNode *bin = bs_ast_new_binary(op, id_copy, value, line, col);
            bs_ast_free(expr);
            ASTNode *node = bs_ast_new_assign(name, bin, line, col);
            free(name);
            return node;
        }
    }

    return expr;
}

static ASTNode* expression(BsParser *parser) {
    return assignment(parser);
}

ASTNode* bs_parser_parse_expression(BsParser *parser) {
    return expression(parser);
}

/* Statements */
static ASTNode* var_declaration(BsParser *parser, bool is_const) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    if (!consume(parser, BS_TOK_IDENTIFIER, "Expected variable name after declaration keyword")) {
        return NULL;
    }

    char *name = parser->previous.as.str_val;
    ASTNode *initializer = NULL;

    if (match(parser, BS_TOK_ASSIGN)) {
        initializer = expression(parser);
    }

    match(parser, BS_TOK_SEMICOLON); // optional semicolon
    ASTNode *node = bs_ast_new_var_decl(name, is_const, initializer, line, col);
    if (name) free(name);
    return node;
}

static ASTNode* block_statement(BsParser *parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    ASTNodeList list;
    bs_ast_list_init(&list);

    while (!check(parser, BS_TOK_RBRACE) && !check(parser, BS_TOK_EOF)) {
        ASTNode *stmt = statement(parser);
        if (stmt) {
            bs_ast_list_append(&list, stmt);
        }
    }

    consume(parser, BS_TOK_RBRACE, "Expected '}' after block");
    return bs_ast_new_block(list, line, col);
}

static ASTNode* if_statement(BsParser *parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    bool has_paren = match(parser, BS_TOK_LPAREN);
    ASTNode *condition = expression(parser);
    if (has_paren) {
        consume(parser, BS_TOK_RPAREN, "Expected ')' after if condition");
    }

    ASTNode *then_branch = statement(parser);
    ASTNode *else_branch = NULL;

    if (match(parser, BS_TOK_ELSE)) {
        else_branch = statement(parser);
    }

    return bs_ast_new_if(condition, then_branch, else_branch, line, col);
}

static ASTNode* while_statement(BsParser *parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    bool has_paren = match(parser, BS_TOK_LPAREN);
    ASTNode *condition = expression(parser);
    if (has_paren) {
        consume(parser, BS_TOK_RPAREN, "Expected ')' after while condition");
    }

    ASTNode *body = statement(parser);
    return bs_ast_new_while(condition, body, line, col);
}

static ASTNode* for_statement(BsParser *parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, BS_TOK_LPAREN, "Expected '(' after 'for'");

    ASTNode *init = NULL;
    if (match(parser, BS_TOK_SEMICOLON)) {
        init = NULL;
    } else if (match(parser, BS_TOK_VAR) || match(parser, BS_TOK_LET)) {
        init = var_declaration(parser, false);
    } else {
        init = bs_ast_new_expr_stmt(expression(parser), line, col);
        consume(parser, BS_TOK_SEMICOLON, "Expected ';' after for loop initializer");
    }

    ASTNode *cond = NULL;
    if (!check(parser, BS_TOK_SEMICOLON)) {
        cond = expression(parser);
    }
    consume(parser, BS_TOK_SEMICOLON, "Expected ';' after for loop condition");

    ASTNode *update = NULL;
    if (!check(parser, BS_TOK_RPAREN)) {
        update = expression(parser);
    }
    consume(parser, BS_TOK_RPAREN, "Expected ')' after for clauses");

    ASTNode *body = statement(parser);
    return bs_ast_new_for(init, cond, update, body, line, col);
}

static ASTNode* func_declaration(BsParser *parser) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    if (!consume(parser, BS_TOK_IDENTIFIER, "Expected function name")) {
        return NULL;
    }
    char *name = parser->previous.as.str_val;

    consume(parser, BS_TOK_LPAREN, "Expected '(' after function name");

    char **param_names = NULL;
    size_t param_count = 0;
    size_t param_cap = 0;

    if (!check(parser, BS_TOK_RPAREN)) {
        do {
            if (consume(parser, BS_TOK_IDENTIFIER, "Expected parameter name")) {
                if (param_count + 1 > param_cap) {
                    param_cap = param_cap < 4 ? 4 : param_cap * 2;
                    param_names = (char**)realloc(param_names, sizeof(char*) * param_cap);
                }
                param_names[param_count++] = parser->previous.as.str_val;
            }
        } while (match(parser, BS_TOK_COMMA));
    }
    consume(parser, BS_TOK_RPAREN, "Expected ')' after parameter list");

    consume(parser, BS_TOK_LBRACE, "Expected '{' before function body");
    ASTNode *body = block_statement(parser);

    ASTNode *node = bs_ast_new_func_decl(name, param_count, param_names, body, line, col);
    if (name) free(name);
    if (param_names) {
        for (size_t i = 0; i < param_count; i++) {
            if (param_names[i]) free(param_names[i]);
        }
        free(param_names);
    }
    return node;
}

/* Extensible Command / Macro / Syntax definition parser */
static ASTNode* extension_declaration(BsParser *parser, bool is_macro) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    if (!consume(parser, BS_TOK_IDENTIFIER, is_macro ? "Expected macro name" : "Expected command name")) {
        return NULL;
    }
    char *name = parser->previous.as.str_val;

    consume(parser, BS_TOK_LPAREN, "Expected '(' after command/macro name");

    char **param_names = NULL;
    size_t param_count = 0;
    size_t param_cap = 0;

    if (!check(parser, BS_TOK_RPAREN)) {
        do {
            if (consume(parser, BS_TOK_IDENTIFIER, "Expected parameter name")) {
                if (param_count + 1 > param_cap) {
                    param_cap = param_cap < 4 ? 4 : param_cap * 2;
                    param_names = (char**)realloc(param_names, sizeof(char*) * param_cap);
                }
                param_names[param_count++] = parser->previous.as.str_val;
            }
        } while (match(parser, BS_TOK_COMMA));
    }
    consume(parser, BS_TOK_RPAREN, "Expected ')' after parameter list");

    ASTNode *body = NULL;
    if (match(parser, BS_TOK_ARROW)) {
        /* Arrow expression body: macro name(...) => expr; */
        ASTNode *expr = expression(parser);
        match(parser, BS_TOK_SEMICOLON);
        body = bs_ast_new_expr_stmt(expr, line, col);
    } else {
        consume(parser, BS_TOK_LBRACE, "Expected '{' before command/macro body");
        body = block_statement(parser);
    }

    ASTNode *node = is_macro
        ? bs_ast_new_macro_def(name, param_count, param_names, body, line, col)
        : bs_ast_new_command_def(name, param_count, param_names, body, line, col);

    if (name) free(name);
    if (param_names) {
        for (size_t i = 0; i < param_count; i++) {
            if (param_names[i]) free(param_names[i]);
        }
        free(param_names);
    }
    return node;
}

static ASTNode* print_statement(BsParser *parser, bool is_raw) {
    int line = parser->previous.line;
    int col = parser->previous.column;

    bool has_paren = match(parser, BS_TOK_LPAREN);
    bool has_bracket = !has_paren && match(parser, BS_TOK_LBRACKET);

    ASTNode *expr = expression(parser);

    if (has_paren) {
        consume(parser, BS_TOK_RPAREN, "Expected ')' after print expression");
    } else if (has_bracket) {
        consume(parser, BS_TOK_RBRACKET, "Expected ']' after prints expression");
    }

    match(parser, BS_TOK_SEMICOLON);
    return bs_ast_new_print(expr, is_raw, !is_raw, line, col);
}

static ASTNode* statement(BsParser *parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    if (match(parser, BS_TOK_VAR) || match(parser, BS_TOK_LET)) {
        return var_declaration(parser, false);
    }
    if (match(parser, BS_TOK_CONST)) {
        return var_declaration(parser, true);
    }
    if (match(parser, BS_TOK_FUNC)) {
        return func_declaration(parser);
    }
    if (match(parser, BS_TOK_COMMAND)) {
        return extension_declaration(parser, false);
    }
    if (match(parser, BS_TOK_MACRO) || match(parser, BS_TOK_SYNTAX)) {
        return extension_declaration(parser, true);
    }
    if (match(parser, BS_TOK_IF)) {
        return if_statement(parser);
    }
    if (match(parser, BS_TOK_WHILE)) {
        return while_statement(parser);
    }
    if (match(parser, BS_TOK_FOR)) {
        return for_statement(parser);
    }
    if (match(parser, BS_TOK_LBRACE)) {
        return block_statement(parser);
    }
    if (match(parser, BS_TOK_RETURN)) {
        ASTNode *val = NULL;
        if (!check(parser, BS_TOK_SEMICOLON)) {
            val = expression(parser);
        }
        match(parser, BS_TOK_SEMICOLON);
        return bs_ast_new_return(val, line, col);
    }
    if (match(parser, BS_TOK_BREAK)) {
        match(parser, BS_TOK_SEMICOLON);
        return bs_ast_new_break(line, col);
    }
    if (match(parser, BS_TOK_CONTINUE)) {
        match(parser, BS_TOK_SEMICOLON);
        return bs_ast_new_continue(line, col);
    }
    if (match(parser, BS_TOK_PRINT)) {
        return print_statement(parser, false);
    }
    if (match(parser, BS_TOK_PRINTS)) {
        return print_statement(parser, true);
    }
    if (match(parser, BS_TOK_PACKAGE)) {
        if (consume(parser, BS_TOK_IDENTIFIER, "Expected package name")) {
            char *pkg = parser->previous.as.str_val;
            match(parser, BS_TOK_SEMICOLON);
            ASTNode *node = bs_ast_new_package(pkg, line, col);
            if (pkg) free(pkg);
            return node;
        }
        return NULL;
    }
    if (match(parser, BS_TOK_IMPORT)) {
        if (consume(parser, BS_TOK_STRING, "Expected import path string")) {
            char *path = parser->previous.as.str_val;
            match(parser, BS_TOK_SEMICOLON);
            ASTNode *node = bs_ast_new_import(path, line, col);
            if (path) free(path);
            return node;
        }
        return NULL;
    }

    /* Check for custom command invocation pattern: ident (args...) [ { body } ] */
    if (check(parser, BS_TOK_IDENTIFIER)) {
        BsToken peek_next = bs_lexer_peek_token(&parser->lexer);
        if (peek_next.type == BS_TOK_LPAREN || peek_next.type == BS_TOK_LBRACE) {
            advance(parser); // consume identifier
            char *cmd_name = parser->previous.as.str_val;
            ASTNodeList args;
            bs_ast_list_init(&args);

            if (match(parser, BS_TOK_LPAREN)) {
                if (!check(parser, BS_TOK_RPAREN)) {
                    do {
                        ASTNode *arg = expression(parser);
                        if (arg) {
                            bs_ast_list_append(&args, arg);
                        }
                    } while (match(parser, BS_TOK_COMMA));
                }
                consume(parser, BS_TOK_RPAREN, "Expected ')' after command arguments");
            }

            ASTNode *body_block = NULL;
            if (match(parser, BS_TOK_LBRACE)) {
                body_block = block_statement(parser);
            }

            match(parser, BS_TOK_SEMICOLON); // optional trailing semicolon

            ASTNode *node = bs_ast_new_custom_call(cmd_name, args, body_block, line, col);
            if (cmd_name) free(cmd_name);
            return node;
        }
    }

    /* Fallback: Expression statement */
    ASTNode *expr = expression(parser);
    match(parser, BS_TOK_SEMICOLON);
    return bs_ast_new_expr_stmt(expr, line, col);
}

ASTNode* bs_parser_parse_statement(BsParser *parser) {
    return statement(parser);
}

ASTNode* bs_parser_parse_program(BsParser *parser) {
    ASTNodeList stmts;
    bs_ast_list_init(&stmts);

    while (!check(parser, BS_TOK_EOF) && !parser->had_error) {
        ASTNode *stmt = statement(parser);
        if (stmt) {
            bs_ast_list_append(&stmts, stmt);
        }
    }

    return bs_ast_new_block(stmts, 1, 1);
}
