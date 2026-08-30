#include "hvm_api.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t hvm_stack_count(const HVM* vm) {
    return (size_t)(vm->stack_top - vm->stack);
}

static bool hvm_push(HVM* vm, HValue value) {
    if (hvm_stack_count(vm) >= HVM_STACK_MAX) {
        hvm_set_error(vm, "stack overflow");
        return false;
    }
    *vm->stack_top++ = value;
    return true;
}

static bool hvm_pop(HVM* vm, HValue* out_value) {
    if (hvm_stack_count(vm) == 0) {
        hvm_set_error(vm, "stack underflow");
        return false;
    }
    --vm->stack_top;
    if (out_value) {
        *out_value = *vm->stack_top;
    }
    return true;
}

static bool hvm_peek(HVM* vm, int distance, HValue* out_value) {
    if (distance < 0 || hvm_stack_count(vm) <= (size_t)distance) {
        hvm_set_error(vm, "stack access out of range");
        return false;
    }
    if (out_value) {
        *out_value = vm->stack_top[-1 - distance];
    }
    return true;
}

static bool hvm_read_operand_u16(HVM* vm, HCallFrame* frame, const uint8_t* code_end, uint16_t* out_operand) {
    if ((size_t)(code_end - frame->ip) < 2) {
        hvm_set_error(vm, "truncated bytecode operand");
        return false;
    }
    *out_operand = hvm_read_u16(frame->ip);
    frame->ip += 2;
    return true;
}

static bool hvm_concat_values(HVM* vm, HValue left, HValue right, HValue* out) {
    char left_buf[64], right_buf[64];
    const char *left_text = left_buf, *right_text = right_buf;
    size_t left_len, right_len;
    char* combined;
    HStringObject* string;
    if (left.tag == HVAL_OBJ && left.as.object && left.as.object->kind == HOBJ_STRING) left_text = ((HStringObject*)left.as.object)->chars;
    else if (left.tag == HVAL_INT) snprintf(left_buf, sizeof(left_buf), "%lld", (long long)left.as.integer);
    else if (left.tag == HVAL_FLOAT) snprintf(left_buf, sizeof(left_buf), "%g", left.as.floating);
    else return false;
    if (right.tag == HVAL_OBJ && right.as.object && right.as.object->kind == HOBJ_STRING) right_text = ((HStringObject*)right.as.object)->chars;
    else if (right.tag == HVAL_INT) snprintf(right_buf, sizeof(right_buf), "%lld", (long long)right.as.integer);
    else if (right.tag == HVAL_FLOAT) snprintf(right_buf, sizeof(right_buf), "%g", right.as.floating);
    else return false;
    left_len = strlen(left_text); right_len = strlen(right_text);
    combined = (char*)malloc(left_len + right_len + 1);
    if (!combined) return false;
    memcpy(combined, left_text, left_len);
    memcpy(combined + left_len, right_text, right_len + 1);
    string = hvm_string_new(vm, combined, (uint32_t)(left_len + right_len));
    free(combined);
    if (!string) return false;
    *out = hvm_value_object((HObject*)string);
    return true;
}

