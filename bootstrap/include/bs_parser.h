/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_parser.h
 * Purpose: Stable recursive-descent parser for HOSC source code
 */

#ifndef HOSC_BOOTSTRAP_PARSER_H
#define HOSC_BOOTSTRAP_PARSER_H

#include "bs_ast.h"
#include "bs_lexer.h"
#include "bs_command_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BsLexer lexer;
    BsToken current;
    BsToken previous;
    bool had_error;
    char error_message[256];
    int error_line;
    int error_column;
} BsParser;

void bs_parser_init(BsParser *parser, const char *source);
ASTNode* bs_parser_parse_program(BsParser *parser);
ASTNode* bs_parser_parse_statement(BsParser *parser);
ASTNode* bs_parser_parse_expression(BsParser *parser);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_PARSER_H */
