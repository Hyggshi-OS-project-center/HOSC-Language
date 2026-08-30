/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_lexer.c
 * Purpose: Lexer implementation with keyword mapping and position tracking
 */

#include "bs_lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char* bs_token_type_name(BsTokenType type) {
    switch (type) {
        case BS_TOK_EOF: return "EOF";
        case BS_TOK_ERROR: return "ERROR";
        case BS_TOK_IDENTIFIER: return "IDENTIFIER";
        case BS_TOK_NUMBER_INT: return "NUMBER_INT";
        case BS_TOK_NUMBER_FLOAT: return "NUMBER_FLOAT";
        case BS_TOK_STRING: return "STRING";
        case BS_TOK_VAR: return "var";
        case BS_TOK_LET: return "let";
        case BS_TOK_CONST: return "const";
        case BS_TOK_FUNC: return "func";
        case BS_TOK_PACKAGE: return "package";
        case BS_TOK_IMPORT: return "import";
        case BS_TOK_PRINT: return "print";
        case BS_TOK_PRINTS: return "prints";
        case BS_TOK_IF: return "if";
        case BS_TOK_ELSE: return "else";
        case BS_TOK_WHILE: return "while";
        case BS_TOK_FOR: return "for";
        case BS_TOK_RETURN: return "return";
        case BS_TOK_BREAK: return "break";
        case BS_TOK_CONTINUE: return "continue";
        case BS_TOK_TRUE: return "true";
        case BS_TOK_FALSE: return "false";
        case BS_TOK_NULL: return "null";
        case BS_TOK_COMMAND: return "command";
        case BS_TOK_MACRO: return "macro";
        case BS_TOK_SYNTAX: return "syntax";
        case BS_TOK_PLUS: return "+";
        case BS_TOK_MINUS: return "-";
        case BS_TOK_STAR: return "*";
        case BS_TOK_SLASH: return "/";
        case BS_TOK_PERCENT: return "%";
        case BS_TOK_ASSIGN: return "=";
        case BS_TOK_PLUS_ASSIGN: return "+=";
        case BS_TOK_MINUS_ASSIGN: return "-=";
        case BS_TOK_EQUAL_EQUAL: return "==";
        case BS_TOK_BANG_EQUAL: return "!=";
        case BS_TOK_LESS: return "<";
        case BS_TOK_LESS_EQUAL: return "<=";
        case BS_TOK_GREATER: return ">";
        case BS_TOK_GREATER_EQUAL: return ">=";
        case BS_TOK_AND_AND: return "&&";
        case BS_TOK_OR_OR: return "||";
        case BS_TOK_BANG: return "!";
        case BS_TOK_LPAREN: return "(";
        case BS_TOK_RPAREN: return ")";
        case BS_TOK_LBRACE: return "{";
        case BS_TOK_RBRACE: return "}";
        case BS_TOK_LBRACKET: return "[";
        case BS_TOK_RBRACKET: return "]";
        case BS_TOK_COMMA: return ",";
        case BS_TOK_SEMICOLON: return ";";
        case BS_TOK_COLON: return ":";
        case BS_TOK_DOT: return ".";
        case BS_TOK_ARROW: return "->";
    }
    return "UNKNOWN";
}

void bs_lexer_init(BsLexer *lexer, const char *source) {
    lexer->source = source ? source : "";
    lexer->cursor = lexer->source;
    lexer->line_start = lexer->source;
    lexer->line = 1;
    lexer->column = 1;
}

static bool is_at_end(BsLexer *lexer) {
    return *lexer->cursor == '\0';
}

static char peek(BsLexer *lexer) {
    return *lexer->cursor;
}

static char peek_next(BsLexer *lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->cursor[1];
}

