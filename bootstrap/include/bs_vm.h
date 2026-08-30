/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_vm.h
 * Purpose: Stack-based Bytecode Virtual Machine with command registry dispatch
 */

#ifndef HOSC_BOOTSTRAP_VM_H
#define HOSC_BOOTSTRAP_VM_H

#include "bs_bytecode.h"
#include "bs_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BS_VM_STACK_MAX 1024

typedef enum {
    VM_RESULT_OK,
    VM_RESULT_COMPILE_ERROR,
    VM_RESULT_RUNTIME_ERROR
} BsVMResult;

typedef struct {
    BsChunk *chunk;
    uint8_t *ip;
    BsValue stack[BS_VM_STACK_MAX];
    BsValue *stack_top;
    BsRuntime *runtime;
} BsVM;

void bs_vm_init(BsVM *vm, BsRuntime *runtime);
void bs_vm_free(BsVM *vm);

void bs_vm_push(BsVM *vm, BsValue value);
BsValue bs_vm_pop(BsVM *vm);
BsValue bs_vm_peek(BsVM *vm, int distance);

BsVMResult bs_vm_run(BsVM *vm, BsChunk *chunk);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_VM_H */