static int hvm_find_function_index(HVM* vm, const char* name) {
    uint32_t i;

    if (!vm || !vm->loaded_bc || !name) {
        return -1;
    }

    for (i = 0; i < vm->loaded_bc->function_count; ++i) {
        const HBCFunction* function = &vm->loaded_bc->functions[i];
        if (function->name_string_index >= vm->loaded_bc->string_count) {
            continue;
        }
        if (strcmp(vm->loaded_bc->strings[function->name_string_index].bytes, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static bool hvm_call_value(HVM* vm, HValue callee, int argc) {
    HValue result;
    HNativeObject* native_object;
    HValue* argv;

    if (argc < 0 || hvm_stack_count(vm) < (size_t)argc + 1) {
        hvm_set_error(vm, "call stack operands missing");
        return false;
    }

    if (callee.tag == HVAL_INT) {
        uint32_t function_index;
        const HBCFunction* function;
        HValue* slots;

        if (callee.as.integer < 0 || (uint64_t)callee.as.integer >= vm->loaded_bc->function_count) {
            hvm_set_error(vm, "invalid function call target");
            return false;
        }

        function_index = (uint32_t)callee.as.integer;
        function = &vm->loaded_bc->functions[function_index];
        if (function->arity != (uint16_t)argc) {
            hvm_set_error(vm, "function arity mismatch");
            return false;
        }

        slots = vm->stack_top - argc - 1;
        return hvm_push_frame(vm, function_index, argc, slots);
    }

    if (callee.tag != HVAL_OBJ || !callee.as.object || callee.as.object->kind != HOBJ_NATIVE) {
        hvm_set_error(vm, "attempted to call a non-callable value");
        return false;
    }

    native_object = (HNativeObject*)callee.as.object;
    if (native_object->arity >= 0 && native_object->arity != argc) {
        hvm_set_error(vm, "native function arity mismatch");
        return false;
    }

    argv = vm->stack_top - argc;
    if (!native_object->fn(vm, argc, argv, &result)) {
        if (hvm_last_error(vm)[0] == '\0') {
            hvm_set_error(vm, "native function call failed");
        }
        return false;
    }

    vm->stack_top -= (ptrdiff_t)(argc + 1);
    return hvm_push(vm, result);
}

int hvm_interpret_loop(HVM* vm) {
    HCallFrame* frame;
    const uint8_t* code_end;

    while (vm->frame_count > 0) {
        frame = &vm->frames[vm->frame_count - 1];
        code_end = frame->code_base + frame->function->code_size;

        while (frame->ip < code_end) {
            HOpcode opcode = (HOpcode)(*frame->ip++);
            uint16_t operand;
            HValue value;
            const HBCGlobalSymbol* global_symbol;
            const char* global_name;
            HNativeObject* native_object;

            switch (opcode) {
                case OP_CONSTANT:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand)) {
                        return 1;
                    }
                    if (operand >= vm->loaded_bc->constant_count) {
                        hvm_set_error(vm, "constant index out of range");
                        return 1;
                    }
                    switch (vm->loaded_bc->constants[operand].tag) {
                        case HBC_CONST_STRING: {
                            uint32_t string_index = vm->loaded_bc->constants[operand].as.string_index;
                            HBCString constant_string;
                            HStringObject* object;
                            if (string_index >= vm->loaded_bc->string_count) {
                                hvm_set_error(vm, "string constant index out of range");
                                return 1;
                            }
                            constant_string = vm->loaded_bc->strings[string_index];
                            object = hvm_string_new(vm, constant_string.bytes, constant_string.length);
                            if (!object) {
                                hvm_set_error(vm, "failed to allocate string constant");
                                return 1;
                            }
                            if (!hvm_push(vm, hvm_value_object((HObject*)object))) {
                                return 1;
                            }
                            break;
                        }
                        case HBC_CONST_INT:
                            if (!hvm_push(vm, hvm_value_int(vm->loaded_bc->constants[operand].as.int_value))) {
                                return 1;
                            }
                            break;
                        case HBC_CONST_FLOAT:
                            if (!hvm_push(vm, hvm_value_float(vm->loaded_bc->constants[operand].as.float_value))) {
                                return 1;
                            }
                            break;
                        default:
                            hvm_set_error(vm, "unsupported constant tag");
                            return 1;
                    }
                    break;

                case OP_GET_GLOBAL:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand)) {
                        return 1;
                    }
                    if (operand >= vm->loaded_bc->global_count) {
                        hvm_set_error(vm, "global index out of range");
                        return 1;
                    }
                    global_symbol = &vm->loaded_bc->globals[operand];
                    if (global_symbol->name_string_index >= vm->loaded_bc->string_count) {
                        hvm_set_error(vm, "global name index out of range");
                        return 1;
                    }
                    global_name = vm->loaded_bc->strings[global_symbol->name_string_index].bytes;
                    native_object = hvm_lookup_native(vm, global_name);
                    if (native_object) {
                        if (!hvm_push(vm, hvm_value_object((HObject*)native_object))) {
                            return 1;
                        }
                    } else {
                        int function_index = hvm_find_function_index(vm, global_name);
                        if (function_index < 0) {
                            hvm_set_error(vm, "unknown global symbol");
                            return 1;
                        }
                        if (!hvm_push(vm, hvm_value_int(function_index))) {
                            return 1;
                        }
                    }
                    break;

                case OP_GET_LOCAL:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand)) {
                        return 1;
                    }
                    if (operand >= frame->function->local_count) {
                        hvm_set_error(vm, "local index out of range");
                        return 1;
                    }
                    if (!hvm_push(vm, frame->slots[operand])) {
                        return 1;
                    }
                    break;

                case OP_SET_LOCAL:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand)) {
                        return 1;
                    }
                    if (operand >= frame->function->local_count) {
                        hvm_set_error(vm, "local index out of range");
                        return 1;
                    }
                    if (!hvm_peek(vm, 0, &value)) {
                        return 1;
                    }
                    frame->slots[operand] = value;
                    break;

                case OP_TRUE:
                    if (!hvm_push(vm, hvm_value_bool(true))) {
                        return 1;
                    }
                    break;

                case OP_FALSE:
                    if (!hvm_push(vm, hvm_value_bool(false))) {
                        return 1;
                    }
                    break;

                case OP_ADD: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_int(left.as.integer + right.as.integer))) {
                            return 1;
                        }
                    } else if (hvm_concat_values(vm, left, right, &value)) {
                        if (!hvm_push(vm, value)) return 1;
                    } else {
                        hvm_set_error(vm, "ADD requires integers or a string with an integer/string");
                        return 1;
                    }
                    break;
                }

                case OP_SUB: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_int(left.as.integer - right.as.integer))) {
                            return 1;
                        }
                    } else {
                        hvm_set_error(vm, "SUB currently supports integers only");
                        return 1;
                    }
                    break;
                }

                case OP_MUL: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_int(left.as.integer * right.as.integer))) {
                            return 1;
                        }
                    } else {
                        hvm_set_error(vm, "MUL currently supports integers only");
                        return 1;
                    }
                    break;
                }

                case OP_DIV: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT && right.as.integer != 0) {
                        if (!hvm_push(vm, hvm_value_int(left.as.integer / right.as.integer))) {
                            return 1;
                        }
                    } else {
                        hvm_set_error(vm, "DIV currently supports nonzero integers only");
                        return 1;
                    }
                    break;
                }

                case OP_LT: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.integer < right.as.integer))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating < right.as.floating))) return 1;
                    } else if (left.tag == HVAL_INT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool((double)left.as.integer < right.as.floating))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating < (double)right.as.integer))) return 1;
                    } else {
                        hvm_set_error(vm, "LT currently supports numbers only");
                        return 1;
                    }
                    break;
                }

                case OP_LE: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.integer <= right.as.integer))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating <= right.as.floating))) return 1;
                    } else if (left.tag == HVAL_INT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool((double)left.as.integer <= right.as.floating))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating <= (double)right.as.integer))) return 1;
                    } else {
                        hvm_set_error(vm, "LE currently supports numbers only");
                        return 1;
                    }
                    break;
                }

                case OP_GT: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.integer > right.as.integer))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating > right.as.floating))) return 1;
                    } else if (left.tag == HVAL_INT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool((double)left.as.integer > right.as.floating))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating > (double)right.as.integer))) return 1;
                    } else {
                        hvm_set_error(vm, "GT currently supports numbers only");
                        return 1;
                    }
                    break;
                }

                case OP_GE: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (left.tag == HVAL_INT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.integer >= right.as.integer))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating >= right.as.floating))) return 1;
                    } else if (left.tag == HVAL_INT && right.tag == HVAL_FLOAT) {
                        if (!hvm_push(vm, hvm_value_bool((double)left.as.integer >= right.as.floating))) return 1;
                    } else if (left.tag == HVAL_FLOAT && right.tag == HVAL_INT) {
                        if (!hvm_push(vm, hvm_value_bool(left.as.floating >= (double)right.as.integer))) return 1;
                    } else {
                        hvm_set_error(vm, "GE currently supports numbers only");
                        return 1;
                    }
                    break;
                }

                case OP_EQ: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (!hvm_push(vm, hvm_value_bool(hvm_value_equals(left, right)))) {
                        return 1;
                    }
                    break;
                }

                case OP_NE: {
                    HValue right;
                    HValue left;
                    if (!hvm_pop(vm, &right) || !hvm_pop(vm, &left)) {
                        return 1;
                    }
                    if (!hvm_push(vm, hvm_value_bool(!hvm_value_equals(left, right)))) {
                        return 1;
                    }
                    break;
                }

                case OP_JUMP:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand) || operand >= frame->function->code_size) return 1;
                    frame->ip = frame->code_base + operand;
                    break;

                case OP_JUMP_IF_FALSE:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand) || !hvm_pop(vm, &value)) return 1;
                    if (!hvm_value_is_truthy(value)) {
                        if (operand >= frame->function->code_size) { hvm_set_error(vm, "jump target out of range"); return 1; }
                        frame->ip = frame->code_base + operand;
                    }
                    break;

                case OP_NOT:
                    if (!hvm_pop(vm, &value)) {
                        return 1;
                    }
                    if (!hvm_push(vm, hvm_value_bool(!hvm_value_is_truthy(value)))) {
                        return 1;
                    }
                    break;

                case OP_NEGATE:
                    if (!hvm_pop(vm, &value)) {
                        return 1;
                    }
                    if (value.tag != HVAL_INT) {
                        hvm_set_error(vm, "NEGATE currently supports integers only");
                        return 1;
                    }
                    if (!hvm_push(vm, hvm_value_int(-value.as.integer))) {
                        return 1;
                    }
                    break;

                case OP_CALL:
                    if (!hvm_read_operand_u16(vm, frame, code_end, &operand)) {
                        return 1;
                    }
                    if (!hvm_peek(vm, (int)operand, &value)) {
                        return 1;
                    }
                    if (!hvm_call_value(vm, value, (int)operand)) {
                        return 1;
                    }
                    frame = &vm->frames[vm->frame_count - 1];
                    code_end = frame->code_base + frame->function->code_size;
                    break;

                case OP_POP:
                    if (!hvm_pop(vm, NULL)) {
                        return 1;
                    }
                    break;

                case OP_NIL:
                    if (!hvm_push(vm, hvm_value_nil())) {
                        return 1;
                    }
                    break;

                case OP_RETURN:
                    if (!hvm_pop(vm, &value)) {
                        return 1;
                    }
                    {
                        HValue* slots = frame->slots;
                        hvm_pop_frame(vm);
                        vm->stack_top = slots;
                    }
                    if (vm->frame_count == 0) {
                        (void)value;
                        return 0;
                    }
                    if (!hvm_push(vm, value)) {
                        return 1;
                    }
                    frame = &vm->frames[vm->frame_count - 1];
                    code_end = frame->code_base + frame->function->code_size;
                    break;

                case OP_HALT:
                    return 0;

                default:
                    hvm_set_error(vm, "unsupported opcode in bootstrap VM");
                    return 1;
            }
        }
    }

    return 0;
}