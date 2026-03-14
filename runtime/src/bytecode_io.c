/* bytecode_io.c - Bytecode serialization helpers for HOSC VM */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bytecode_io.h"
#include "file_utils.h"

#define MAGIC "HBC1"

typedef enum { OP_NONE=0, OP_INT=1, OP_FLOAT=2, OP_STRING=3 } OpKind;

static int opcode_uses_string_operand(HVM_Opcode opcode) {
    return opcode == HVM_PUSH_STRING ||
           opcode == HVM_STORE_GLOBAL ||
           opcode == HVM_LOAD_GLOBAL ||
           opcode == HVM_CREATE_WINDOW ||
           opcode == HVM_CALL_NATIVE;
}

void hvm_bytecode_free(HVM_Instruction* code, size_t count) {
    size_t i;
    if (!code) return;
    for (i = 0; i < count; i++) {
        if (opcode_uses_string_operand(code[i].opcode) && code[i].operand.string_operand) {
            free(code[i].operand.string_operand);
            code[i].operand.string_operand = NULL;
        }
    }
    free(code);
}

HVM_Instruction* hvm_bytecode_parse_buffer(const unsigned char* buffer, size_t size, size_t* out_count) {
    uint32_t count;
    size_t offset;
    HVM_Instruction* code;
    uint32_t i;

    if (!buffer || !out_count) return NULL;
    *out_count = 0;

    if (size < 8) return NULL;
    if (memcmp(buffer, MAGIC, 4) != 0) return NULL;

    memcpy(&count, buffer + 4, sizeof(uint32_t));
    offset = 8;

    code = (HVM_Instruction*)calloc(count, sizeof(HVM_Instruction));
    if (!code) return NULL;

    for (i = 0; i < count; i++) {
        uint8_t op;
        uint8_t kind;

        if (offset + 2 > size) {
            hvm_bytecode_free(code, i);
            return NULL;
        }

        op = buffer[offset++];
        kind = buffer[offset++];

        code[i].opcode = (HVM_Opcode)op;
        switch (kind) {
            case OP_INT:
                if (offset + sizeof(int64_t) > size) {
                    hvm_bytecode_free(code, i + 1);
                    return NULL;
                }
                memcpy(&code[i].operand.int_operand, buffer + offset, sizeof(int64_t));
                offset += sizeof(int64_t);
                break;

            case OP_FLOAT:
                if (offset + sizeof(double) > size) {
                    hvm_bytecode_free(code, i + 1);
                    return NULL;
                }
                memcpy(&code[i].operand.float_operand, buffer + offset, sizeof(double));
                offset += sizeof(double);
                break;

            case OP_STRING: {
                uint32_t len = 0;
                if (offset + sizeof(uint32_t) > size) {
                    hvm_bytecode_free(code, i + 1);
                    return NULL;
                }
                memcpy(&len, buffer + offset, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                code[i].operand.string_operand = (char*)calloc((size_t)len + 1, 1);
                if (!code[i].operand.string_operand) {
                    hvm_bytecode_free(code, i + 1);
                    return NULL;
                }

                if (offset + len > size) {
                    hvm_bytecode_free(code, i + 1);
                    return NULL;
                }

                if (len > 0) {
                    memcpy(code[i].operand.string_operand, buffer + offset, len);
                }
                code[i].operand.string_operand[len] = '\0';
                offset += len;
                break;
            }

            case OP_NONE:
            default:
                break;
        }
    }

    *out_count = count;
    return code;
}

HVM_Instruction* hvm_bytecode_read_file(const char* path, size_t* out_count) {
    unsigned char* data;
    size_t size;
    HVM_Instruction* code;

    data = hosc_read_file_bytes(path, &size);
    if (!data) return NULL;

    code = hvm_bytecode_parse_buffer(data, size, out_count);
    free(data);
    return code;
}

int hvm_bytecode_write_file(const HVM_Instruction* code, size_t count, const char* path) {
    FILE* fp;
    uint32_t ucount;
    size_t i;

    if (!path) return 0;
    fp = fopen(path, "wb");
    if (!fp) return 0;

    ucount = (uint32_t)count;
    fwrite(MAGIC, 1, 4, fp);
    fwrite(&ucount, sizeof(uint32_t), 1, fp);

    for (i = 0; i < count; i++) {
        const HVM_Instruction* ins = &code[i];
        uint8_t op = (uint8_t)ins->opcode;
        uint8_t kind = OP_NONE;
        fwrite(&op, 1, 1, fp);
        switch (ins->opcode) {
            case HVM_PUSH_INT:
            case HVM_PUSH_BOOL:
            case HVM_JUMP:
            case HVM_JUMP_IF_FALSE:
            case HVM_JUMP_IF_TRUE:
            case HVM_CALL:
            case HVM_STORE:
            case HVM_LOAD:
                kind = OP_INT;
                fwrite(&kind, 1, 1, fp);
                fwrite(&ins->operand.int_operand, sizeof(int64_t), 1, fp);
                break;
            case HVM_PUSH_FLOAT:
                kind = OP_FLOAT;
                fwrite(&kind, 1, 1, fp);
                fwrite(&ins->operand.float_operand, sizeof(double), 1, fp);
                break;
            case HVM_PUSH_STRING:
            case HVM_STORE_GLOBAL:
            case HVM_LOAD_GLOBAL:
            case HVM_CREATE_WINDOW:
            case HVM_CALL_NATIVE:
                kind = OP_STRING;
                fwrite(&kind, 1, 1, fp);
                if (ins->operand.string_operand) {
                    uint32_t len = (uint32_t)strlen(ins->operand.string_operand);
                    fwrite(&len, sizeof(uint32_t), 1, fp);
                    fwrite(ins->operand.string_operand, 1, len, fp);
                } else {
                    uint32_t len = 0;
                    fwrite(&len, sizeof(uint32_t), 1, fp);
                }
                break;
            default:
                kind = OP_NONE;
                fwrite(&kind, 1, 1, fp);
                break;
        }
    }

    fclose(fp);
    return 1;
}
