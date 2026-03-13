/*
 * File: runtime\src\hvm_error.c
 * Purpose: HOSC source file.
 */

void hvm_set_error(HVM_VM* vm, int code, const char* message) {
    if (!vm) return;
    vm->error_code = code;
    hvm_set_error_msg(vm, message);
}

const char* hvm_get_error(HVM_VM* vm) {
    return vm ? vm->error_message : NULL;
}

void hvm_print_stack(HVM_VM* vm) {
    size_t i;
    if (!vm) return;
    printf("Stack (top=%zu):\n", vm->stack_top);
    for (i = 0; i < vm->stack_top; i++) {
        HVM_Value* v = &vm->stack[i];
        printf("  [%zu]: ", i);
        switch (v->type) {
            case HVM_TYPE_INT: printf("INT %lld", (long long)v->data.int_value); break;
            case HVM_TYPE_FLOAT: printf("FLOAT %g", v->data.float_value); break;
            case HVM_TYPE_STRING: printf("STRING %s", v->data.string_value ? v->data.string_value : ""); break;
            case HVM_TYPE_BOOL: printf("BOOL %s", v->data.bool_value ? "true" : "false"); break;
            default: printf("NULL"); break;
        }
        printf("\n");
    }
}


