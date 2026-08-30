/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_interpreter.h
 * Purpose: Tree-walk interpreter / evaluator for AST execution
 */

#ifndef HOSC_BOOTSTRAP_INTERPRETER_H
#define HOSC_BOOTSTRAP_INTERPRETER_H

#include "bs_ast.h"
#include "bs_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BS_EVAL_OK,
    BS_EVAL_RETURN,
    BS_EVAL_BREAK,
    BS_EVAL_CONTINUE,
    BS_EVAL_ERROR
} BsEvalStatus;

typedef struct {
    BsEvalStatus status;
    BsValue value;
} BsEvalResult;

/* Evaluates an AST statement or block in the given runtime environment */
BsEvalResult bs_eval_statement(BsRuntime *runtime, ASTNode *stmt);

/* Evaluates an AST expression in the given runtime environment */
BsEvalResult bs_eval_expression(BsRuntime *runtime, ASTNode *expr);

/* Runs a complete AST program */
BsEvalResult bs_eval_program(BsRuntime *runtime, ASTNode *program);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_INTERPRETER_H */
