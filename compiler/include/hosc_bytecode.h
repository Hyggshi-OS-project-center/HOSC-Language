#ifndef HOSC_BYTECODE_H
#define HOSC_BYTECODE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HBC_MAGIC "HBC0"
#define HBC_VERSION_MAJOR 0
#define HBC_VERSION_MINOR 1

typedef enum HOpcode {
    OP_NOP = 0,
    OP_CONSTANT = 1,
    OP_NIL = 2,
    OP_TRUE = 3,
    OP_FALSE = 4,
    OP_POP = 5,
    OP_DUP = 6,
    OP_GET_LOCAL = 7,
    OP_SET_LOCAL = 8,
    OP_GET_GLOBAL = 9,
    OP_DEFINE_GLOBAL = 10,
    OP_SET_GLOBAL = 11,
    OP_ADD = 12,
    OP_SUB = 13,
    OP_MUL = 14,
    OP_DIV = 15,
    OP_NEGATE = 16,
    OP_NOT = 17,
    OP_EQ = 18,
    OP_NE = 19,
    OP_LT = 20,
    OP_LE = 21,
    OP_GT = 22,
    OP_GE = 23,
    OP_JUMP = 24,
    OP_JUMP_IF_FALSE = 25,
    OP_LOOP = 26,
    OP_CALL = 27,
    OP_RETURN = 28,
    OP_HALT = 29
} HOpcode;

typedef enum HBCConstTag {
    HBC_CONST_INT = 1,
    HBC_CONST_FLOAT = 2,
    HBC_CONST_STRING = 3
} HBCConstTag;

typedef struct HBCString {
    uint32_t length;
    char* bytes;
} HBCString;

typedef struct HBCConstant {
    uint8_t tag;
    union {
        int64_t int_value;
        double float_value;
        uint32_t string_index;
    } as;
} HBCConstant;

typedef struct HBCGlobalSymbol {
    uint32_t name_string_index;
    uint8_t is_mutable;
    uint8_t reserved[3];
} HBCGlobalSymbol;

typedef struct HBCFunction {
    uint32_t name_string_index;
    uint16_t arity;
    uint16_t local_count;
    uint16_t max_stack;
    uint16_t flags;
    uint32_t code_offset;
    uint32_t code_size;
} HBCFunction;

typedef struct HBCFileHeader {
    char magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t flags;
    uint32_t string_count;
    uint32_t constant_count;
    uint32_t global_count;
    uint32_t function_count;
    uint32_t code_size;
    uint32_t entry_function_index;
} HBCFileHeader;

typedef struct HBytecode {
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t entry_function_index;
    size_t string_count;
    HBCString* strings;
    size_t constant_count;
    HBCConstant* constants;
    size_t global_count;
    HBCGlobalSymbol* globals;
    size_t function_count;
    HBCFunction* functions;
    size_t code_size;
    uint8_t* code;
} HBytecode;

static inline void hbytecode_init(HBytecode* bc) {
    memset(bc, 0, sizeof(*bc));
    bc->version_major = HBC_VERSION_MAJOR;
    bc->version_minor = HBC_VERSION_MINOR;
}

static inline void hbytecode_free(HBytecode* bc) {
    size_t i;
    if (!bc) {
        return;
    }
    for (i = 0; i < bc->string_count; ++i) {
        free(bc->strings[i].bytes);
    }
    free(bc->strings);
    free(bc->constants);
    free(bc->globals);
    free(bc->functions);
    free(bc->code);
    memset(bc, 0, sizeof(*bc));
}

#endif
