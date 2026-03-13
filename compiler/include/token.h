/*
 * File: compiler\include\token.h
 * Purpose: HOSC source file.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

typedef enum {
    TOKEN_UNKNOWN = 0,
    TOKEN_LET,
    TOKEN_VAR,
    TOKEN_FUNC,
    TOKEN_PACKAGE,
    TOKEN_IMPORT,
    TOKEN_PRINT,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_RETURN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_WINDOW,
    TOKEN_TEXT,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_BOOL_TRUE,
    TOKEN_BOOL_FALSE,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_BANG,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_DOT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    int line;
    int column;
    union {
        char *identifier;
        int number;
        double fnumber;
        char *string_lit;
    } value;
} Token;

#ifdef __cplusplus
}
#endif
