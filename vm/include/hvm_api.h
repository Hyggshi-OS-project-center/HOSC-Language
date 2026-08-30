#ifndef HVM_API_H
#define HVM_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hosc_bytecode.h"
#include "hvm_loader.h"
#include "hvm_object.h"
#include "hvm_value.h"

#define HVM_STACK_MAX 1024
#define HVM_FRAMES_MAX 256

typedef struct HCallFrame {
    const HBCFunction* function;
    const uint8_t* code_base;
    const uint8_t* ip;
    HValue* slots;
} HCallFrame;

typedef struct HNativeRegistryEntry {
    HNativeObject* object;
} HNativeRegistryEntry;

typedef struct HVMConfig {
    bool enable_gc;
    bool enable_trace;
} HVMConfig;

typedef struct HVM {
    HValue stack[HVM_STACK_MAX];
    HValue* stack_top;

    HCallFrame frames[HVM_FRAMES_MAX];
    int frame_count;

    const HBytecode* loaded_bc;

    HNativeRegistryEntry* natives;
    size_t native_count;
    size_t native_capacity;

    HObject* objects_head;
    size_t bytes_allocated;
    size_t gc_threshold;

    HVMConfig config;
    char last_error[256];
} HVM;

HVM* hvm_create(const HVMConfig* config);
void hvm_destroy(HVM* vm);
bool hvm_register_native(HVM* vm, const char* name, int arity, HNativeFn fn);
bool hvm_load_bytecode(HVM* vm, const HBytecode* bc);
int hvm_execute(HVM* vm, const HBytecode* bc);
int hvm_execute_entry(HVM* vm);
const char* hvm_last_error(HVM* vm);

uint16_t hvm_read_u16(const uint8_t* ip);
void hvm_set_error(HVM* vm, const char* message);
bool hvm_push_frame(HVM* vm, uint32_t function_index, int argc, HValue* slots);
void hvm_pop_frame(HVM* vm);
int hvm_interpret_loop(HVM* vm);
HNativeObject* hvm_lookup_native(HVM* vm, const char* name);
void hvm_register_builtin_natives(HVM* vm);

#endif
