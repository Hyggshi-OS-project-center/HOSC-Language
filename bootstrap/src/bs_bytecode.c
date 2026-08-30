/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_bytecode.c
 * Purpose: Bytecode chunk manipulation and disassembly
 */

#include "bs_bytecode.h"
#include <stdio.h>
#include <stdlib.h>

void bs_chunk_init(BsChunk *chunk) {
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->const_count = 0;
    chunk->const_capacity = 0;
}

void bs_chunk_free(BsChunk *chunk) {
    if (!chunk) return;
    if (chunk->code) free(chunk->code);
    if (chunk->lines) free(chunk->lines);
    for (size_t i = 0; i < chunk->const_count; i++) {
        bs_free_value(chunk->constants[i]);
    }
    if (chunk->constants) free(chunk->constants);
    bs_chunk_init(chunk);
}

void bs_chunk_write(BsChunk *chunk, uint8_t byte, int line) {
    if (chunk->count + 1 > chunk->capacity) {
        size_t new_cap = chunk->capacity < 8 ? 8 : chunk->capacity * 2;
        chunk->code = (uint8_t*)realloc(chunk->code, new_cap);
        chunk->lines = (int*)realloc(chunk->lines, sizeof(int) * new_cap);
        chunk->capacity = new_cap;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

size_t bs_chunk_add_constant(BsChunk *chunk, BsValue value) {
    // Check if duplicate constant exists
    for (size_t i = 0; i < chunk->const_count; i++) {
        if (bs_values_equal(chunk->constants[i], value)) {
            return i;
        }
    }

    if (chunk->const_count + 1 > chunk->const_capacity) {
        size_t new_cap = chunk->const_capacity < 8 ? 8 : chunk->const_capacity * 2;
        chunk->constants = (BsValue*)realloc(chunk->constants, sizeof(BsValue) * new_cap);
        chunk->const_capacity = new_cap;
    }
    size_t idx = chunk->const_count;
    chunk->constants[idx] = bs_clone_value(value);
    chunk->const_count++;
    return idx;
}

static size_t simple_instruction(const char *name, size_t offset) {
    printf("%-16s\n", name);
    return offset + 1;
}

static size_t constant_instruction(const char *name, const BsChunk *chunk, size_t offset) {
    uint8_t constant_idx = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant_idx);
    bs_print_value(chunk->constants[constant_idx]);
    printf("'\n");
    return offset + 2;
}

static size_t byte_instruction(const char *name, const BsChunk *chunk, size_t offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static size_t jump_instruction(const char *name, int sign, const BsChunk *chunk, size_t offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4zu -> %zu\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

static size_t disassemble_instruction(const BsChunk *chunk, size_t offset) {
    printf("%04zu ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_HALT: return simple_instruction("OP_HALT", offset);
        case OP_CONSTANT: return constant_instruction("OP_CONSTANT", chunk, offset);
        case OP_NULL: return simple_instruction("OP_NULL", offset);
        case OP_TRUE: return simple_instruction("OP_TRUE", offset);
        case OP_FALSE: return simple_instruction("OP_FALSE", offset);
        case OP_POP: return simple_instruction("OP_POP", offset);
        case OP_ADD: return simple_instruction("OP_ADD", offset);
        case OP_SUB: return simple_instruction("OP_SUB", offset);
        case OP_MUL: return simple_instruction("OP_MUL", offset);
        case OP_DIV: return simple_instruction("OP_DIV", offset);
        case OP_MOD: return simple_instruction("OP_MOD", offset);
        case OP_NEGATE: return simple_instruction("OP_NEGATE", offset);
        case OP_NOT: return simple_instruction("OP_NOT", offset);
        case OP_EQUAL: return simple_instruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL: return simple_instruction("OP_NOT_EQUAL", offset);
        case OP_GREATER: return simple_instruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL: return simple_instruction("OP_GREATER_EQUAL", offset);
        case OP_LESS: return simple_instruction("OP_LESS", offset);
        case OP_LESS_EQUAL: return simple_instruction("OP_LESS_EQUAL", offset);
        case OP_DEFINE_GLOBAL: return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL: return constant_instruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL: return constant_instruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_LOCAL: return byte_instruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL: return byte_instruction("OP_SET_LOCAL", chunk, offset);
        case OP_JUMP: return jump_instruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE: return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP: return jump_instruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL: return byte_instruction("OP_CALL", chunk, offset);
        case OP_CALL_COMMAND: {
            uint8_t const_idx = chunk->code[offset + 1];
            uint8_t argc = chunk->code[offset + 2];
            printf("%-16s %s (argc: %d)\n", "OP_CALL_COMMAND",
                   chunk->constants[const_idx].type == BS_VAL_STRING ? chunk->constants[const_idx].as.string : "<?>", argc);
            return offset + 3;
        }
        case OP_PRINT: return simple_instruction("OP_PRINT", offset);
        case OP_PRINTS: return simple_instruction("OP_PRINTS", offset);
        case OP_RETURN: return simple_instruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void bs_chunk_disassemble(const BsChunk *chunk, const char *name) {
    printf("== %s ==\n", name);
    for (size_t offset = 0; offset < chunk->count;) {
        offset = disassemble_instruction(chunk, offset);
    }
}
