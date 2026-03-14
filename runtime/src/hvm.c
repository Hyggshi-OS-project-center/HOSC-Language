/* hvm.c - HOSC source file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hvm.h"
#include "runtime_services.h"
#include "hvm_internal.h"
#include "file_utils.h"
#include "vm_bytecode.h"
#include "vm_gc.h"


#define HVM_MAX_GLOBALS 512
#define HVM_MAX_INSTRUCTIONS 65536
#define HVM_GC_INITIAL_THRESHOLD (64u * 1024u)
static void hvm_gc_collect_internal(HVM_VM* vm) {
    if (!vm || !vm->gc_heap) return;
    hvm_gc_collect_heap((HVM_GcHeap*)vm->gc_heap, vm);
}

static void hvm_gc_destroy_all(HVM_VM* vm) {
    if (!vm || !vm->gc_heap) return;
    hvm_gc_destroy((HVM_GcHeap*)vm->gc_heap);
    vm->gc_heap = NULL;
    vm->gc_object_count = 0;
    vm->gc_bytes = 0;
    vm->gc_pending = 0;
}

static void sync_bytecode_view(HVM_VM* vm) {
    if (!vm || !vm->bytecode) {
        if (vm) {
            vm->instructions = NULL;
            vm->instruction_count = 0;
            vm->instruction_capacity = 0;
        }
        return;
    }
    vm->instructions = hvm_bytecode_data(vm->bytecode);
    vm->instruction_count = hvm_bytecode_count(vm->bytecode);
    vm->instruction_capacity = hvm_bytecode_capacity(vm->bytecode);
}


void hvm_set_error_msg(HVM_VM* vm, const char* msg) {
    if (!vm) return;
    if (vm->error_message) free(vm->error_message);
    vm->error_message = msg ? strdup(msg) : NULL;
}

static void hvm_clear_instructions(HVM_VM* vm) {
    if (!vm || !vm->bytecode) return;
    hvm_bytecode_clear(vm->bytecode);
    sync_bytecode_view(vm);
    vm->pc = 0;
}

static int hvm_is_truthy(HVM_Value v) {
    switch (v.type) {
        case HVM_TYPE_BOOL: return v.data.bool_value != 0;
        case HVM_TYPE_INT: return v.data.int_value != 0;
        case HVM_TYPE_FLOAT: return v.data.float_value != 0.0;
        case HVM_TYPE_STRING: return v.data.string_value && v.data.string_value[0] != '\0';
        default: return 0;
    }
}

static int hvm_is_numeric(HVM_Value v) {
    return v.type == HVM_TYPE_INT || v.type == HVM_TYPE_FLOAT || v.type == HVM_TYPE_BOOL;
}

static double hvm_to_double(HVM_Value v) {
    if (v.type == HVM_TYPE_FLOAT) return v.data.float_value;
    if (v.type == HVM_TYPE_BOOL) return v.data.bool_value ? 1.0 : 0.0;
    return (double)v.data.int_value;
}


static char *hvm_read_text_file(const char *path) {
    char *buffer;
    if (!path || path[0] == '\0') return strdup("");
    buffer = hosc_read_text_file(path, NULL);
    if (!buffer) return strdup("");
    return buffer;
}

static char *hvm_read_text_line(const char *path, int line_no) {
    char *content;
    char *start;
    char *p;
    char *line;
    size_t len;
    int current_line = 1;

    if (line_no < 1) line_no = 1;

    content = hvm_read_text_file(path);
    if (!content) return strdup("");

    start = content;
    p = content;
    while (*p && current_line < line_no) {
        if (*p == '\n') {
            current_line++;
            start = p + 1;
        }
        p++;
    }

    if (current_line != line_no) {
        free(content);
        return strdup("");
    }

    p = start;
    while (*p && *p != '\n' && *p != '\r') p++;

    len = (size_t)(p - start);
    line = (char *)malloc(len + 1);
    if (!line) {
        free(content);
        return strdup("");
    }

    memcpy(line, start, len);
    line[len] = '\0';
    free(content);
    return line;
}

static int hvm_write_text_file(const char *path, const char *text) {
    FILE *fp;
    size_t len;
    size_t written;

    if (!path || path[0] == '\0') return 0;

    fp = fopen(path, "wb");
    if (!fp) return 0;

    if (!text) text = "";
    len = strlen(text);
    written = fwrite(text, 1, len, fp);

    if (fclose(fp) != 0) return 0;
    return written == len;
}

static char *hvm_exec_command(const char *cmd) {
    FILE *pipe;
    char chunk[256];
    char *output;
    size_t len = 0;
    size_t cap = 1;

    if (!cmd || cmd[0] == '\0') return strdup("");

#ifdef _WIN32
    pipe = _popen(cmd, "r");
#else
    pipe = popen(cmd, "r");
#endif
    if (!pipe) return strdup("");

    output = (char *)malloc(cap);
    if (!output) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return strdup("");
    }
    output[0] = '\0';

    while (fgets(chunk, (int)sizeof(chunk), pipe)) {
        size_t c_len = strlen(chunk);
        if (len + c_len + 1 > cap) {
            size_t new_cap = cap;
            char *n;
            while (len + c_len + 1 > new_cap) {
                new_cap *= 2;
            }
            n = (char *)realloc(output, new_cap);
            if (!n) {
                free(output);
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
                return strdup("");
            }
            output = n;
            cap = new_cap;
        }
        memcpy(output + len, chunk, c_len + 1);
        len += c_len;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

HVM_VM* hvm_create(void) {
    HVM_VM *vm = (HVM_VM*)calloc(1, sizeof(HVM_VM));
    if (!vm) return NULL;

    vm->gc_enabled = 1;
    vm->gc_pending = 0;
    vm->gc_next_collection = HVM_GC_INITIAL_THRESHOLD;
    vm->gc_heap = hvm_gc_create();
    if (!vm->gc_heap) {
        if (vm->bytecode) {
            hvm_bytecode_destroy(vm->bytecode);
            vm->bytecode = NULL;
        }
        free(vm);
        return NULL;
    }

    vm->bytecode = hvm_bytecode_create();
    if (!vm->bytecode) {
        hvm_gc_destroy((HVM_GcHeap*)vm->gc_heap);
        vm->gc_heap = NULL;
        free(vm);
        return NULL;
    }
    sync_bytecode_view(vm);

    vm->services = hvm_runtime_services_create_default();
    if (!vm->services) {
        hvm_gc_destroy((HVM_GcHeap*)vm->gc_heap);
        vm->gc_heap = NULL;
        hvm_bytecode_destroy(vm->bytecode);
        vm->bytecode = NULL;
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

    if (vm->error_message) free(vm->error_message);

    free(vm->scratch_a);
    free(vm->scratch_b);
    free(vm->scratch_concat);

    if (vm->services) {
        hvm_runtime_services_destroy(vm->services);
        vm->services = NULL;
    }

    if (vm->bytecode) {
        hvm_bytecode_destroy(vm->bytecode);
        vm->bytecode = NULL;
    }

    free(vm);
}

int hvm_load_bytecode(HVM_VM* vm, const HVM_Instruction* instructions, size_t count) {
    if (!vm || !vm->bytecode || (!instructions && count > 0)) return 0;
    if (count > HVM_MAX_INSTRUCTIONS) return 0;

    hvm_clear_instructions(vm);

    if (!hvm_bytecode_load(vm->bytecode, instructions, count)) return 0;
    sync_bytecode_view(vm);
    vm->pc = 0;
    return 1;
}

int hvm_add_instruction(HVM_VM* vm, HVM_Opcode opcode, int64_t operand) {
    if (!vm || !vm->bytecode) return 0;
    if (!hvm_bytecode_add_int(vm->bytecode, opcode, operand)) return 0;
    sync_bytecode_view(vm);
    return 1;
}

int hvm_add_instruction_float(HVM_VM* vm, HVM_Opcode opcode, double operand) {
    if (!vm || !vm->bytecode) return 0;
    if (!hvm_bytecode_add_float(vm->bytecode, opcode, operand)) return 0;
    sync_bytecode_view(vm);
    return 1;
}

int hvm_add_instruction_string(HVM_VM* vm, HVM_Opcode opcode, const char* operand) {
    if (!vm || !operand || !vm->bytecode) return 0;
    if (!hvm_bytecode_add_string(vm->bytecode, opcode, operand)) return 0;
    sync_bytecode_view(vm);
    return 1;
}

int hvm_add_instruction_address(HVM_VM* vm, HVM_Opcode opcode, size_t address) {
    if (!vm || !vm->bytecode) return 0;
    if (!hvm_bytecode_add_address(vm->bytecode, opcode, address)) return 0;
    sync_bytecode_view(vm);
    return 1;
}

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
    vm->stack[vm->stack_top].data.string_value = hvm_gc_strdup((HVM_GcHeap*)vm->gc_heap, vm, value);
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

static int find_global_index(HVM_VM* vm, const char* name) {
    size_t i;
    for (i = 0; i < vm->string_count; i++) {
        if (vm->strings[i] && strcmp(vm->strings[i], name) == 0) return (int)i;
    }
    return -1;
}

static char *resolve_runtime_name(HVM_VM* vm, const char* name) {
    int needed;
    char *out;
    if (!vm || !name) return NULL;
    if (strncmp(name, "__", 2) != 0 || vm->call_top == 0) return NULL;
    needed = snprintf(NULL, 0, "%s#%zu", name, vm->call_top);
    if (needed < 0) return NULL;
    out = (char *)malloc((size_t)needed + 1);
    if (!out) return NULL;
    snprintf(out, (size_t)needed + 1, "%s#%zu", name, vm->call_top);
    return out;
}

static int store_global(HVM_VM* vm, const char* name, HVM_Value value) {
    int idx;
    char *resolved_name;
    const char *key;

    if (!vm || !name) return 0;
    resolved_name = resolve_runtime_name(vm, name);
    key = resolved_name ? resolved_name : name;

    idx = find_global_index(vm, key);
    if (idx >= 0) {
        hvm_free_value(&vm->memory[idx]);
        vm->memory[idx] = value;
        free(resolved_name);
        return 1;
    }

    if (vm->string_count >= HVM_MAX_GLOBALS || vm->memory_used >= HVM_MEMORY_SIZE) {
        free(resolved_name);
        return 0;
    }

    vm->strings[vm->string_count] = strdup(key);
    if (!vm->strings[vm->string_count]) {
        free(resolved_name);
        return 0;
    }

    vm->memory[vm->string_count] = value;
    vm->string_count++;
    vm->memory_used++;
    free(resolved_name);
    return 1;
}

static int load_global(HVM_VM* vm, const char* name, HVM_Value* out) {
    int idx;
    char *resolved_name;
    const char *key;

    if (!vm || !name || !out) return 0;
    resolved_name = resolve_runtime_name(vm, name);
    key = resolved_name ? resolved_name : name;

    idx = find_global_index(vm, key);
    free(resolved_name);
    if (idx < 0) return 0;

    *out = vm->memory[idx];
    return 1;
}

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

void hvm_print_instructions(HVM_VM* vm) {
    size_t i;
    if (!vm || !vm->instructions) return;
    printf("Instructions:\n");
    for (i = 0; i < vm->instruction_count; i++) {
        HVM_Instruction* instr = &vm->instructions[i];
        printf("  [%zu]: opcode=%d\n", i, instr->opcode);
    }
}

void hvm_disassemble(HVM_VM* vm) {
    hvm_print_instructions(vm);
}



















































































/* Opcode dispatch table */
typedef int (*HVM_OpcodeHandler)(HVM_VM *vm, const HVM_Instruction *instr);

