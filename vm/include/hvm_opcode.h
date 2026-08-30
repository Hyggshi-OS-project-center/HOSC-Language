#ifndef HVM_OPCODE_H
#define HVM_OPCODE_H

#include "hosc_bytecode.h"

static inline const char* hvm_opcode_name(HOpcode opcode) {
    switch (opcode) {
        case OP_NOP: return "OP_NOP";
        case OP_CONSTANT: return "OP_CONSTANT";
        case OP_NIL: return "OP_NIL";
        case OP_TRUE: return "OP_TRUE";
        case OP_FALSE: return "OP_FALSE";
        case OP_POP: return "OP_POP";
        case OP_DUP: return "OP_DUP";
        case OP_GET_LOCAL: return "OP_GET_LOCAL";
        case OP_SET_LOCAL: return "OP_SET_LOCAL";
        case OP_GET_GLOBAL: return "OP_GET_GLOBAL";
        case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
        case OP_SET_GLOBAL: return "OP_SET_GLOBAL";
        case OP_ADD: return "OP_ADD";
        case OP_SUB: return "OP_SUB";
        case OP_MUL: return "OP_MUL";
        case OP_DIV: return "OP_DIV";
        case OP_NEGATE: return "OP_NEGATE";
        case OP_NOT: return "OP_NOT";
        case OP_EQ: return "OP_EQ";
        case OP_NE: return "OP_NE";
        case OP_LT: return "OP_LT";
        case OP_LE: return "OP_LE";
        case OP_GT: return "OP_GT";
        case OP_GE: return "OP_GE";
        case OP_JUMP: return "OP_JUMP";
        case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OP_LOOP: return "OP_LOOP";
        case OP_CALL: return "OP_CALL";
        case OP_RETURN: return "OP_RETURN";
        case OP_HALT: return "OP_HALT";
        default: return "OP_UNKNOWN";
    }
}

#endif
