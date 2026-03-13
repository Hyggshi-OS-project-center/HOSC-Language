/*
 * File: compiler\src\lexer.c
 * Purpose: HOSC source file.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "lexer.h"

static char *dup_range(const char *start, size_t len) {
    char *s = (char *)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_part(char c) { return isalnum((unsigned char)c) || c == '_'; }

static Token make_simple(TokenType t, int line, int column) {
    Token tk;
    memset(&tk, 0, sizeof(tk));
    tk.type = t;
    tk.line = line;
    tk.column = column;
    return tk;
}

static void free_partial_tokens(Token *tokens, size_t count) {
    size_t i;
    if (!tokens) return;

    for (i = 0; i < count; i++) {
        switch (tokens[i].type) {
            case TOKEN_IDENTIFIER:
                free(tokens[i].value.identifier);
                tokens[i].value.identifier = NULL;
                break;
            case TOKEN_STRING:
                free(tokens[i].value.string_lit);
                tokens[i].value.string_lit = NULL;
                break;
            default:
                break;
        }
    }

    free(tokens);
}

Token* lexer_tokenize(const char* source) {
    size_t cap = 64;
    size_t count = 0;
    Token *tokens;
    const char *p;
    int line = 1;
    int col = 1;

    if (!source) return NULL;
    tokens = (Token *)calloc(cap, sizeof(Token));
    if (!tokens) return NULL;

    p = source;
    while (*p) {
        int tok_line;
        int tok_col;

        if (isspace((unsigned char)*p)) {
            if (*p == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
            p++;
            continue;
        }

        if (count + 2 >= cap) {
            Token *nt;
            cap *= 2;
            nt = (Token *)realloc(tokens, cap * sizeof(Token));
            if (!nt) {
                free_partial_tokens(tokens, count);
                return NULL;
            }
            tokens = nt;
        }

        tok_line = line;
        tok_col = col;

        if (is_ident_start(*p)) {
            const char *start = p;
            size_t len;
            while (is_ident_part(*p)) {
                p++;
                col++;
            }
            len = (size_t)(p - start);
            if (len == 3 && strncmp(start, "let", 3) == 0) tokens[count++] = make_simple(TOKEN_LET, tok_line, tok_col);
            else if (len == 3 && strncmp(start, "var", 3) == 0) tokens[count++] = make_simple(TOKEN_VAR, tok_line, tok_col);
            else if (len == 4 && strncmp(start, "func", 4) == 0) tokens[count++] = make_simple(TOKEN_FUNC, tok_line, tok_col);
            else if (len == 7 && strncmp(start, "package", 7) == 0) tokens[count++] = make_simple(TOKEN_PACKAGE, tok_line, tok_col);
            else if (len == 6 && strncmp(start, "import", 6) == 0) tokens[count++] = make_simple(TOKEN_IMPORT, tok_line, tok_col);
            else if (len == 5 && strncmp(start, "print", 5) == 0) tokens[count++] = make_simple(TOKEN_PRINT, tok_line, tok_col);
            else if (len == 2 && strncmp(start, "if", 2) == 0) tokens[count++] = make_simple(TOKEN_IF, tok_line, tok_col);
            else if (len == 4 && strncmp(start, "else", 4) == 0) tokens[count++] = make_simple(TOKEN_ELSE, tok_line, tok_col);
            else if (len == 5 && strncmp(start, "while", 5) == 0) tokens[count++] = make_simple(TOKEN_WHILE, tok_line, tok_col);
            else if (len == 3 && strncmp(start, "for", 3) == 0) tokens[count++] = make_simple(TOKEN_FOR, tok_line, tok_col);
            else if (len == 6 && strncmp(start, "return", 6) == 0) tokens[count++] = make_simple(TOKEN_RETURN, tok_line, tok_col);
            else if (len == 5 && strncmp(start, "break", 5) == 0) tokens[count++] = make_simple(TOKEN_BREAK, tok_line, tok_col);
            else if (len == 8 && strncmp(start, "continue", 8) == 0) tokens[count++] = make_simple(TOKEN_CONTINUE, tok_line, tok_col);
            else if (len == 6 && strncmp(start, "window", 6) == 0) tokens[count++] = make_simple(TOKEN_WINDOW, tok_line, tok_col);
            else if (len == 4 && strncmp(start, "text", 4) == 0) tokens[count++] = make_simple(TOKEN_TEXT, tok_line, tok_col);
            else if (len == 4 && strncmp(start, "true", 4) == 0) tokens[count++] = make_simple(TOKEN_BOOL_TRUE, tok_line, tok_col);
            else if (len == 5 && strncmp(start, "false", 5) == 0) tokens[count++] = make_simple(TOKEN_BOOL_FALSE, tok_line, tok_col);
            else {
                tokens[count].type = TOKEN_IDENTIFIER;
                tokens[count].line = tok_line;
                tokens[count].column = tok_col;
                tokens[count].value.identifier = dup_range(start, len);
                if (!tokens[count].value.identifier) {
                    free_partial_tokens(tokens, count);
                    return NULL;
                }
                count++;
            }
            continue;
        }

        if (*p == '"') {
            size_t str_cap = 32;
            size_t str_len = 0;
            char *str_buf = (char *)malloc(str_cap);
            if (!str_buf) {
                free_partial_tokens(tokens, count);
                return NULL;
            }

            p++;
            col++;

            while (*p && *p != '"' && *p != '\n' && *p != '\r') {
                char ch = *p;

                if (ch == '\\') {
                    p++;
                    col++;
                    if (!*p) break;

                    switch (*p) {
                        case 'n': ch = '\n'; break;
                        case 'r': ch = '\r'; break;
                        case 't': ch = '\t'; break;
                        case '"': ch = '"'; break;
                        case '\\': ch = '\\'; break;
                        default: ch = *p; break;
                    }
                }

                if (str_len + 1 >= str_cap) {
                    char *new_buf;
                    str_cap *= 2;
                    new_buf = (char *)realloc(str_buf, str_cap);
                    if (!new_buf) {
                        free(str_buf);
                        free_partial_tokens(tokens, count);
                        return NULL;
                    }
                    str_buf = new_buf;
                }

                str_buf[str_len++] = ch;
                p++;
                col++;
            }

            if (*p != '"') {
                fprintf(stderr, "Lexer error (%d:%d): unterminated string literal\n", tok_line, tok_col);
                free(str_buf);
                free_partial_tokens(tokens, count);
                return NULL;
            }

            if (str_len + 1 >= str_cap) {
                char *new_buf = (char *)realloc(str_buf, str_len + 1);
                if (!new_buf) {
                    free(str_buf);
                    free_partial_tokens(tokens, count);
                    return NULL;
                }
                str_buf = new_buf;
            }
            str_buf[str_len] = '\0';

            tokens[count].type = TOKEN_STRING;
            tokens[count].line = tok_line;
            tokens[count].column = tok_col;
            tokens[count].value.string_lit = str_buf;

            p++;
            col++;
            count++;
            continue;
        }
        if (isdigit((unsigned char)*p)) {
            int is_float = 0;
            const char *start = p;
            size_t len;
            char *tmp;

            while (isdigit((unsigned char)*p)) {
                p++;
                col++;
            }

            if (*p == '.') {
                is_float = 1;
                p++;
                col++;
                while (isdigit((unsigned char)*p)) {
                    p++;
                    col++;
                }
            }

            if (*p == 'e' || *p == 'E') {
                const char *exp_ptr = p;
                int exp_col = col;
                const char *q = p + 1;
                if (*q == '+' || *q == '-') q++;
                if (isdigit((unsigned char)*q)) {
                    is_float = 1;
                    p++;
                    col++;
                    if (*p == '+' || *p == '-') {
                        p++;
                        col++;
                    }
                    while (isdigit((unsigned char)*p)) {
                        p++;
                        col++;
                    }
                } else {
                    p = exp_ptr;
                    col = exp_col;
                }
            }

            len = (size_t)(p - start);
            tmp = dup_range(start, len);
            if (!tmp) {
                free_partial_tokens(tokens, count);
                return NULL;
            }

            if (is_float) {
                char *endptr = NULL;
                double fval;
                errno = 0;
                fval = strtod(tmp, &endptr);
                if (errno == ERANGE) {
                    fprintf(stderr, "Lexer error (%d:%d): float literal overflow\n", tok_line, tok_col);
                    free(tmp);
                    free_partial_tokens(tokens, count);
                    return NULL;
                }
                if (!endptr || *endptr != '\0') {
                    fprintf(stderr, "Lexer error (%d:%d): invalid float literal\n", tok_line, tok_col);
                    free(tmp);
                    free_partial_tokens(tokens, count);
                    return NULL;
                }
                tokens[count].type = TOKEN_FLOAT;
                tokens[count].line = tok_line;
                tokens[count].column = tok_col;
                tokens[count].value.fnumber = fval;
            } else {
                char *endptr = NULL;
                long val;
                errno = 0;
                val = strtol(tmp, &endptr, 10);
                if (errno == ERANGE || !endptr || *endptr != '\0' || val > INT_MAX || val < INT_MIN) {
                    fprintf(stderr, "Lexer error (%d:%d): integer literal overflow\n", tok_line, tok_col);
                    free(tmp);
                    free_partial_tokens(tokens, count);
                    return NULL;
                }
                tokens[count].type = TOKEN_NUMBER;
                tokens[count].line = tok_line;
                tokens[count].column = tok_col;
                tokens[count].value.number = (int)val;
            }

            free(tmp);
            count++;
            continue;
        }

        switch (*p) {
            case '=':
                if (*(p + 1) == '=') {
                    tokens[count++] = make_simple(TOKEN_EQUAL_EQUAL, tok_line, tok_col);
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_ASSIGN, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '!':
                if (*(p + 1) == '=') {
                    tokens[count++] = make_simple(TOKEN_BANG_EQUAL, tok_line, tok_col);
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_BANG, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '<':
                if (*(p + 1) == '=') {
                    tokens[count++] = make_simple(TOKEN_LESS_EQUAL, tok_line, tok_col);
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_LESS, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '>':
                if (*(p + 1) == '=') {
                    tokens[count++] = make_simple(TOKEN_GREATER_EQUAL, tok_line, tok_col);
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_GREATER, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '&':
                if (*(p + 1) == '&') {
                    tokens[count++] = make_simple(TOKEN_AND, tok_line, tok_col);
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_UNKNOWN, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '|':
                if (*(p + 1) == '|') {
                    tokens[count++] = make_simple(TOKEN_OR, tok_line, tok_col);
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_UNKNOWN, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '+':
                tokens[count++] = make_simple(TOKEN_PLUS, tok_line, tok_col);
                p++;
                col++;
                break;

            case '-':
                tokens[count++] = make_simple(TOKEN_MINUS, tok_line, tok_col);
                p++;
                col++;
                break;

            case '*':
                tokens[count++] = make_simple(TOKEN_STAR, tok_line, tok_col);
                p++;
                col++;
                break;

            case '%':
                tokens[count++] = make_simple(TOKEN_PERCENT, tok_line, tok_col);
                p++;
                col++;
                break;

            case '/':
                if (*(p + 1) == '/') {
                    p += 2;
                    col += 2;
                    while (*p && *p != '\n' && *p != '\r') {
                        p++;
                        col++;
                    }
                } else if (*(p + 1) == '*') {
                    p += 2;
                    col += 2;
                    while (*p && !(*p == '*' && *(p + 1) == '/')) {
                        if (*p == '\n') {
                            p++;
                            line++;
                            col = 1;
                        } else {
                            p++;
                            col++;
                        }
                    }
                    if (!*p) {
                        fprintf(stderr, "Lexer error (%d:%d): unterminated block comment\n", tok_line, tok_col);
                        free_partial_tokens(tokens, count);
                        return NULL;
                    }
                    p += 2;
                    col += 2;
                } else {
                    tokens[count++] = make_simple(TOKEN_SLASH, tok_line, tok_col);
                    p++;
                    col++;
                }
                break;

            case '.':
                tokens[count++] = make_simple(TOKEN_DOT, tok_line, tok_col);
                p++;
                col++;
                break;

            case '(':
                tokens[count++] = make_simple(TOKEN_LPAREN, tok_line, tok_col);
                p++;
                col++;
                break;

            case ')':
                tokens[count++] = make_simple(TOKEN_RPAREN, tok_line, tok_col);
                p++;
                col++;
                break;

            case '{':
                tokens[count++] = make_simple(TOKEN_LBRACE, tok_line, tok_col);
                p++;
                col++;
                break;

            case '}':
                tokens[count++] = make_simple(TOKEN_RBRACE, tok_line, tok_col);
                p++;
                col++;
                break;

            case ',':
                tokens[count++] = make_simple(TOKEN_COMMA, tok_line, tok_col);
                p++;
                col++;
                break;

            case ';':
                tokens[count++] = make_simple(TOKEN_SEMICOLON, tok_line, tok_col);
                p++;
                col++;
                break;

            default:
                tokens[count++] = make_simple(TOKEN_UNKNOWN, tok_line, tok_col);
                p++;
                col++;
                break;
        }
    }

    tokens[count] = make_simple(TOKEN_EOF, line, col);
    return tokens;
}

void free_tokens(Token *tokens) {
    size_t i = 0;
    if (!tokens) return;
    while (tokens[i].type != TOKEN_EOF) {
        switch (tokens[i].type) {
            case TOKEN_IDENTIFIER:
                if (tokens[i].value.identifier) free(tokens[i].value.identifier);
                break;
            case TOKEN_STRING:
                if (tokens[i].value.string_lit) free(tokens[i].value.string_lit);
                break;
            default:
                break;
        }
        i++;
    }
    free(tokens);
}
