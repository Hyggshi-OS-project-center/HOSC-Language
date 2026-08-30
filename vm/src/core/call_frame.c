#include "hvm_api.h"

bool hvm_push_frame(HVM* vm, uint32_t function_index, int argc, HValue* slots) {
    HCallFrame* frame;
    const HBCFunction* function;
    (void)argc;

    if (!vm->loaded_bc || function_index >= vm->loaded_bc->function_count) {
        hvm_set_error(vm, "invalid function index");
        return false;
    }
    if (vm->frame_count >= HVM_FRAMES_MAX) {
        hvm_set_error(vm, "call frame overflow");
        return false;
    }

    function = &vm->loaded_bc->functions[function_index];
    frame = &vm->frames[vm->frame_count++];
    frame->function = function;
    frame->code_base = vm->loaded_bc->code + function->code_offset;
    frame->ip = frame->code_base;
    frame->slots = slots;
    if ((size_t)(slots - vm->stack) + function->local_count > HVM_STACK_MAX) {
        --vm->frame_count;
        hvm_set_error(vm, "local stack overflow");
        return false;
    }
    while (vm->stack_top < slots + function->local_count) {
        *vm->stack_top++ = hvm_value_nil();
    }
    return true;
}

void hvm_pop_frame(HVM* vm) {
    if (vm->frame_count > 0) {
        --vm->frame_count;
    }
}
