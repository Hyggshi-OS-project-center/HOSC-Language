#ifndef PARSER_H
#define PARSER_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lexer.h"
#include "ast.h"
#include "arena.h"

typedef struct {
    Token* tokens;
    int current;
    int token_count;
    Arena* arena;
} Parser;

Parser* parser_create(Token* tokens);
void parser_free(Parser* parser);
ASTNode* parser_parse(const char* source);
ASTNode* parser_parse_from_tokens(Parser* parser);
ASTNode* parser_parse_program(Parser* parser);
ASTNode* parser_parse_statement(Parser* parser);
ASTNode* parser_parse_expression(Parser* parser);

#ifdef __cplusplus
}
#endif
#endif // PARSER_H
