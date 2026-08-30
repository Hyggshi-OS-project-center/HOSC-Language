/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_token.h
 * Purpose: Token definitions and lexical types
 */

#ifndef HOSC_BOOTSTRAP_TOKEN_H
#define HOSC_BOOTSTRAP_TOKEN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BS_TOK_EOF = 0,
    BS_TOK_ERROR,

    /* Literals */
    BS_TOK_IDENTIFIER,
    BS_TOK_NUMBER_INT,
    BS_TOK_NUMBER_FLOAT,
    BS_TOK_STRING,

    /* Keywords */
    BS_TOK_VAR,
    BS_TOK_LET,
    BS_TOK_CONST,
    BS_TOK_FUNC,
    BS_TOK_PACKAGE,
    BS_TOK_IMPORT,
    BS_TOK_PRINT,
    BS_TOK_PRINTS,
    BS_TOK_IF,
    BS_TOK_ELSE,
    BS_TOK_WHILE,
    BS_TOK_FOR,
    BS_TOK_RETURN,
    BS_TOK_BREAK,
    BS_TOK_CONTINUE,
    BS_TOK_TRUE,
    BS_TOK_FALSE,
    BS_TOK_NULL,

    /* Extensible Language Constructs */
    BS_TOK_COMMAND,  /* 'command' keyword */
    BS_TOK_MACRO,    /* 'macro' keyword */
    BS_TOK_SYNTAX,   /* 'syntax' keyword */

    /* Operators & Punctuations */
    BS_TOK_PLUS,         /* + */
    BS_TOK_MINUS,        /* - */
    BS_TOK_STAR,         /* * */
    BS_TOK_SLASH,        /* / */
    BS_TOK_PERCENT,      /* % */
    BS_TOK_ASSIGN,       /* = */
    BS_TOK_PLUS_ASSIGN,  /* += */
    BS_TOK_MINUS_ASSIGN, /* -= */
    BS_TOK_EQUAL_EQUAL,  /* == */
    BS_TOK_BANG_EQUAL,   /* != */
    BS_TOK_LESS,         /* < */
    BS_TOK_LESS_EQUAL,   /* <= */
    BS_TOK_GREATER,      /* > */
    BS_TOK_GREATER_EQUAL,/* >= */
    BS_TOK_AND_AND,      /* && */
    BS_TOK_OR_OR,        /* || */
    BS_TOK_BANG,         /* ! */

    BS_TOK_LPAREN,       /* ( */
    BS_TOK_RPAREN,       /* ) */
    BS_TOK_LBRACE,       /* { */
    BS_TOK_RBRACE,       /* } */
    BS_TOK_LBRACKET,     /* [ */
    BS_TOK_RBRACKET,     /* ] */
    BS_TOK_COMMA,        /* , */
    BS_TOK_SEMICOLON,    /* ; */
    BS_TOK_COLON,        /* : */
    BS_TOK_DOT,          /* . */
    BS_TOK_ARROW         /* -> or => */
} BsTokenType;

typedef struct {
    BsTokenType type;
    const char *start;
    size_t length;
    int line;
    int column;
    union {
        int64_t int_val;
        double float_val;
        char *str_val;
    } as;
} BsToken;

const char* bs_token_type_name(BsTokenType type);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_TOKEN_H */
