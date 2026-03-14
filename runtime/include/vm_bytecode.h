/* vm_bytecode.h - C API for HVM bytecode container */
#ifndef VM_BYTECODE_H
#define VM_BYTECODE_H

#include <stddef.h>
#include <stdint.h>
#include "hvm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HVM_Bytecode HVM_Bytecode;

HVM_Bytecode* hvm_bytecode_create(void);
void hvm_bytecode_destroy(HVM_Bytecode* bytecode);
void hvm_bytecode_clear(HVM_Bytecode* bytecode);
size_t hvm_bytecode_count(const HVM_Bytecode* bytecode);
size_t hvm_bytecode_capacity(const HVM_Bytecode* bytecode);
HVM_Instruction* hvm_bytecode_data(HVM_Bytecode* bytecode);
const HVM_Instruction* hvm_bytecode_cdata(const HVM_Bytecode* bytecode);
int hvm_bytecode_add_int(HVM_Bytecode* bytecode, HVM_Opcode opcode, int64_t operand);
int hvm_bytecode_add_float(HVM_Bytecode* bytecode, HVM_Opcode opcode, double operand);
int hvm_bytecode_add_string(HVM_Bytecode* bytecode, HVM_Opcode opcode, const char* operand);
int hvm_bytecode_add_address(HVM_Bytecode* bytecode, HVM_Opcode opcode, size_t address);
int hvm_bytecode_load(HVM_Bytecode* bytecode, const HVM_Instruction* instructions, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* VM_BYTECODE_H */
