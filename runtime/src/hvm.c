/*
 * File: runtime\src\hvm.c
 * Purpose: HOSC source file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hvm.h"
#include "hosc_cpp_api.h"
#include "runtime_services.h"
#include "runtime_gui.h"

#define HVM_MAX_GLOBALS 512
#define HVM_MAX_INSTRUCTIONS 65536
#define HVM_GC_INITIAL_THRESHOLD (64u * 1024u)
#include "hvm_gc.c"

#include "runtime_gui.c"
#include "runtime_services.c"

#include "hvm_memory.c"

#include "hvm_opcode.c"

HVM_VM* hvm_create(void) {
    HVM_VM *vm = (HVM_VM*)calloc(1, sizeof(HVM_VM));
    if (!vm) return NULL;

    vm->gc_enabled = 1;
    vm->gc_pending = 0;
    vm->gc_next_collection = HVM_GC_INITIAL_THRESHOLD;
    vm->cpp_api = hosc_api_create(1024 * 1024);
    vm->services = hvm_runtime_services_create_default();
    if (!vm->services) {
        if (vm->cpp_api) hosc_api_destroy(vm->cpp_api);
        free(vm);
        return NULL;
    }
    return vm;
}

void hvm_destroy(HVM_VM* vm) {
    size_t i;
    if (!vm) return;

    for (i = 0; i < vm->stack_top; i++) hvm_free_value(&vm->stack[i]);
    for (i = 0; i < vm->memory_used; i++) hvm_free_value(&vm->memory[i]);

    hvm_clear_instructions(vm);

    for (i = 0; i < vm->string_count; i++) {
        free(vm->strings[i]);
        vm->strings[i] = NULL;
    }

    hvm_gc_destroy_all(vm);

    if (vm->cpp_api) {
        hosc_api_destroy(vm->cpp_api);
        vm->cpp_api = NULL;
    }

    if (vm->error_message) free(vm->error_message);
    if (vm->services) {
        vm->services->gui.shutdown(vm->services);
        hvm_runtime_services_destroy(vm->services);
        vm->services = NULL;
    }
    free(vm->scratch_a);
    free(vm->scratch_b);
    free(vm->scratch_concat);
    free(vm);
}

#include "hvm_stack.c"

#include "hvm_execute.c"

void hvm_gc_collect(HVM_VM* vm) {
    hvm_gc_collect_internal(vm);
}

void hvm_gc_set_enabled(HVM_VM* vm, int enabled) {
    if (!vm) return;
    vm->gc_enabled = enabled ? 1 : 0;
    if (!vm->gc_enabled) {
        vm->gc_pending = 0;
    } else if (vm->gc_bytes >= vm->gc_next_collection) {
        vm->gc_pending = 1;
    }
}

size_t hvm_gc_live_objects(HVM_VM* vm) {
    return vm ? vm->gc_object_count : 0;
}

size_t hvm_gc_live_bytes(HVM_VM* vm) {
    return vm ? vm->gc_bytes : 0;
}
#include "hvm_error.c"

#include "hvm_debug.c"

void hvm_disassemble(HVM_VM* vm) {
    hvm_print_instructions(vm);
}
