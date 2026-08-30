/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_bytecode.h
 * Purpose: Bytecode definitions, opcodes, and chunk structure
 */

#ifndef HOSC_BOOTSTRAP_BYTECODE_H
#define HOSC_BOOTSTRAP_BYTECODE_H

#include "bs_value.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OP_HALT = 0,
    OP_CONSTANT,
    OP_NULL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,

    /* Arithmetic & Logical */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEGATE,
    OP_NOT,

    /* Comparison */
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    /* Variable Operations */
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,

    /* Control Flow */
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    /* Calls & Commands */
    OP_CALL,
    OP_CALL_COMMAND,

    /* Output & Control */
    OP_PRINT,
    OP_PRINTS,
    OP_RETURN
} BsOpCode;

typedef struct {
    uint8_t *code;
    int *lines;
    size_t count;
    size_t capacity;

    BsValue *constants;
    size_t const_count;
    size_t const_capacity;
} BsChunk;

void bs_chunk_init(BsChunk *chunk);
void bs_chunk_free(BsChunk *chunk);
void bs_chunk_write(BsChunk *chunk, uint8_t byte, int line);
size_t bs_chunk_add_constant(BsChunk *chunk, BsValue value);
void bs_chunk_disassemble(const BsChunk *chunk, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_BYTECODE_H */
