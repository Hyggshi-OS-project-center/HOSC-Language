/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_lexer.h
 * Purpose: Fast, deterministic lexer for HOSC source code
 */

#ifndef HOSC_BOOTSTRAP_LEXER_H
#define HOSC_BOOTSTRAP_LEXER_H

#include "bs_token.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *source;
    const char *cursor;
    const char *line_start;
    int line;
    int column;
} BsLexer;

void bs_lexer_init(BsLexer *lexer, const char *source);
BsToken bs_lexer_next_token(BsLexer *lexer);
BsToken bs_lexer_peek_token(BsLexer *lexer);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_LEXER_H */
