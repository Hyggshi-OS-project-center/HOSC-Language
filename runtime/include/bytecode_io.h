/* bytecode_io.h - Bytecode serialization helpers for HOSC VM */

#ifndef BYTECODE_IO_H
#define BYTECODE_IO_H

#include <stddef.h>
#include "hvm.h"

#ifdef __cplusplus
extern "C" {
#endif

int hvm_bytecode_write_file(const HVM_Instruction* code, size_t count, const char* path);
HVM_Instruction* hvm_bytecode_read_file(const char* path, size_t* out_count);
HVM_Instruction* hvm_bytecode_parse_buffer(const unsigned char* buffer, size_t size, size_t* out_count);
void hvm_bytecode_free(HVM_Instruction* code, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* BYTECODE_IO_H */
