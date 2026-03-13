/*
 * File: runtime/src/hvm_execute.c
 * Purpose: Executes HVM bytecode using a dispatch table.
 */

#include <string.h>
#include "runtime_services.h"

typedef int (*HVM_OpcodeHandler)(HVM_VM *vm, const HVM_Instruction *instr);

static unsigned int hvm_color_rgb(int r, int g, int b) {
    return ((unsigned int)(r & 0xFF)) | (((unsigned int)(g & 0xFF)) << 8) | (((unsigned int)(b & 0xFF)) << 16);
}

static int hvm_value_to_int(HVM_Value v) {
    return (v.type == HVM_TYPE_FLOAT) ? (int)v.data.float_value : (int)v.data.int_value;
}

static long long hvm_value_to_ll(HVM_Value v) {
    return (v.type == HVM_TYPE_FLOAT) ? (long long)v.data.float_value : (long long)v.data.int_value;
}
static int hvm_exec_push_int(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_int(vm, instr->operand.int_operand)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_push_float(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_float(vm, instr->operand.float_operand)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_push_string(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_string(vm, instr->operand.string_operand ? instr->operand.string_operand : "")) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_push_bool(HVM_VM *vm, const HVM_Instruction *instr) {
    if (!hvm_push_bool(vm, (int)instr->operand.int_operand)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_pop(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_add(HVM_VM *vm, const HVM_Instruction *instr) {
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
        const char *sa = hvm_value_to_cstring(vm, a, &vm->scratch_a, &vm->scratch_a_cap);
        const char *sb = hvm_value_to_cstring(vm, b, &vm->scratch_b, &vm->scratch_b_cap);
        size_t lena = strlen(sa);
        size_t lenb = strlen(sb);
        if (!hvm_ensure_buffer(&vm->scratch_concat, &vm->scratch_concat_cap, lena + lenb + 1)) {
            hvm_set_error_msg(vm, "Out of memory in string concat");
            vm->running = 0;
            hvm_free_value(&a);
            hvm_free_value(&b);
            return 1;
        }
        memcpy(vm->scratch_concat, sa, lena);
        memcpy(vm->scratch_concat + lena, sb, lenb);
        vm->scratch_concat[lena + lenb] = '\0';
        if (!hvm_push_string(vm, vm->scratch_concat)) {
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

static int hvm_exec_arithmetic(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_compare(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_logic(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_not(HVM_VM *vm, const HVM_Instruction *instr) {
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
static int hvm_exec_store_global(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_load_global(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_print(HVM_VM *vm, const HVM_Instruction *instr) {
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
static int hvm_exec_create_window(HVM_VM *vm, const HVM_Instruction *instr) {
    const char *title = instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window";
    int success = 0;
    if (vm->services && vm->services->gui.create_window) {
        success = vm->services->gui.create_window(vm->services, title);
    }
    if (!hvm_push_bool(vm, success)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_clear(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value b, g, r;
    int ri, gi, bi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in CLEAR"); vm->running = 0; return 1; }
    b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
    ri = hvm_value_to_int(r);
    gi = hvm_value_to_int(g);
    bi = hvm_value_to_int(b);
    if (vm->services) {
        vm->services->gui.set_bg_color(vm->services, hvm_color_rgb(ri, gi, bi));
        vm->services->gui.clear_commands(vm->services);
    }
    if (!hvm_push_int(vm, 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
    return 1;
}

static int hvm_exec_set_bg_color(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value b, g, r;
    int ri, gi, bi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in SET_BG_COLOR"); vm->running = 0; return 1; }
    b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
    ri = hvm_value_to_int(r);
    gi = hvm_value_to_int(g);
    bi = hvm_value_to_int(b);
    if (vm->services) {
        vm->services->gui.set_bg_color(vm->services, hvm_color_rgb(ri, gi, bi));
    }
    if (!hvm_push_int(vm, 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
    return 1;
}

static int hvm_exec_set_color(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value b, g, r;
    int ri, gi, bi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in SET_COLOR"); vm->running = 0; return 1; }
    b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
    ri = hvm_value_to_int(r);
    gi = hvm_value_to_int(g);
    bi = hvm_value_to_int(b);
    if (vm->services) {
        vm->services->gui.set_fg_color(vm->services, hvm_color_rgb(ri, gi, bi));
    }
    if (!hvm_push_int(vm, 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
    return 1;
}

static int hvm_exec_set_font_size(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value v;
    int size;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in SET_FONT_SIZE"); vm->running = 0; return 1; }
    v = hvm_pop(vm);
    size = hvm_value_to_int(v);
    if (vm->services) {
        vm->services->gui.set_font_size(vm->services, size);
    }
    if (!hvm_push_int(vm, 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&v);
    return 1;
}

static int hvm_exec_draw_text(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value msg, y, x;
    const char *text;
    long long xi, yi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in DRAW_TEXT"); vm->running = 0; return 1; }
    msg = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    text = (msg.type == HVM_TYPE_STRING && msg.data.string_value) ? msg.data.string_value : hvm_value_to_cstring(vm, msg, &vm->scratch_a, &vm->scratch_a_cap);
    xi = hvm_value_to_ll(x);
    yi = hvm_value_to_ll(y);
    if (vm->services) {
        if (!vm->services->gui.draw_text(vm->services, (int)xi, (int)yi, text)) {
            hvm_set_error_msg(vm, "Out of memory while queueing GUI text");
            vm->running = 0;
        }
    }
    if (!hvm_push_int(vm, 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&msg); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int hvm_exec_draw_button(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value label, h, w, y, x;
    long long xi, yi, wi, hi;
    int clicked = 0;
    const char *button_id;
    (void)instr;
    if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_BUTTON"); vm->running = 0; return 1; }
    label = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = hvm_value_to_ll(x);
    yi = hvm_value_to_ll(y);
    wi = hvm_value_to_ll(w);
    hi = hvm_value_to_ll(h);
    button_id = (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : hvm_value_to_cstring(vm, label, &vm->scratch_a, &vm->scratch_a_cap);
    if (vm->services) {
        if (!vm->services->gui.draw_button(vm->services, (int)xi, (int)yi, (int)wi, (int)hi, button_id, &clicked)) {
            hvm_set_error_msg(vm, "Out of memory while queueing button");
            vm->running = 0;
        }
    }
    if (!hvm_push_bool(vm, clicked)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&label); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int hvm_exec_draw_button_state(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value label_value, id_value;
    const char *id_name;
    const char *label_text;
    int clicked = 0;
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in DRAW_BUTTON_STATE"); vm->running = 0; return 1; }
    label_value = hvm_pop(vm);
    id_value = hvm_pop(vm);

    id_name = (id_value.type == HVM_TYPE_STRING && id_value.data.string_value)
        ? id_value.data.string_value
        : hvm_value_to_cstring(vm, id_value, &vm->scratch_a, &vm->scratch_a_cap);

    label_text = (label_value.type == HVM_TYPE_STRING && label_value.data.string_value)
        ? label_value.data.string_value
        : hvm_value_to_cstring(vm, label_value, &vm->scratch_b, &vm->scratch_b_cap);

    if (vm->services) {
        if (!vm->services->gui.draw_button_state(vm->services, id_name, label_text, &clicked)) {
            hvm_set_error_msg(vm, "Out of memory while queueing state button");
            vm->running = 0;
        }
    }

    if (!hvm_push_bool(vm, clicked)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }

    hvm_free_value(&label_value);
    hvm_free_value(&id_value);
    return 1;
}

static int hvm_exec_draw_input(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value label, w, y, x;
    long long xi, yi, wi;
    const char *text_out = "";
    const char *label_text;
    (void)instr;
    if (vm->stack_top < 4) { hvm_set_error_msg(vm, "Stack underflow in DRAW_INPUT"); vm->running = 0; return 1; }
    label = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = hvm_value_to_ll(x);
    yi = hvm_value_to_ll(y);
    wi = hvm_value_to_ll(w);
    label_text = (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : hvm_value_to_cstring(vm, label, &vm->scratch_a, &vm->scratch_a_cap);
    if (vm->services) {
        if (!vm->services->gui.draw_input(vm->services, (int)xi, (int)yi, (int)wi, label_text, &text_out)) {
            hvm_set_error_msg(vm, "Out of memory while queueing input");
            vm->running = 0;
        }
    }
    if (!hvm_push_string(vm, text_out ? text_out : "")) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&label); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int hvm_exec_draw_input_state(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value placeholder_value, id_value;
    const char *id_name;
    const char *placeholder;
    const char *text_out = "";
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in DRAW_INPUT_STATE"); vm->running = 0; return 1; }
    placeholder_value = hvm_pop(vm);
    id_value = hvm_pop(vm);

    id_name = (id_value.type == HVM_TYPE_STRING && id_value.data.string_value)
        ? id_value.data.string_value
        : hvm_value_to_cstring(vm, id_value, &vm->scratch_a, &vm->scratch_a_cap);

    placeholder = (placeholder_value.type == HVM_TYPE_STRING && placeholder_value.data.string_value)
        ? placeholder_value.data.string_value
        : hvm_value_to_cstring(vm, placeholder_value, &vm->scratch_b, &vm->scratch_b_cap);

    if (vm->services) {
        if (!vm->services->gui.draw_input_state(vm->services, id_name, placeholder, &text_out)) {
            hvm_set_error_msg(vm, "Out of memory while queueing state input");
            vm->running = 0;
        }
    }

    if (!hvm_push_string(vm, text_out ? text_out : "")) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }

    hvm_free_value(&placeholder_value);
    hvm_free_value(&id_value);
    return 1;
}

static int hvm_exec_draw_textarea(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value id, h, w, y, x;
    long long xi, yi, wi, hi;
    const char *text_out = "";
    const char *id_text;
    (void)instr;
    if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_TEXTAREA"); vm->running = 0; return 1; }
    id = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = hvm_value_to_ll(x);
    yi = hvm_value_to_ll(y);
    wi = hvm_value_to_ll(w);
    hi = hvm_value_to_ll(h);
    id_text = (id.type == HVM_TYPE_STRING && id.data.string_value) ? id.data.string_value : hvm_value_to_cstring(vm, id, &vm->scratch_a, &vm->scratch_a_cap);
    if (vm->services) {
        if (!vm->services->gui.draw_textarea(vm->services, (int)xi, (int)yi, (int)wi, (int)hi, id_text, &text_out)) {
            hvm_set_error_msg(vm, "Out of memory while queueing textarea");
            vm->running = 0;
        }
    }
    if (!hvm_push_string(vm, text_out ? text_out : "")) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&id); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int hvm_exec_draw_image(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value path, h, w, y, x;
    long long xi, yi, wi, hi;
    const char *label;
    (void)instr;
    if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_IMAGE"); vm->running = 0; return 1; }
    path = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = hvm_value_to_ll(x);
    yi = hvm_value_to_ll(y);
    wi = hvm_value_to_ll(w);
    hi = hvm_value_to_ll(h);
    label = (path.type == HVM_TYPE_STRING && path.data.string_value) ? path.data.string_value : hvm_value_to_cstring(vm, path, &vm->scratch_a, &vm->scratch_a_cap);
    if (vm->services) {
        if (!vm->services->gui.draw_image(vm->services, (int)xi, (int)yi, (int)wi, (int)hi, label)) {
            hvm_set_error_msg(vm, "Out of memory while queueing image");
            vm->running = 0;
        }
    }
    if (!hvm_push_int(vm, 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&path); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int hvm_exec_get_mouse_x(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_int(vm, vm->services ? vm->services->gui.get_mouse_x(vm->services) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_get_mouse_y(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_int(vm, vm->services ? vm->services->gui.get_mouse_y(vm->services) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_is_mouse_down(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_bool(vm, vm->services ? vm->services->gui.is_mouse_down(vm->services) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_was_mouse_up(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_bool(vm, vm->services ? vm->services->gui.was_mouse_up(vm->services) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_was_mouse_click(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_bool(vm, vm->services ? vm->services->gui.was_mouse_click(vm->services) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_is_mouse_hover(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value id;
    const char *id_name;
    int hovered = 0;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in IS_MOUSE_HOVER"); vm->running = 0; return 1; }
    id = hvm_pop(vm);
    id_name = (id.type == HVM_TYPE_STRING && id.data.string_value) ? id.data.string_value : hvm_value_to_cstring(vm, id, &vm->scratch_a, &vm->scratch_a_cap);
    if (vm->services) {
        hovered = vm->services->gui.is_mouse_hover(vm->services, id_name);
    }
    if (!hvm_push_bool(vm, hovered)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&id);
    return 1;
}

static int hvm_exec_is_key_down(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value key;
    int code;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in IS_KEY_DOWN"); vm->running = 0; return 1; }
    key = hvm_pop(vm);
    code = hvm_value_to_int(key);
    if (!hvm_push_bool(vm, vm->services ? vm->services->gui.is_key_down(vm->services, code) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&key);
    return 1;
}

static int hvm_exec_was_key_press(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value key;
    int code;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in WAS_KEY_PRESS"); vm->running = 0; return 1; }
    key = hvm_pop(vm);
    code = hvm_value_to_int(key);
    if (!hvm_push_bool(vm, vm->services ? vm->services->gui.was_key_press(vm->services, code) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&key);
    return 1;
}

static int hvm_exec_delta_time(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_float(vm, vm->services ? vm->services->gui.get_delta_ms(vm->services) : 0.0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_layout_reset(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value gap, y, x;
    int ok = 0;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_RESET"); vm->running = 0; return 1; }
    gap = hvm_pop(vm);
    y = hvm_pop(vm);
    x = hvm_pop(vm);

    if (vm->services) {
        ok = vm->services->gui.layout_reset(vm->services, hvm_value_to_int(x), hvm_value_to_int(y), hvm_value_to_int(gap));
    }

    if (!hvm_push_int(vm, ok ? 0 : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&gap);
    hvm_free_value(&y);
    hvm_free_value(&x);
    return 1;
}

static int hvm_exec_layout_next(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value h;
    int y = 0;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_NEXT"); vm->running = 0; return 1; }
    h = hvm_pop(vm);
    if (vm->services) {
        y = vm->services->gui.layout_next(vm->services, hvm_value_to_int(h));
    }
    if (!hvm_push_int(vm, y)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&h);
    return 1;
}

static int hvm_exec_layout_row(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cols_value;
    int cols = 0;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_ROW"); vm->running = 0; return 1; }
    cols_value = hvm_pop(vm);
    if (vm->services) {
        cols = vm->services->gui.layout_row(vm->services, hvm_value_to_int(cols_value));
    }
    if (!hvm_push_int(vm, cols)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&cols_value);
    return 1;
}

static int hvm_exec_layout_column(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value width_value;
    int width = 0;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_COLUMN"); vm->running = 0; return 1; }
    width_value = hvm_pop(vm);
    if (vm->services) {
        width = vm->services->gui.layout_column(vm->services, hvm_value_to_int(width_value));
    }
    if (!hvm_push_int(vm, width)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&width_value);
    return 1;
}

static int hvm_exec_layout_grid(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cell_h_value, cell_w_value, cols_value;
    int cols = 0;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_GRID"); vm->running = 0; return 1; }
    cell_h_value = hvm_pop(vm);
    cell_w_value = hvm_pop(vm);
    cols_value = hvm_pop(vm);
    if (vm->services) {
        cols = vm->services->gui.layout_grid(vm->services, hvm_value_to_int(cols_value), hvm_value_to_int(cell_w_value), hvm_value_to_int(cell_h_value));
    }
    if (!hvm_push_int(vm, cols)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&cell_h_value);
    hvm_free_value(&cell_w_value);
    hvm_free_value(&cols_value);
    return 1;
}

static int hvm_exec_loop(HVM_VM *vm, const HVM_Instruction *instr) {
    double delta = 0.0;
    int running = 1;
    (void)instr;
    if (vm->services) {
        running = vm->services->gui.loop_tick(vm->services, &delta);
    }
    if (!running) {
        vm->running = 0;
    }
    if (!hvm_push_float(vm, delta)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_menu_setup_notepad(HVM_VM *vm, const HVM_Instruction *instr) {
    int ok = 0;
    (void)instr;
    if (vm->services) ok = vm->services->gui.menu_setup_notepad(vm->services);
    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_menu_event(HVM_VM *vm, const HVM_Instruction *instr) {
    int evt = 0;
    (void)instr;
    if (vm->services) evt = vm->services->gui.menu_event(vm->services);
    if (!hvm_push_int(vm, evt)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_scroll_set_range(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value total;
    int total_height;
    int result = 0;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in SCROLL_SET_RANGE"); vm->running = 0; return 1; }
    total = hvm_pop(vm);
    total_height = hvm_value_to_int(total);
    if (vm->services) result = vm->services->gui.scroll_set_range(vm->services, total_height);
    if (!hvm_push_int(vm, result)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    hvm_free_value(&total);
    return 1;
}

static int hvm_exec_scroll_y(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (!hvm_push_int(vm, vm->services ? vm->services->gui.scroll_y(vm->services) : 0)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }
    return 1;
}

static int hvm_exec_file_open_dialog(HVM_VM *vm, const HVM_Instruction *instr) {
    char *path_value = NULL;
    (void)instr;
    if (vm->services) path_value = vm->services->gui.open_file_dialog(vm->services);
    if (!path_value) path_value = strdup("");
    if (!path_value || !hvm_push_string(vm, path_value)) {
        free(path_value);
        hvm_set_error_msg(vm, "Failed to push OPEN dialog path");
        vm->running = 0;
        return 1;
    }
    free(path_value);
    return 1;
}

static int hvm_exec_file_save_dialog(HVM_VM *vm, const HVM_Instruction *instr) {
    char *path_value = NULL;
    (void)instr;
    if (vm->services) path_value = vm->services->gui.save_file_dialog(vm->services);
    if (!path_value) path_value = strdup("");
    if (!path_value || !hvm_push_string(vm, path_value)) {
        free(path_value);
        hvm_set_error_msg(vm, "Failed to push SAVE dialog path");
        vm->running = 0;
        return 1;
    }
    free(path_value);
    return 1;
}
static int hvm_exec_file_read(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value path_value;
    const char *path;
    char *content = NULL;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in FILE_READ"); vm->running = 0; return 1; }
    path_value = hvm_pop(vm);
    path = (path_value.type == HVM_TYPE_STRING && path_value.data.string_value)
        ? path_value.data.string_value
        : hvm_value_to_cstring(vm, path_value, &vm->scratch_a, &vm->scratch_a_cap);

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

static int hvm_exec_file_read_line(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value line_value;
    HVM_Value path_value;
    const char *path;
    char *line = NULL;
    int line_no;
    (void)instr;
    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in FILE_READ_LINE"); vm->running = 0; return 1; }
    line_value = hvm_pop(vm);
    path_value = hvm_pop(vm);

    line_no = hvm_value_to_int(line_value);
    path = (path_value.type == HVM_TYPE_STRING && path_value.data.string_value)
        ? path_value.data.string_value
        : hvm_value_to_cstring(vm, path_value, &vm->scratch_a, &vm->scratch_a_cap);

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

static int hvm_exec_file_write(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value content_value;
    HVM_Value path_value;
    const char *path;
    const char *text;
    int ok;
    (void)instr;
    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in FILE_WRITE"); vm->running = 0; return 1; }
    content_value = hvm_pop(vm);
    path_value = hvm_pop(vm);

    path = (path_value.type == HVM_TYPE_STRING && path_value.data.string_value)
        ? path_value.data.string_value
        : hvm_value_to_cstring(vm, path_value, &vm->scratch_a, &vm->scratch_a_cap);

    text = (content_value.type == HVM_TYPE_STRING && content_value.data.string_value)
        ? content_value.data.string_value
        : hvm_value_to_cstring(vm, content_value, &vm->scratch_b, &vm->scratch_b_cap);

    ok = hvm_write_text_file(path, text);
    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow in FILE_WRITE");
        vm->running = 0;
    }

    hvm_free_value(&content_value);
    hvm_free_value(&path_value);
    return 1;
}

static int hvm_exec_input_set(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value value;
    HVM_Value label;
    const char *label_name;
    const char *text_value;
    int ok = 0;
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in INPUT_SET"); vm->running = 0; return 1; }

    value = hvm_pop(vm);
    label = hvm_pop(vm);

    label_name = (label.type == HVM_TYPE_STRING && label.data.string_value)
        ? label.data.string_value
        : hvm_value_to_cstring(vm, label, &vm->scratch_a, &vm->scratch_a_cap);

    text_value = (value.type == HVM_TYPE_STRING && value.data.string_value)
        ? value.data.string_value
        : hvm_value_to_cstring(vm, value, &vm->scratch_b, &vm->scratch_b_cap);

    if (vm->services) {
        ok = vm->services->gui.input_set(vm->services, label_name, text_value);
        if (!ok) {
            hvm_set_error_msg(vm, "Out of memory resizing INPUT_SET");
            vm->running = 0;
        }
    }

    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }

    hvm_free_value(&value);
    hvm_free_value(&label);
    return 1;
}

static int hvm_exec_textarea_set(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value value;
    HVM_Value id;
    const char *id_name;
    const char *text_value;
    int ok = 0;
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in TEXTAREA_SET"); vm->running = 0; return 1; }

    value = hvm_pop(vm);
    id = hvm_pop(vm);

    id_name = (id.type == HVM_TYPE_STRING && id.data.string_value)
        ? id.data.string_value
        : hvm_value_to_cstring(vm, id, &vm->scratch_a, &vm->scratch_a_cap);

    text_value = (value.type == HVM_TYPE_STRING && value.data.string_value)
        ? value.data.string_value
        : hvm_value_to_cstring(vm, value, &vm->scratch_b, &vm->scratch_b_cap);

    if (vm->services) {
        ok = vm->services->gui.textarea_set(vm->services, id_name, text_value);
        if (!ok) {
            hvm_set_error_msg(vm, "Out of memory resizing TEXTAREA_SET");
            vm->running = 0;
        }
    }

    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow");
        vm->running = 0;
    }

    hvm_free_value(&value);
    hvm_free_value(&id);
    return 1;
}

static int hvm_exec_command(HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cmd_value;
    const char *cmd;
    char *out = NULL;
    (void)instr;

    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in EXEC_COMMAND"); vm->running = 0; return 1; }

    cmd_value = hvm_pop(vm);
    cmd = (cmd_value.type == HVM_TYPE_STRING && cmd_value.data.string_value)
        ? cmd_value.data.string_value
        : hvm_value_to_cstring(vm, cmd_value, &vm->scratch_a, &vm->scratch_a_cap);

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

static int hvm_exec_jump(HVM_VM *vm, const HVM_Instruction *instr) {
    int64_t target = instr->operand.int_operand;
    if (target < 0 || (size_t)target >= vm->instruction_count) {
        hvm_set_error_msg(vm, "Invalid jump target");
        vm->running = 0;
        return 1;
    }
    vm->pc = (size_t)target;
    return 0;
}

static int hvm_exec_jump_cond(HVM_VM *vm, const HVM_Instruction *instr) {
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
            return 1;
        }
        vm->pc = (size_t)target;
        return 0;
    }
    return 1;
}

static int hvm_exec_call(HVM_VM *vm, const HVM_Instruction *instr) {
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

static int hvm_exec_return(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (vm->call_top == 0) {
        vm->running = 0;
        return 1;
    }
    vm->pc = vm->call_stack[--vm->call_top];
    return 0;
}

static int hvm_exec_halt(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    vm->running = 0;
    return 1;
}

static int hvm_exec_nop(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)vm;
    (void)instr;
    return 1;
}

static int hvm_exec_unknown(HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    hvm_set_error_msg(vm, "Unknown opcode");
    vm->running = 0;
    return 1;
}
static HVM_OpcodeHandler hvm_dispatch[HVM_OPCODE_COUNT];

static void hvm_init_dispatch(void) {
    size_t i;
    static int initialized = 0;
    if (initialized) return;
    for (i = 0; i < HVM_OPCODE_COUNT; i++) {
        hvm_dispatch[i] = hvm_exec_unknown;
    }
    hvm_dispatch[HVM_PUSH_INT] = hvm_exec_push_int;
    hvm_dispatch[HVM_PUSH_FLOAT] = hvm_exec_push_float;
    hvm_dispatch[HVM_PUSH_STRING] = hvm_exec_push_string;
    hvm_dispatch[HVM_PUSH_BOOL] = hvm_exec_push_bool;
    hvm_dispatch[HVM_POP] = hvm_exec_pop;
    hvm_dispatch[HVM_ADD] = hvm_exec_add;
    hvm_dispatch[HVM_SUB] = hvm_exec_arithmetic;
    hvm_dispatch[HVM_MUL] = hvm_exec_arithmetic;
    hvm_dispatch[HVM_DIV] = hvm_exec_arithmetic;
    hvm_dispatch[HVM_MOD] = hvm_exec_arithmetic;
    hvm_dispatch[HVM_EQ] = hvm_exec_compare;
    hvm_dispatch[HVM_NE] = hvm_exec_compare;
    hvm_dispatch[HVM_LT] = hvm_exec_compare;
    hvm_dispatch[HVM_LE] = hvm_exec_compare;
    hvm_dispatch[HVM_GT] = hvm_exec_compare;
    hvm_dispatch[HVM_GE] = hvm_exec_compare;
    hvm_dispatch[HVM_AND] = hvm_exec_logic;
    hvm_dispatch[HVM_OR] = hvm_exec_logic;
    hvm_dispatch[HVM_NOT] = hvm_exec_not;
    hvm_dispatch[HVM_STORE_GLOBAL] = hvm_exec_store_global;
    hvm_dispatch[HVM_LOAD_GLOBAL] = hvm_exec_load_global;
    hvm_dispatch[HVM_PRINT] = hvm_exec_print;
    hvm_dispatch[HVM_PRINTLN] = hvm_exec_print;
    hvm_dispatch[HVM_CREATE_WINDOW] = hvm_exec_create_window;
    hvm_dispatch[HVM_CLEAR] = hvm_exec_clear;
    hvm_dispatch[HVM_SET_BG_COLOR] = hvm_exec_set_bg_color;
    hvm_dispatch[HVM_SET_COLOR] = hvm_exec_set_color;
    hvm_dispatch[HVM_SET_FONT_SIZE] = hvm_exec_set_font_size;
    hvm_dispatch[HVM_DRAW_TEXT] = hvm_exec_draw_text;
    hvm_dispatch[HVM_DRAW_BUTTON] = hvm_exec_draw_button;
    hvm_dispatch[HVM_DRAW_BUTTON_STATE] = hvm_exec_draw_button_state;
    hvm_dispatch[HVM_DRAW_INPUT] = hvm_exec_draw_input;
    hvm_dispatch[HVM_DRAW_INPUT_STATE] = hvm_exec_draw_input_state;
    hvm_dispatch[HVM_DRAW_TEXTAREA] = hvm_exec_draw_textarea;
    hvm_dispatch[HVM_DRAW_IMAGE] = hvm_exec_draw_image;
    hvm_dispatch[HVM_GET_MOUSE_X] = hvm_exec_get_mouse_x;
    hvm_dispatch[HVM_GET_MOUSE_Y] = hvm_exec_get_mouse_y;
    hvm_dispatch[HVM_IS_MOUSE_DOWN] = hvm_exec_is_mouse_down;
    hvm_dispatch[HVM_WAS_MOUSE_UP] = hvm_exec_was_mouse_up;
    hvm_dispatch[HVM_WAS_MOUSE_CLICK] = hvm_exec_was_mouse_click;
    hvm_dispatch[HVM_IS_MOUSE_HOVER] = hvm_exec_is_mouse_hover;
    hvm_dispatch[HVM_IS_KEY_DOWN] = hvm_exec_is_key_down;
    hvm_dispatch[HVM_WAS_KEY_PRESS] = hvm_exec_was_key_press;
    hvm_dispatch[HVM_DELTA_TIME] = hvm_exec_delta_time;
    hvm_dispatch[HVM_LAYOUT_RESET] = hvm_exec_layout_reset;
    hvm_dispatch[HVM_LAYOUT_NEXT] = hvm_exec_layout_next;
    hvm_dispatch[HVM_LAYOUT_ROW] = hvm_exec_layout_row;
    hvm_dispatch[HVM_LAYOUT_COLUMN] = hvm_exec_layout_column;
    hvm_dispatch[HVM_LAYOUT_GRID] = hvm_exec_layout_grid;
    hvm_dispatch[HVM_LOOP] = hvm_exec_loop;
    hvm_dispatch[HVM_MENU_SETUP_NOTEPAD] = hvm_exec_menu_setup_notepad;
    hvm_dispatch[HVM_MENU_EVENT] = hvm_exec_menu_event;
    hvm_dispatch[HVM_SCROLL_SET_RANGE] = hvm_exec_scroll_set_range;
    hvm_dispatch[HVM_SCROLL_Y] = hvm_exec_scroll_y;
    hvm_dispatch[HVM_FILE_OPEN_DIALOG] = hvm_exec_file_open_dialog;
    hvm_dispatch[HVM_FILE_SAVE_DIALOG] = hvm_exec_file_save_dialog;
    hvm_dispatch[HVM_FILE_READ] = hvm_exec_file_read;
    hvm_dispatch[HVM_FILE_READ_LINE] = hvm_exec_file_read_line;
    hvm_dispatch[HVM_FILE_WRITE] = hvm_exec_file_write;
    hvm_dispatch[HVM_INPUT_SET] = hvm_exec_input_set;
    hvm_dispatch[HVM_TEXTAREA_SET] = hvm_exec_textarea_set;
    hvm_dispatch[HVM_EXEC_COMMAND] = hvm_exec_command;
    hvm_dispatch[HVM_JUMP] = hvm_exec_jump;
    hvm_dispatch[HVM_JUMP_IF_FALSE] = hvm_exec_jump_cond;
    hvm_dispatch[HVM_JUMP_IF_TRUE] = hvm_exec_jump_cond;
    hvm_dispatch[HVM_CALL] = hvm_exec_call;
    hvm_dispatch[HVM_RETURN] = hvm_exec_return;
    hvm_dispatch[HVM_HALT] = hvm_exec_halt;
    hvm_dispatch[HVM_NOP] = hvm_exec_nop;
    initialized = 1;
}

int hvm_run(HVM_VM* vm) {
    if (!vm || !vm->instructions) return 0;

    if (vm->error_message) {
        free(vm->error_message);
        vm->error_message = NULL;
    }

    vm->running = 1;
    vm->pc = 0;

    if (vm->services) {
        vm->services->gui.prepare_run(vm->services);
    }

    hvm_init_dispatch();

    while (vm->running && vm->pc < vm->instruction_count) {
        HVM_Instruction* instr = &vm->instructions[vm->pc];
        HVM_OpcodeHandler handler = hvm_exec_unknown;
        int advance = 1;

        if ((unsigned)instr->opcode < HVM_OPCODE_COUNT) {
            handler = hvm_dispatch[instr->opcode];
        }
        if (!handler) handler = hvm_exec_unknown;

        advance = handler(vm, instr);

        if (vm->gc_pending) {
            hvm_gc_collect_internal(vm);
        }

        if (advance) {
            vm->pc++;
        }
    }

    if (!vm->error_message && vm->services) {
        vm->services->gui.finish_run(vm->services);
    }

    return vm->error_message ? 0 : 1;
}