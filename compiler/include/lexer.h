/* lexer.h - HOSC source file */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "token.h"

Token *lexer_tokenize(const char *source);
void free_tokens(Token* tokens);
#ifdef __cplusplus
}
#endif