static char advance(BsLexer *lexer) {
    char c = *lexer->cursor++;
    if (c == '\n') {
        lexer->line++;
        lexer->line_start = lexer->cursor;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static bool match(BsLexer *lexer, char expected) {
    if (is_at_end(lexer) || *lexer->cursor != expected) return false;
    advance(lexer);
    return true;
}

static void skip_whitespace_and_comments(BsLexer *lexer) {
    while (!is_at_end(lexer)) {
        char c = peek(lexer);
        if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
            advance(lexer);
        } else if (c == '/' && peek_next(lexer) == '/') {
            // Single line comment
            while (!is_at_end(lexer) && peek(lexer) != '\n') {
                advance(lexer);
            }
        } else if (c == '/' && peek_next(lexer) == '*') {
            // Block comment
            advance(lexer); // /
            advance(lexer); // *
            while (!is_at_end(lexer)) {
                if (peek(lexer) == '*' && peek_next(lexer) == '/') {
                    advance(lexer);
                    advance(lexer);
                    break;
                }
                advance(lexer);
            }
        } else if (c == '#') {
            // Hash comment support
            while (!is_at_end(lexer) && peek(lexer) != '\n') {
                advance(lexer);
            }
        } else {
            break;
        }
    }
}

static BsToken make_token(BsLexer *lexer, BsTokenType type, const char *start, size_t len, int line, int col) {
    BsToken tok;
    tok.type = type;
    tok.start = start;
    tok.length = len;
    tok.line = line;
    tok.column = col;
    tok.as.int_val = 0;
    return tok;
}

static BsToken make_error_token(BsLexer *lexer, const char *message) {
    BsToken tok;
    tok.type = BS_TOK_ERROR;
    tok.start = message;
    tok.length = strlen(message);
    tok.line = lexer->line;
    tok.column = lexer->column;
    tok.as.int_val = 0;
    return tok;
}

static BsTokenType check_keyword(const char *start, size_t len) {
    switch (len) {
        case 2:
            if (memcmp(start, "if", 2) == 0) return BS_TOK_IF;
            break;
        case 3:
            if (memcmp(start, "var", 3) == 0) return BS_TOK_VAR;
            if (memcmp(start, "let", 3) == 0) return BS_TOK_LET;
            if (memcmp(start, "for", 3) == 0) return BS_TOK_FOR;
            break;
        case 4:
            if (memcmp(start, "func", 4) == 0) return BS_TOK_FUNC;
            if (memcmp(start, "else", 4) == 0) return BS_TOK_ELSE;
            if (memcmp(start, "true", 4) == 0) return BS_TOK_TRUE;
            if (memcmp(start, "null", 4) == 0) return BS_TOK_NULL;
            break;
        case 5:
            if (memcmp(start, "const", 5) == 0) return BS_TOK_CONST;
            if (memcmp(start, "while", 5) == 0) return BS_TOK_WHILE;
            if (memcmp(start, "break", 5) == 0) return BS_TOK_BREAK;
            if (memcmp(start, "false", 5) == 0) return BS_TOK_FALSE;
            if (memcmp(start, "print", 5) == 0) return BS_TOK_PRINT;
            if (memcmp(start, "macro", 5) == 0) return BS_TOK_MACRO;
            break;
        case 6:
            if (memcmp(start, "return", 6) == 0) return BS_TOK_RETURN;
            if (memcmp(start, "import", 6) == 0) return BS_TOK_IMPORT;
            if (memcmp(start, "prints", 6) == 0) return BS_TOK_PRINTS;
            if (memcmp(start, "syntax", 6) == 0) return BS_TOK_SYNTAX;
            break;
        case 7:
            if (memcmp(start, "package", 7) == 0) return BS_TOK_PACKAGE;
            if (memcmp(start, "command", 7) == 0) return BS_TOK_COMMAND;
            break;
        case 8:
            if (memcmp(start, "continue", 8) == 0) return BS_TOK_CONTINUE;
            break;
    }
    return BS_TOK_IDENTIFIER;
}

static BsToken string_literal(BsLexer *lexer, char quote, int start_line, int start_col) {
    const char *start = lexer->cursor;
    while (!is_at_end(lexer) && peek(lexer) != quote) {
        if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer);
        }
        advance(lexer);
    }

    if (is_at_end(lexer)) {
        return make_error_token(lexer, "Unterminated string literal");
    }

    size_t len = (size_t)(lexer->cursor - start);
    advance(lexer); // Consume closing quote

    /* Unescape string content */
    char *buf = (char*)malloc(len + 1);
    size_t out_idx = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            switch (start[i]) {
                case 'n': buf[out_idx++] = '\n'; break;
                case 't': buf[out_idx++] = '\t'; break;
                case 'r': buf[out_idx++] = '\r'; break;
                case '\\': buf[out_idx++] = '\\'; break;
                case '"': buf[out_idx++] = '"'; break;
                case '\'': buf[out_idx++] = '\''; break;
                default: buf[out_idx++] = start[i]; break;
            }
        } else {
            buf[out_idx++] = start[i];
        }
    }
    buf[out_idx] = '\0';

    BsToken tok = make_token(lexer, BS_TOK_STRING, start, len, start_line, start_col);
    tok.as.str_val = buf;
    return tok;
}

