/*
 * File: compiler\include\parser.h
 * Purpose: HOSC source file.
 */

#ifndef PARSER_H
#define PARSER_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lexer.h"
#include "ast.h"
#include "arena.h"

#define MAX_PARSE_DEPTH 256

typedef struct {
    Token* tokens;
    int current;
    int token_count;
    Arena* arena;
    int depth;
    char error_message[512];
    int error_line;
    int error_column;
    int error_end_column;
} Parser;

Parser* parser_create(Token* tokens);
void parser_free(Parser* parser);
const char* parser_get_error(Parser* parser);
void parser_set_error(Parser* p, const char* message);
const char* parser_get_last_error(void);
void parser_get_last_error_span(int* line, int* col, int* end_line, int* end_col);
ASTNode* parser_parse(const char* source);


ASTNode* parser_parse_from_tokens(Parser* parser);
ASTNode* parser_parse_program(Parser* parser);
ASTNode* parser_parse_statement(Parser* parser);
ASTNode* parser_parse_expression(Parser* parser);

#ifdef __cplusplus
}
#endif
#endif // PARSER_H
