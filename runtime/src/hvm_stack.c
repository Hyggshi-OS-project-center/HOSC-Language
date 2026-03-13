/*
 * File: runtime\src\hvm_stack.c
 * Purpose: HOSC source file.
 */

int hvm_push_int(HVM_VM* vm, int64_t value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_INT;
    vm->stack[vm->stack_top].data.int_value = value;
    vm->stack_top++;
    return 1;
}

int hvm_push_float(HVM_VM* vm, double value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_FLOAT;
    vm->stack[vm->stack_top].data.float_value = value;
    vm->stack_top++;
    return 1;
}

int hvm_push_string(HVM_VM* vm, const char* value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE || !value) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_STRING;
    vm->stack[vm->stack_top].data.string_value = hvm_gc_strdup(vm, value);
    if (!vm->stack[vm->stack_top].data.string_value) return 0;
    vm->stack_top++;

    if (!vm->running && vm->gc_pending) {
        hvm_gc_collect_internal(vm);
    }

    return 1;
}

int hvm_push_bool(HVM_VM* vm, int value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_BOOL;
    vm->stack[vm->stack_top].data.bool_value = value ? 1 : 0;
    vm->stack_top++;
    return 1;
}

HVM_Value hvm_pop(HVM_VM* vm) {
    HVM_Value r;
    memset(&r, 0, sizeof(r));
    r.type = HVM_TYPE_NULL;
    if (!vm || vm->stack_top == 0) {
        hvm_set_error_msg(vm, "Stack underflow");
        return r;
    }
    vm->stack_top--;
    r = vm->stack[vm->stack_top];
    vm->stack[vm->stack_top].type = HVM_TYPE_NULL;
    vm->stack[vm->stack_top].data.string_value = NULL;
    return r;
}

HVM_Value hvm_peek(HVM_VM* vm, size_t offset) {
    HVM_Value r;
    memset(&r, 0, sizeof(r));
    r.type = HVM_TYPE_NULL;
    if (!vm || vm->stack_top == 0 || offset >= vm->stack_top) {
        hvm_set_error_msg(vm, "Stack underflow");
        return r;
    }
    return vm->stack[vm->stack_top - 1 - offset];
}