static BsToken raw_backtick_string(BsLexer *lexer, int start_line, int start_col) {
    const char *start = lexer->cursor;
    while (!is_at_end(lexer) && peek(lexer) != '`') {
        advance(lexer);
    }
    if (is_at_end(lexer)) {
        return make_error_token(lexer, "Unterminated raw backtick string");
    }
    size_t len = (size_t)(lexer->cursor - start);
    advance(lexer); // consume '`'

    char *buf = (char*)malloc(len + 1);
    memcpy(buf, start, len);
    buf[len] = '\0';

    BsToken tok = make_token(lexer, BS_TOK_STRING, start, len, start_line, start_col);
    tok.as.str_val = buf;
    return tok;
}

static BsToken number_literal(BsLexer *lexer, const char *start, int start_line, int start_col) {
    bool is_float = false;
    while (isdigit((unsigned char)peek(lexer))) {
        advance(lexer);
    }

    if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer))) {
        is_float = true;
        advance(lexer); // consume '.'
        while (isdigit((unsigned char)peek(lexer))) {
            advance(lexer);
        }
    }

    size_t len = (size_t)(lexer->cursor - start);
    char buf[64];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';

    if (is_float) {
        BsToken tok = make_token(lexer, BS_TOK_NUMBER_FLOAT, start, len, start_line, start_col);
        tok.as.float_val = strtod(buf, NULL);
        return tok;
    } else {
        BsToken tok = make_token(lexer, BS_TOK_NUMBER_INT, start, len, start_line, start_col);
        tok.as.int_val = strtoll(buf, NULL, 10);
        return tok;
    }
}

static BsToken identifier(BsLexer *lexer, const char *start, int start_line, int start_col) {
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }

    size_t len = (size_t)(lexer->cursor - start);
    BsTokenType type = check_keyword(start, len);

    BsToken tok = make_token(lexer, type, start, len, start_line, start_col);
    if (type == BS_TOK_IDENTIFIER) {
        char *id_buf = (char*)malloc(len + 1);
        memcpy(id_buf, start, len);
        id_buf[len] = '\0';
        tok.as.str_val = id_buf;
    }
    return tok;
}