static int op_push_int(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_int(vm, instr->operand.int_operand)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int op_push_float(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_float(vm, instr->operand.float_operand)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int op_push_string(HVM_VM *vm, const HVM_Instruction *instr) {
    const char *value = instr->operand.string_operand ? instr->operand.string_operand : "";
    if (!hvm_push_string(vm, value)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int op_push_bool(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_bool(vm, (int)instr->operand.int_operand)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int op_pop(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value dropped;
    (void)instr;
    if (vm->stack_top < 1) {
        hvm_set_error_msg(vm, "Stack underflow in POP");
        vm->running = 0;
        return 1;
    }
    dropped = hvm_pop(vm);
    hvm_free_value(&dropped);
    return 1;
}

static int op_add(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value a, b;
    (void)instr;
    if (vm->stack_top < 2) {
        hvm_set_error_msg(vm, "Stack underflow in ADD");
        vm->running = 0;
        return 1;
    }
    b = hvm_pop(vm);
    a = hvm_pop(vm);

    if (a.type == HVM_TYPE_STRING || b.type == HVM_TYPE_STRING) {
        const char *sa = hvm_value_to_cstring_a(vm, a);
        const char *sb = hvm_value_to_cstring_b(vm, b);
        const char *out = NULL;
        if (!sa || !sb) {
            hvm_set_error_msg(vm, "Out of memory in string concat");
            vm->running = 0;
            hvm_free_value(&a);
            hvm_free_value(&b);
            return 1;
        }
        out = hvm_concat_cstrings(vm, sa, sb);
        if (!out) {
            hvm_set_error_msg(vm, "Out of memory in string concat");
            vm->running = 0;
            hvm_free_value(&a);
            hvm_free_value(&b);
            return 1;
        }
        if (!hvm_push_string(vm, out)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
        hvm_free_value(&a);
        hvm_free_value(&b);
        return 1;
    }

    if (!hvm_is_numeric(a) || !hvm_is_numeric(b)) {
        hvm_set_error_msg(vm, "ADD requires numeric or string operands");
        vm->running = 0;
        hvm_free_value(&a);
        hvm_free_value(&b);
        return 1;
    }

    if (a.type == HVM_TYPE_FLOAT || b.type == HVM_TYPE_FLOAT) {
        if (!hvm_push_float(vm, hvm_to_double(a) + hvm_to_double(b))) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    } else {
        if (!hvm_push_int(vm, (int64_t)(hvm_to_double(a) + hvm_to_double(b)))) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    }
    hvm_free_value(&a);
    hvm_free_value(&b);
    return 1;
}

static int op_arithmetic(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value a, b;
    double av, bv, rv;
    if (vm->stack_top < 2) {
        hvm_set_error_msg(vm, "Stack underflow in arithmetic op");
        vm->running = 0;
        return 1;
    }
    b = hvm_pop(vm);
    a = hvm_pop(vm);

    if (!hvm_is_numeric(a) || !hvm_is_numeric(b)) {
        hvm_set_error_msg(vm, "Arithmetic ops require numeric operands");
        vm->running = 0;
        hvm_free_value(&a);
        hvm_free_value(&b);
        return 1;
    }

    av = hvm_to_double(a);
    bv = hvm_to_double(b);

    if (instr->opcode == HVM_SUB) {
        rv = av - bv;
    } else if (instr->opcode == HVM_MUL) {
        rv = av * bv;
    } else if (instr->opcode == HVM_DIV) {
        if (bv == 0.0) {
            hvm_set_error_msg(vm, "Divide by zero");
            vm->running = 0;
            hvm_free_value(&a);
            hvm_free_value(&b);
            return 1;
        }
        rv = av / bv;
    } else {
        int64_t ai;
        int64_t bi;
        if (bv == 0.0) {
            hvm_set_error_msg(vm, "Modulo by zero");
            vm->running = 0;
            hvm_free_value(&a);
            hvm_free_value(&b);
            return 1;
        }
        ai = (int64_t)av;
        bi = (int64_t)bv;
        rv = (double)(ai % bi);
    }

    if (a.type == HVM_TYPE_FLOAT || b.type == HVM_TYPE_FLOAT || instr->opcode == HVM_DIV) {
        if (!hvm_push_float(vm, rv)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    } else {
        if (!hvm_push_int(vm, (int64_t)rv)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    }

    hvm_free_value(&a);
    hvm_free_value(&b);
    return 1;
}

static int op_compare(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value a, b;
    int result = 0;
    if (vm->stack_top < 2) {
        hvm_set_error_msg(vm, "Stack underflow in comparison");
        vm->running = 0;
        return 1;
    }
    b = hvm_pop(vm);
    a = hvm_pop(vm);

    if ((a.type == HVM_TYPE_STRING || b.type == HVM_TYPE_STRING) &&
        (instr->opcode == HVM_LT || instr->opcode == HVM_LE || instr->opcode == HVM_GT || instr->opcode == HVM_GE)) {
        hvm_set_error_msg(vm, "Ordering comparison does not support strings");
        vm->running = 0;
        hvm_free_value(&a);
        hvm_free_value(&b);
        return 1;
    }

    if (a.type == HVM_TYPE_STRING || b.type == HVM_TYPE_STRING) {
        const char *sa = (a.type == HVM_TYPE_STRING && a.data.string_value) ? a.data.string_value : "";
        const char *sb = (b.type == HVM_TYPE_STRING && b.data.string_value) ? b.data.string_value : "";
        int cmp = strcmp(sa, sb);
        result = (instr->opcode == HVM_EQ) ? (cmp == 0) : (cmp != 0);
    } else {
        double av = hvm_to_double(a);
        double bv = hvm_to_double(b);
        switch (instr->opcode) {
            case HVM_EQ: result = (av == bv); break;
            case HVM_NE: result = (av != bv); break;
            case HVM_LT: result = (av < bv); break;
            case HVM_LE: result = (av <= bv); break;
            case HVM_GT: result = (av > bv); break;
            case HVM_GE: result = (av >= bv); break;
            default: result = 0; break;
        }
    }

    if (!hvm_push_bool(vm, result)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }

    hvm_free_value(&a);
    hvm_free_value(&b);
    return 1;
}

static int op_logic(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value a, b;
    int result;
    if (vm->stack_top < 2) {
        hvm_set_error_msg(vm, "Stack underflow in logical op");
        vm->running = 0;
        return 1;
    }
    b = hvm_pop(vm);
    a = hvm_pop(vm);
    result = (instr->opcode == HVM_AND) ? (hvm_is_truthy(a) && hvm_is_truthy(b)) : (hvm_is_truthy(a) || hvm_is_truthy(b));
    if (!hvm_push_bool(vm, result)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&a);
    hvm_free_value(&b);
    return 1;
}

static int op_not(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value v;
    (void)instr;
    if (vm->stack_top < 1) {
        hvm_set_error_msg(vm, "Stack underflow in NOT");
        vm->running = 0;
        return 1;
    }
    v = hvm_pop(vm);
    if (!hvm_push_bool(vm, !hvm_is_truthy(v))) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&v);
    return 1;
}

static int op_store_global(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value v;
    if (vm->stack_top < 1) {
        hvm_set_error_msg(vm, "Stack underflow in STORE_GLOBAL");
        vm->running = 0;
        return 1;
    }
    v = hvm_pop(vm);
    if (!store_global(vm, instr->operand.string_operand, v)) {
        hvm_free_value(&v);
        hvm_set_error_msg(vm, "Failed to store global variable");
        vm->running = 0;
    }
    return 1;
}

static int op_load_global(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value v;
    memset(&v, 0, sizeof(v));
    v.type = HVM_TYPE_INT;
    if (!load_global(vm, instr->operand.string_operand, &v)) {
        if (!hvm_push_int(vm, 0)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    } else if (v.type == HVM_TYPE_FLOAT) {
        if (!hvm_push_float(vm, v.data.float_value)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    } else if (v.type == HVM_TYPE_STRING) {
        if (!hvm_push_string(vm, v.data.string_value ? v.data.string_value : "")) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    } else if (v.type == HVM_TYPE_BOOL) {
        if (!hvm_push_bool(vm, v.data.bool_value)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    } else {
        if (!hvm_push_int(vm, v.data.int_value)) {
            hvm_set_error_msg(vm, "Stack overflow");
            vm->running = 0;
        }
    }
    return 1;
}

static int op_print(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value value;
    if (vm->stack_top < 1) {
        hvm_set_error_msg(vm, "Stack underflow in PRINT");
        vm->running = 0;
        return 1;
    }
    value = hvm_pop(vm);
    switch (value.type) {
        case HVM_TYPE_INT:
            printf("%lld", (long long)value.data.int_value);
            break;
        case HVM_TYPE_FLOAT:
            printf("%g", value.data.float_value);
            break;
        case HVM_TYPE_STRING:
            printf("%s", value.data.string_value ? value.data.string_value : "");
            break;
        case HVM_TYPE_BOOL:
            printf("%s", value.data.bool_value ? "true" : "false");
            break;
        default:
            printf("null");
            break;
    }
    if (instr->opcode == HVM_PRINTLN) printf("\n");
    hvm_free_value(&value);
    return 1;
}

static int op_file_read(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value path_value;
    const char *path = NULL;
    char *content = NULL;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in FILE_READ"); vm->running = 0; return 1; }
    path_value = hvm_pop(vm);
    if (path_value.type == HVM_TYPE_STRING) path = path_value.data.string_value ? path_value.data.string_value : "";
    else path = hvm_value_to_cstring_a(vm, path_value);
    if (!path) {
        hvm_free_value(&path_value);
        hvm_set_error_msg(vm, "Out of memory in FILE_READ path");
        vm->running = 0;
        return 1;
    }

    content = hvm_read_text_file(path);
    if (!content) {
        hvm_free_value(&path_value);
        hvm_set_error_msg(vm, "Out of memory in FILE_READ content");
        vm->running = 0;
        return 1;
    }

    if (!hvm_push_string(vm, content)) {
        hvm_set_error_msg(vm, "Stack overflow in FILE_READ");
        vm->running = 0;
    }

    free(content);
    hvm_free_value(&path_value);
    return 1;
}

static int op_file_read_line(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value line_value;
    HVM_Value path_value;
    const char *path = NULL;
    char *line = NULL;
    int line_no;
    (void)instr;
    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in FILE_READ_LINE"); vm->running = 0; return 1; }
    line_value = hvm_pop(vm);
    path_value = hvm_pop(vm);

    line_no = (line_value.type == HVM_TYPE_FLOAT) ? (int)line_value.data.float_value : (int)line_value.data.int_value;
    if (path_value.type == HVM_TYPE_STRING) path = path_value.data.string_value ? path_value.data.string_value : "";
    else path = hvm_value_to_cstring_a(vm, path_value);

    if (!path) {
        hvm_free_value(&line_value);
        hvm_free_value(&path_value);
        hvm_set_error_msg(vm, "Out of memory in FILE_READ_LINE path");
        vm->running = 0;
        return 1;
    }

    line = hvm_read_text_line(path, line_no);
    if (!line) {
        hvm_free_value(&line_value);
        hvm_free_value(&path_value);
        hvm_set_error_msg(vm, "Out of memory in FILE_READ_LINE");
        vm->running = 0;
        return 1;
    }

    if (!hvm_push_string(vm, line)) {
        hvm_set_error_msg(vm, "Stack overflow in FILE_READ_LINE");
        vm->running = 0;
    }

    free(line);
    hvm_free_value(&line_value);
    hvm_free_value(&path_value);
    return 1;
}

static int op_file_write(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value content_value;
    HVM_Value path_value;
    const char *path = NULL;
    const char *text = NULL;
    int ok;
    (void)instr;
    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in FILE_WRITE"); vm->running = 0; return 1; }
    content_value = hvm_pop(vm);
    path_value = hvm_pop(vm);

    if (path_value.type == HVM_TYPE_STRING) path = path_value.data.string_value ? path_value.data.string_value : "";
    else path = hvm_value_to_cstring_a(vm, path_value);

    if (content_value.type == HVM_TYPE_STRING) text = content_value.data.string_value ? content_value.data.string_value : "";
    else text = hvm_value_to_cstring_b(vm, content_value);

    if (!path || !text) {
        hvm_free_value(&content_value);
        hvm_free_value(&path_value);
        hvm_set_error_msg(vm, "Out of memory in FILE_WRITE");
        vm->running = 0;
        return 1;
    }

    ok = hvm_write_text_file(path, text);
    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow in FILE_WRITE");
        vm->running = 0;
    }

    hvm_free_value(&content_value);
    hvm_free_value(&path_value);
    return 1;
}

static int op_exec_command(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cmd_value;
    const char *cmd = NULL;
    char *out = NULL;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in EXEC_COMMAND"); vm->running = 0; return 1; }

    cmd_value = hvm_pop(vm);
    if (cmd_value.type == HVM_TYPE_STRING) cmd = cmd_value.data.string_value ? cmd_value.data.string_value : "";
    else cmd = hvm_value_to_cstring_a(vm, cmd_value);

    if (!cmd) {
        hvm_free_value(&cmd_value);
        hvm_set_error_msg(vm, "Out of memory in EXEC_COMMAND");
        vm->running = 0;
        return 1;
    }

    out = hvm_exec_command(cmd);
    if (!out) out = strdup("");
    if (!out || !hvm_push_string(vm, out)) {
        free(out);
        hvm_free_value(&cmd_value);
        hvm_set_error_msg(vm, "Stack overflow in EXEC_COMMAND");
        vm->running = 0;
        return 1;
    }

    free(out);
    hvm_free_value(&cmd_value);
    return 1;
}

static int op_jump(HVM_VM *vm, const HVM_Instruction *instr) {
    int64_t target = instr->operand.int_operand;
    if (target < 0 || (size_t)target >= vm->instruction_count) {
        hvm_set_error_msg(vm, "Invalid jump target");
        vm->running = 0;
        return 0;
    }
    vm->pc = (size_t)target;
    return 0;
}

static int op_jump_if(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cond;
    int truthy;
    int should_jump;
    int64_t target = instr->operand.int_operand;
    if (vm->stack_top < 1) {
        hvm_set_error_msg(vm, "Stack underflow in conditional jump");
        vm->running = 0;
        return 1;
    }
    cond = hvm_pop(vm);
    truthy = hvm_is_truthy(cond);
    should_jump = (instr->opcode == HVM_JUMP_IF_FALSE) ? !truthy : truthy;
    hvm_free_value(&cond);
    if (should_jump) {
        if (target < 0 || (size_t)target >= vm->instruction_count) {
            hvm_set_error_msg(vm, "Invalid jump target");
            vm->running = 0;
            return 0;
        }
        vm->pc = (size_t)target;
        return 0;
    }
    return 1;
}

static int op_call(HVM_VM *vm, const HVM_Instruction *instr) {
    int64_t target = instr->operand.int_operand;
    if (target < 0 || (size_t)target >= vm->instruction_count) {
        hvm_set_error_msg(vm, "Invalid call target");
        vm->running = 0;
        return 1;
    }
    if (vm->call_top >= HVM_CALL_STACK_SIZE) {
        hvm_set_error_msg(vm, "Call stack overflow");
        vm->running = 0;
        return 1;
    }
    vm->call_stack[vm->call_top++] = vm->pc + 1;
    vm->pc = (size_t)target;
    return 0;
}

static int op_return(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (vm->call_top == 0) {
        vm->running = 0;
        return 0;
    }
    vm->pc = vm->call_stack[--vm->call_top];
    return 0;
}

static int op_halt(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    vm->running = 0;
    return 0;
}

static int op_nop(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)vm; (void)instr;
    return 1;
}

static const char *gui_opcode_to_native(HVM_Opcode opcode) {
    switch (opcode) {
        case HVM_CREATE_WINDOW: return "gui.create_window";
        case HVM_CLEAR: return "gui.clear";
        case HVM_SET_BG_COLOR: return "gui.set_bg_color";
        case HVM_SET_COLOR: return "gui.set_color";
        case HVM_SET_FONT_SIZE: return "gui.set_font_size";
        case HVM_DRAW_TEXT: return "gui.draw_text";
        case HVM_DRAW_BUTTON: return "gui.draw_button";
        case HVM_DRAW_BUTTON_STATE: return "gui.draw_button_state";
        case HVM_DRAW_INPUT: return "gui.draw_input";
        case HVM_DRAW_INPUT_STATE: return "gui.draw_input_state";
        case HVM_DRAW_TEXTAREA: return "gui.draw_textarea";
        case HVM_DRAW_IMAGE: return "gui.draw_image";
        case HVM_GET_MOUSE_X: return "gui.get_mouse_x";
        case HVM_GET_MOUSE_Y: return "gui.get_mouse_y";
        case HVM_IS_MOUSE_DOWN: return "gui.is_mouse_down";
        case HVM_WAS_MOUSE_UP: return "gui.was_mouse_up";
        case HVM_WAS_MOUSE_CLICK: return "gui.was_mouse_click";
        case HVM_IS_MOUSE_HOVER: return "gui.is_mouse_hover";
        case HVM_IS_KEY_DOWN: return "gui.is_key_down";
        case HVM_WAS_KEY_PRESS: return "gui.was_key_press";
        case HVM_DELTA_TIME: return "gui.delta_time";
        case HVM_LAYOUT_RESET: return "gui.layout_reset";
        case HVM_LAYOUT_NEXT: return "gui.layout_next";
        case HVM_LAYOUT_ROW: return "gui.layout_row";
        case HVM_LAYOUT_COLUMN: return "gui.layout_column";
        case HVM_LAYOUT_GRID: return "gui.layout_grid";
        case HVM_LOOP: return "gui.loop";
        case HVM_MENU_SETUP_NOTEPAD: return "gui.menu_setup_notepad";
        case HVM_MENU_EVENT: return "gui.menu_event";
        case HVM_SCROLL_SET_RANGE: return "gui.scroll_set_range";
        case HVM_SCROLL_Y: return "gui.scroll_y";
        case HVM_FILE_OPEN_DIALOG: return "gui.file_open_dialog";
        case HVM_FILE_SAVE_DIALOG: return "gui.file_save_dialog";
        case HVM_INPUT_SET: return "gui.input_set";
        case HVM_TEXTAREA_SET: return "gui.textarea_set";
        default: return NULL;
    }
}

static int op_call_native(HVM_VM *vm, const HVM_Instruction *instr) {
    const char *name = instr ? instr->operand.string_operand : NULL;
    int handled = hvm_runtime_services_call_native(vm->services, vm, name, instr);
    if (!handled) {
        hvm_set_error_msg(vm, "Unknown native call");
        vm->running = 0;
        return 0;
    }
    return 1;
}

static int op_sleep(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value v;
    int ms;
    (void)instr;
    if (vm->stack_top < 1) {
        hvm_set_error_msg(vm, "Stack underflow in SLEEP");
        vm->running = 0;
        return 1;
    }
    v = hvm_pop(vm);
    ms = (v.type == HVM_TYPE_FLOAT) ? (int)v.data.float_value : (int)v.data.int_value;
    if (ms < 0) ms = 0;
    hvm_runtime_services_sleep_ms(vm->services, ms);
    hvm_free_value(&v);
    return 1;
}

static int op_gui(HVM_VM *vm, const HVM_Instruction *instr) {
    const char *name = gui_opcode_to_native(instr ? instr->opcode : HVM_OPCODE_COUNT);
    int handled;
    if (!name) {
        hvm_set_error_msg(vm, "Unknown legacy GUI opcode");
        vm->running = 0;
        return 0;
    }
    handled = hvm_runtime_services_call_native(vm->services, vm, name, instr);
    if (!handled) {
        hvm_set_error_msg(vm, "GUI service unavailable");
        vm->running = 0;
        return 0;
    }
    return 1;
}

static const HVM_OpcodeHandler s_opcode_handlers[HVM_OPCODE_COUNT] = {
    [HVM_PUSH_INT] = op_push_int,
    [HVM_PUSH_FLOAT] = op_push_float,
    [HVM_PUSH_STRING] = op_push_string,
    [HVM_PUSH_BOOL] = op_push_bool,
    [HVM_POP] = op_pop,
    [HVM_ADD] = op_add,
    [HVM_SUB] = op_arithmetic,
    [HVM_MUL] = op_arithmetic,
    [HVM_DIV] = op_arithmetic,
    [HVM_MOD] = op_arithmetic,
    [HVM_EQ] = op_compare,
    [HVM_NE] = op_compare,
    [HVM_LT] = op_compare,
    [HVM_LE] = op_compare,
    [HVM_GT] = op_compare,
    [HVM_GE] = op_compare,
    [HVM_AND] = op_logic,
    [HVM_OR] = op_logic,
    [HVM_NOT] = op_not,
    [HVM_STORE_GLOBAL] = op_store_global,
    [HVM_LOAD_GLOBAL] = op_load_global,
    [HVM_PRINT] = op_print,
    [HVM_PRINTLN] = op_print,
    [HVM_JUMP] = op_jump,
    [HVM_JUMP_IF_FALSE] = op_jump_if,
    [HVM_JUMP_IF_TRUE] = op_jump_if,
    [HVM_CALL] = op_call,
    [HVM_RETURN] = op_return,
    [HVM_HALT] = op_halt,
    [HVM_NOP] = op_nop,
    [HVM_FILE_READ] = op_file_read,
    [HVM_FILE_READ_LINE] = op_file_read_line,
    [HVM_FILE_WRITE] = op_file_write,
    [HVM_EXEC_COMMAND] = op_exec_command,
    [HVM_SLEEP] = op_sleep,

    [HVM_CREATE_WINDOW] = op_gui,
    [HVM_DRAW_TEXT] = op_gui,
    [HVM_DRAW_BUTTON] = op_gui,
    [HVM_DRAW_BUTTON_STATE] = op_gui,
    [HVM_DRAW_INPUT] = op_gui,
    [HVM_DRAW_INPUT_STATE] = op_gui,
    [HVM_DRAW_TEXTAREA] = op_gui,
    [HVM_SET_COLOR] = op_gui,
    [HVM_CLEAR] = op_gui,
    [HVM_SET_BG_COLOR] = op_gui,
    [HVM_SET_FONT_SIZE] = op_gui,
    [HVM_DRAW_IMAGE] = op_gui,
    [HVM_GET_MOUSE_X] = op_gui,
    [HVM_GET_MOUSE_Y] = op_gui,
    [HVM_IS_MOUSE_DOWN] = op_gui,
    [HVM_WAS_MOUSE_UP] = op_gui,
    [HVM_WAS_MOUSE_CLICK] = op_gui,
    [HVM_IS_MOUSE_HOVER] = op_gui,
    [HVM_IS_KEY_DOWN] = op_gui,
    [HVM_WAS_KEY_PRESS] = op_gui,
    [HVM_DELTA_TIME] = op_gui,
    [HVM_LAYOUT_RESET] = op_gui,
    [HVM_LAYOUT_NEXT] = op_gui,
    [HVM_LAYOUT_ROW] = op_gui,
    [HVM_LAYOUT_COLUMN] = op_gui,
    [HVM_LAYOUT_GRID] = op_gui,
    [HVM_LOOP] = op_gui,
    [HVM_MENU_SETUP_NOTEPAD] = op_gui,
    [HVM_MENU_EVENT] = op_gui,
    [HVM_SCROLL_SET_RANGE] = op_gui,
    [HVM_SCROLL_Y] = op_gui,
    [HVM_FILE_OPEN_DIALOG] = op_gui,
    [HVM_FILE_SAVE_DIALOG] = op_gui,
    [HVM_INPUT_SET] = op_gui,
    [HVM_TEXTAREA_SET] = op_gui,
    [HVM_CALL_NATIVE] = op_call_native
};

int hvm_run(HVM_VM* vm) {
    if (!vm || !vm->instructions) return 0;

    if (vm->error_message) {
        free(vm->error_message);
        vm->error_message = NULL;
    }

    vm->running = 1;
    vm->pc = 0;
    hvm_runtime_services_reset_for_run(vm->services);

    while (vm->running && vm->pc < vm->instruction_count) {
        HVM_Instruction* instr = &vm->instructions[vm->pc];
        HVM_OpcodeHandler handler = NULL;
        int advance = 1;

        if ((int)instr->opcode >= 0 && instr->opcode < HVM_OPCODE_COUNT) {
            handler = s_opcode_handlers[instr->opcode];
        }

        if (!handler) {
            hvm_set_error_msg(vm, "Unknown opcode");
            vm->running = 0;
            break;
        }

        advance = handler(vm, instr);

        if (vm->gc_pending) {
            hvm_gc_collect_internal(vm);
        }

        if (vm->running && advance) {
            vm->pc++;
        }
    }

    hvm_runtime_services_post_run(vm->services, vm);
    return vm->error_message ? 0 : 1;
}


