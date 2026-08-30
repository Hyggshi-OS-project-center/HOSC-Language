/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_vm.c
 * Purpose: Stack-based Bytecode Virtual Machine
 */

#include "bs_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bs_vm_init(BsVM *vm, BsRuntime *runtime) {
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->stack_top = vm->stack;
    vm->runtime = runtime;
}

void bs_vm_free(BsVM *vm) {
    /* Clear stack */
    while (vm->stack_top > vm->stack) {
        bs_free_value(bs_vm_pop(vm));
    }
}

void bs_vm_push(BsVM *vm, BsValue value) {
    if (vm->stack_top - vm->stack >= BS_VM_STACK_MAX) {
        fprintf(stderr, "[VM] Stack overflow\n");
        return;
    }
    *vm->stack_top = value;
    vm->stack_top++;
}

BsValue bs_vm_pop(BsVM *vm) {
    if (vm->stack_top <= vm->stack) {
        fprintf(stderr, "[VM] Stack underflow\n");
        return bs_null();
    }
    vm->stack_top--;
    return *vm->stack_top;
}

BsValue bs_vm_peek(BsVM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

#define READ_BYTE()    (*vm->ip++)
#define READ_SHORT()   (vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))
#define READ_CONSTANT(idx) (vm->chunk->constants[(idx)])
#define PEEK()         bs_vm_peek(vm, 0)
#define POP()          bs_vm_pop(vm)
#define PUSH(v)        bs_vm_push(vm, v)

static BsVMResult runtime_error(BsVM *vm, const char *msg) {
    int line = vm->chunk->lines[vm->ip - vm->chunk->code - 1];
    fprintf(stderr, "[VM Runtime Error] line %d: %s\n", line, msg);
    if (vm->runtime) {
        vm->runtime->runtime_error = true;
        snprintf(vm->runtime->error_message, sizeof(vm->runtime->error_message), "%s", msg);
    }
    return VM_RESULT_RUNTIME_ERROR;
}

static bool is_string_or_number(BsValue v) {
    return v.type == BS_VAL_INT || v.type == BS_VAL_FLOAT || v.type == BS_VAL_STRING;
}

BsVMResult bs_vm_run(BsVM *vm, BsChunk *chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;

    for (;;) {
        uint8_t instruction = READ_BYTE();

        switch (instruction) {

            case OP_HALT:
                return VM_RESULT_OK;

            case OP_CONSTANT: {
                uint8_t idx = READ_BYTE();
                PUSH(bs_clone_value(READ_CONSTANT(idx)));
                break;
            }

            case OP_NULL:   PUSH(bs_null());       break;
            case OP_TRUE:   PUSH(bs_bool(true));   break;
            case OP_FALSE:  PUSH(bs_bool(false));  break;

            case OP_POP:
                bs_free_value(POP());
                break;

            case OP_NEGATE: {
                BsValue v = POP();
                if (v.type == BS_VAL_INT) {
                    PUSH(bs_int(-v.as.integer));
                } else if (v.type == BS_VAL_FLOAT) {
                    PUSH(bs_float(-v.as.floating));
                } else {
                    bs_free_value(v);
                    return runtime_error(vm, "Operand to '-' must be a number");
                }
                break;
            }

            case OP_NOT: {
                BsValue v = POP();
                bool b = !bs_is_truthy(v);
                bs_free_value(v);
                PUSH(bs_bool(b));
                break;
            }

            case OP_ADD: {
                BsValue b = POP();
                BsValue a = POP();
                if (a.type == BS_VAL_INT && b.type == BS_VAL_INT) {
                    PUSH(bs_int(a.as.integer + b.as.integer));
                } else if (is_string_or_number(a) && is_string_or_number(b)) {
                    /* String concatenation or float add */
                    if (a.type == BS_VAL_STRING || b.type == BS_VAL_STRING) {
                        char buf_a[64], buf_b[64];
                        const char *sa = a.type == BS_VAL_STRING ? a.as.string :
                                         (a.type == BS_VAL_INT ? (snprintf(buf_a, sizeof(buf_a), "%ld", (long)a.as.integer), buf_a) :
                                          (snprintf(buf_a, sizeof(buf_a), "%g", a.as.floating), buf_a));
                        const char *sb = b.type == BS_VAL_STRING ? b.as.string :
                                         (b.type == BS_VAL_INT ? (snprintf(buf_b, sizeof(buf_b), "%ld", (long)b.as.integer), buf_b) :
                                          (snprintf(buf_b, sizeof(buf_b), "%g", b.as.floating), buf_b));
                        size_t la = strlen(sa), lb = strlen(sb);
                        char *res = (char*)malloc(la + lb + 1);
                        memcpy(res, sa, la);
                        memcpy(res + la, sb, lb);
                        res[la + lb] = '\0';
                        bs_free_value(a); bs_free_value(b);
                        PUSH(bs_string_take(res));
                    } else {
                        double da = a.type == BS_VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                        double db = b.type == BS_VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                        bs_free_value(a); bs_free_value(b);
                        PUSH(bs_float(da + db));
                    }
                } else {
                    bs_free_value(a); bs_free_value(b);
                    return runtime_error(vm, "Operands to '+' must be numbers or strings");
                }
                break;
            }

#define BINARY_OP(op_int, op_float) \
    do { \
        BsValue b = POP(); \
        BsValue a = POP(); \
        if (a.type == BS_VAL_INT && b.type == BS_VAL_INT) { \
            PUSH(bs_int(a.as.integer op_int b.as.integer)); \
        } else { \
            double da = a.type == BS_VAL_FLOAT ? a.as.floating : (double)a.as.integer; \
            double db = b.type == BS_VAL_FLOAT ? b.as.floating : (double)b.as.integer; \
            bs_free_value(a); bs_free_value(b); \
            PUSH(bs_float(da op_float db)); \
        } \
    } while(0)

#define CMP_OP(op) \
    do { \
        BsValue b = POP(); \
        BsValue a = POP(); \
        bool res; \
        if (a.type == BS_VAL_INT && b.type == BS_VAL_INT) { \
            res = a.as.integer op b.as.integer; \
        } else { \
            double da = a.type == BS_VAL_FLOAT ? a.as.floating : (double)a.as.integer; \
            double db = b.type == BS_VAL_FLOAT ? b.as.floating : (double)b.as.integer; \
            res = da op db; \
        } \
        bs_free_value(a); bs_free_value(b); \
        PUSH(bs_bool(res)); \
    } while(0)

            case OP_SUB: BINARY_OP(-, -); break;
            case OP_MUL: BINARY_OP(*, *); break;
            case OP_DIV: {
                BsValue b = POP();
                BsValue a = POP();
                double db = b.type == BS_VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                if (db == 0.0) { bs_free_value(a); bs_free_value(b); return runtime_error(vm, "Division by zero"); }
                double da = a.type == BS_VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                bool both_int = a.type == BS_VAL_INT && b.type == BS_VAL_INT;
                bs_free_value(a); bs_free_value(b);
                PUSH(both_int ? bs_int((int64_t)(da / db)) : bs_float(da / db));
                break;
            }
            case OP_MOD: {
                BsValue b = POP();
                BsValue a = POP();
                if (a.type == BS_VAL_INT && b.type == BS_VAL_INT && b.as.integer != 0) {
                    int64_t r = a.as.integer % b.as.integer;
                    bs_free_value(a); bs_free_value(b);
                    PUSH(bs_int(r));
                } else {
                    bs_free_value(a); bs_free_value(b);
                    return runtime_error(vm, "Modulo requires integer operands");
                }
                break;
            }

            case OP_EQUAL: {
                BsValue b = POP();
                BsValue a = POP();
                bool eq = bs_values_equal(a, b);
                bs_free_value(a); bs_free_value(b);
                PUSH(bs_bool(eq));
                break;
            }
            case OP_NOT_EQUAL: {
                BsValue b = POP();
                BsValue a = POP();
                bool neq = !bs_values_equal(a, b);
                bs_free_value(a); bs_free_value(b);
                PUSH(bs_bool(neq));
                break;
            }
            case OP_LESS:           CMP_OP(<);  break;
            case OP_LESS_EQUAL:     CMP_OP(<=); break;
            case OP_GREATER:        CMP_OP(>);  break;
            case OP_GREATER_EQUAL:  CMP_OP(>=); break;

            case OP_DEFINE_GLOBAL: {
                uint8_t idx = READ_BYTE();
                BsValue name_val = READ_CONSTANT(idx);
                BsValue val = POP();
                if (name_val.type == BS_VAL_STRING && vm->runtime) {
                    bs_env_define(vm->runtime->global_env, name_val.as.string, val, false);
                }
                bs_free_value(val);
                break;
            }
            case OP_GET_GLOBAL: {
                uint8_t idx = READ_BYTE();
                BsValue name_val = READ_CONSTANT(idx);
                BsValue result = bs_null();
                if (name_val.type == BS_VAL_STRING && vm->runtime) {
                    if (!bs_env_get(vm->runtime->global_env, name_val.as.string, &result)) {
                        char err[128];
                        snprintf(err, sizeof(err), "Undefined global variable '%s'", name_val.as.string);
                        return runtime_error(vm, err);
                    }
                }
                PUSH(result);
                break;
            }
            case OP_SET_GLOBAL: {
                uint8_t idx = READ_BYTE();
                BsValue name_val = READ_CONSTANT(idx);
                BsValue val = PEEK();
                if (name_val.type == BS_VAL_STRING && vm->runtime) {
                    bs_env_assign(vm->runtime->global_env, name_val.as.string, val);
                }
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                PUSH(bs_clone_value(vm->stack[slot]));
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                bs_free_value(vm->stack[slot]);
                vm->stack[slot] = bs_clone_value(PEEK());
                break;
            }

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vm->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!bs_is_truthy(PEEK())) {
                    vm->ip += offset;
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vm->ip -= offset;
                break;
            }

            case OP_CALL: {
                uint8_t argc = READ_BYTE();
                /* Very simple call: function is on the stack above args.
                   For now, dispatch to runtime interpreter for function execution. */
                (void)argc;
                return runtime_error(vm, "OP_CALL: function-as-value not fully implemented in bytecode VM; use tree-walk interpreter");
            }

            case OP_CALL_COMMAND: {
                uint8_t name_idx = READ_BYTE();
                uint8_t argc = READ_BYTE();
                BsValue name_val = READ_CONSTANT(name_idx);
                if (!vm->runtime || name_val.type != BS_VAL_STRING) {
                    return runtime_error(vm, "OP_CALL_COMMAND: invalid command reference");
                }

                BsCommand *cmd = bs_command_registry_lookup(&vm->runtime->command_registry, name_val.as.string);
                if (!cmd) {
                    char err[128];
                    snprintf(err, sizeof(err), "Unknown command '%s'", name_val.as.string);
                    return runtime_error(vm, err);
                }

                /* Collect arguments from the stack */
                BsValue *args = argc > 0 ? (BsValue*)calloc(argc, sizeof(BsValue)) : NULL;
                for (int i = argc - 1; i >= 0; i--) {
                    args[i] = POP();
                }

                BsValue result = bs_null();
                if (cmd->kind == COMMAND_NATIVE && cmd->native) {
                    result = cmd->native(vm->runtime, args, argc);
                } else {
                    fprintf(stderr, "[VM] AST commands not supported in bytecode VM; use interpreter mode\n");
                }

                for (int i = 0; i < argc; i++) bs_free_value(args[i]);
                if (args) free(args);
                PUSH(result);
                break;
            }

            case OP_PRINT: {
                BsValue v = POP();
                bs_print_value(v);
                printf("\n");
                bs_free_value(v);
                break;
            }
            case OP_PRINTS: {
                BsValue v = POP();
                bs_print_value(v);
                bs_free_value(v);
                break;
            }

            case OP_RETURN: {
                BsValue result = POP();
                bs_print_value(result);
                printf("\n");
                bs_free_value(result);
                return VM_RESULT_OK;
            }

            default:
                return runtime_error(vm, "Unknown opcode");
        }
    }
}