BsToken bs_lexer_next_token(BsLexer *lexer) {
    skip_whitespace_and_comments(lexer);

    if (is_at_end(lexer)) {
        return make_token(lexer, BS_TOK_EOF, lexer->cursor, 0, lexer->line, lexer->column);
    }

    int start_line = lexer->line;
    int start_col = lexer->column;
    const char *start = lexer->cursor;
    char c = advance(lexer);

    if (isalpha((unsigned char)c) || c == '_') {
        return identifier(lexer, start, start_line, start_col);
    }

    if (isdigit((unsigned char)c)) {
        return number_literal(lexer, start, start_line, start_col);
    }

    if (c == '"' || c == '\'') {
        return string_literal(lexer, c, start_line, start_col);
    }

    if (c == '`') {
        return raw_backtick_string(lexer, start_line, start_col);
    }

    switch (c) {
        case '+':
            if (match(lexer, '=')) return make_token(lexer, BS_TOK_PLUS_ASSIGN, start, 2, start_line, start_col);
            return make_token(lexer, BS_TOK_PLUS, start, 1, start_line, start_col);
        case '-':
            if (match(lexer, '=')) return make_token(lexer, BS_TOK_MINUS_ASSIGN, start, 2, start_line, start_col);
            if (match(lexer, '>')) return make_token(lexer, BS_TOK_ARROW, start, 2, start_line, start_col);
            return make_token(lexer, BS_TOK_MINUS, start, 1, start_line, start_col);
        case '*': return make_token(lexer, BS_TOK_STAR, start, 1, start_line, start_col);
        case '/': return make_token(lexer, BS_TOK_SLASH, start, 1, start_line, start_col);
        case '%': return make_token(lexer, BS_TOK_PERCENT, start, 1, start_line, start_col);
        case '=':
            if (match(lexer, '=')) return make_token(lexer, BS_TOK_EQUAL_EQUAL, start, 2, start_line, start_col);
            if (match(lexer, '>')) return make_token(lexer, BS_TOK_ARROW, start, 2, start_line, start_col);
            return make_token(lexer, BS_TOK_ASSIGN, start, 1, start_line, start_col);
        case '!':
            if (match(lexer, '=')) return make_token(lexer, BS_TOK_BANG_EQUAL, start, 2, start_line, start_col);
            return make_token(lexer, BS_TOK_BANG, start, 1, start_line, start_col);
        case '<':
            if (match(lexer, '=')) return make_token(lexer, BS_TOK_LESS_EQUAL, start, 2, start_line, start_col);
            return make_token(lexer, BS_TOK_LESS, start, 1, start_line, start_col);
        case '>':
            if (match(lexer, '=')) return make_token(lexer, BS_TOK_GREATER_EQUAL, start, 2, start_line, start_col);
            return make_token(lexer, BS_TOK_GREATER, start, 1, start_line, start_col);
        case '&':
            if (match(lexer, '&')) return make_token(lexer, BS_TOK_AND_AND, start, 2, start_line, start_col);
            break;
        case '|':
            if (match(lexer, '|')) return make_token(lexer, BS_TOK_OR_OR, start, 2, start_line, start_col);
            break;
        case '(': return make_token(lexer, BS_TOK_LPAREN, start, 1, start_line, start_col);
        case ')': return make_token(lexer, BS_TOK_RPAREN, start, 1, start_line, start_col);
        case '{': return make_token(lexer, BS_TOK_LBRACE, start, 1, start_line, start_col);
        case '}': return make_token(lexer, BS_TOK_RBRACE, start, 1, start_line, start_col);
        case '[': return make_token(lexer, BS_TOK_LBRACKET, start, 1, start_line, start_col);
        case ']': return make_token(lexer, BS_TOK_RBRACKET, start, 1, start_line, start_col);
        case ',': return make_token(lexer, BS_TOK_COMMA, start, 1, start_line, start_col);
        case ';': return make_token(lexer, BS_TOK_SEMICOLON, start, 1, start_line, start_col);
        case ':': return make_token(lexer, BS_TOK_COLON, start, 1, start_line, start_col);
        case '.': return make_token(lexer, BS_TOK_DOT, start, 1, start_line, start_col);
    }

    return make_error_token(lexer, "Unexpected character encountered");
}

BsToken bs_lexer_peek_token(BsLexer *lexer) {
    BsLexer copy = *lexer;
    return bs_lexer_next_token(&copy);
}
