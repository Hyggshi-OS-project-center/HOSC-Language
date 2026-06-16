#include "hvm_api.h"

#include <stdlib.h>
#include <string.h>

static bool hvm_opcode_has_u16_operand(HOpcode opcode) {
    switch (opcode) {
        case OP_CONSTANT:
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_GET_GLOBAL:
        case OP_DEFINE_GLOBAL:
        case OP_SET_GLOBAL:
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_LOOP:
        case OP_CALL:
            return true;
        default:
            return false;
    }
}

static bool hvm_validate_function_code(HVM* vm, const HBytecode* bc, const HBCFunction* function) {
    const uint8_t* cursor;
    const uint8_t* end;

    if (function->code_size == 0) {
        hvm_set_error(vm, "function code is empty");
        return false;
    }
    if ((size_t)function->code_offset > bc->code_size ||
        (size_t)function->code_size > bc->code_size - (size_t)function->code_offset) {
        hvm_set_error(vm, "function code range out of bounds");
        return false;
    }

    cursor = bc->code + function->code_offset;
    end = cursor + function->code_size;

    while (cursor < end) {
        HOpcode opcode = (HOpcode)(*cursor++);
        uint16_t operand = 0;

        if (opcode > OP_HALT) {
            hvm_set_error(vm, "invalid opcode in bytecode");
            return false;
        }

        if (hvm_opcode_has_u16_operand(opcode)) {
            if ((size_t)(end - cursor) < 2) {
                hvm_set_error(vm, "truncated bytecode operand");
                return false;
            }
            operand = hvm_read_u16(cursor);
            cursor += 2;
        }

        switch (opcode) {
            case OP_CONSTANT:
                if (operand >= bc->constant_count) {
                    hvm_set_error(vm, "constant index out of range");
                    return false;
                }
                break;
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
                if (operand >= function->local_count) {
                    hvm_set_error(vm, "local index out of range");
                    return false;
                }
                break;
            case OP_GET_GLOBAL:
            case OP_DEFINE_GLOBAL:
            case OP_SET_GLOBAL:
                if (operand >= bc->global_count) {
                    hvm_set_error(vm, "global index out of range");
                    return false;
                }
                break;
            default:
                break;
        }
    }

    return true;
}

static bool hvm_validate_bytecode_module(HVM* vm, const HBytecode* bc) {
    size_t i;

    if (!bc || bc->function_count == 0 || bc->entry_function_index >= bc->function_count) {
        hvm_set_error(vm, "invalid bytecode module");
        return false;
    }
    if ((bc->string_count && !bc->strings) ||
        (bc->constant_count && !bc->constants) ||
        (bc->global_count && !bc->globals) ||
        (bc->function_count && !bc->functions) ||
        (bc->code_size && !bc->code)) {
        hvm_set_error(vm, "missing bytecode section data");
        return false;
    }

    for (i = 0; i < bc->string_count; ++i) {
        if (!bc->strings[i].bytes) {
            hvm_set_error(vm, "invalid string table entry");
            return false;
        }
    }

    for (i = 0; i < bc->constant_count; ++i) {
        switch (bc->constants[i].tag) {
            case HBC_CONST_INT:
            case HBC_CONST_FLOAT:
                break;
            case HBC_CONST_STRING:
                if (bc->constants[i].as.string_index >= bc->string_count) {
                    hvm_set_error(vm, "string constant index out of range");
                    return false;
                }
                break;
            default:
                hvm_set_error(vm, "invalid constant tag");
                return false;
        }
    }

    for (i = 0; i < bc->global_count; ++i) {
        if (bc->globals[i].name_string_index >= bc->string_count) {
            hvm_set_error(vm, "global name index out of range");
            return false;
        }
    }

    for (i = 0; i < bc->function_count; ++i) {
        const HBCFunction* function = &bc->functions[i];
        if (function->name_string_index >= bc->string_count) {
            hvm_set_error(vm, "function name index out of range");
            return false;
        }
        if (function->arity > function->local_count) {
            hvm_set_error(vm, "function arity exceeds local count");
            return false;
        }
        if (function->max_stack > HVM_STACK_MAX) {
            hvm_set_error(vm, "function max stack exceeds VM stack");
            return false;
        }
        if (!hvm_validate_function_code(vm, bc, function)) {
            return false;
        }
    }

    return true;
}

HVM* hvm_create(const HVMConfig* config) {
    HVM* vm;

    vm = (HVM*)calloc(1, sizeof(HVM));
    if (!vm) {
        return NULL;
    }

    vm->stack_top = vm->stack;
    vm->gc_threshold = 1024 * 1024;
    if (config) {
        vm->config = *config;
    } else {
        vm->config.enable_gc = true;
        vm->config.enable_trace = false;
    }
    vm->last_error[0] = '\0';
    hvm_register_builtin_natives(vm);
    return vm;
}

void hvm_destroy(HVM* vm) {
    if (!vm) {
        return;
    }
    free(vm->natives);
    hvm_free_all_objects(vm);
    free(vm);
}

bool hvm_load_bytecode(HVM* vm, const HBytecode* bc) {
    if (!vm || !hvm_validate_bytecode_module(vm, bc)) {
        return false;
    }
    vm->loaded_bc = bc;
    return true;
}

int hvm_execute_entry(HVM* vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    if (!hvm_push_frame(vm, vm->loaded_bc->entry_function_index, 0, vm->stack)) {
        return 1;
    }
    return hvm_interpret_loop(vm);
}

int hvm_execute(HVM* vm, const HBytecode* bc) {
    if (!hvm_load_bytecode(vm, bc)) {
        return 1;
    }
    return hvm_execute_entry(vm);
}

const char* hvm_last_error(HVM* vm) {
    return vm ? vm->last_error : "unknown VM error";
}

void hvm_set_error(HVM* vm, const char* message) {
    if (!vm || !message) {
        return;
    }
    strncpy(vm->last_error, message, sizeof(vm->last_error) - 1);
    vm->last_error[sizeof(vm->last_error) - 1] = '\0';
}
