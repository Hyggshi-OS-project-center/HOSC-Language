/* hvm_memory.c - Internal value helpers and reusable buffers for HOSC VM */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hvm_internal.h"

void hvm_free_value(HVM_Value *v) {
    if (!v) return;
    if (v->type == HVM_TYPE_STRING) {
        v->data.string_value = NULL;
    }
    v->type = HVM_TYPE_NULL;
}

char* hvm_ensure_buffer(char **buffer, size_t *cap, size_t needed) {
    char *out;
    size_t new_cap;

    if (!buffer || !cap) return NULL;
    if (needed == 0) needed = 1;

    if (*cap >= needed && *buffer) {
        return *buffer;
    }

    new_cap = (*cap < 16) ? 16 : *cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    out = (char *)realloc(*buffer, new_cap);
    if (!out) return NULL;

    *buffer = out;
    *cap = new_cap;
    return out;
}

const char* hvm_value_to_cstring(HVM_VM *vm, HVM_Value v, char **buffer, size_t *cap) {
    char temp[128];
    const char *src = "";
    size_t len;

    if (!vm || !buffer || !cap) return NULL;

    switch (v.type) {
        case HVM_TYPE_STRING:
            src = v.data.string_value ? v.data.string_value : "";
            break;
        case HVM_TYPE_FLOAT:
            snprintf(temp, sizeof(temp), "%g", v.data.float_value);
            src = temp;
            break;
        case HVM_TYPE_BOOL:
            src = v.data.bool_value ? "true" : "false";
            break;
        case HVM_TYPE_INT:
            snprintf(temp, sizeof(temp), "%lld", (long long)v.data.int_value);
            src = temp;
            break;
        default:
            src = "null";
            break;
    }

    len = strlen(src) + 1;
    if (!hvm_ensure_buffer(buffer, cap, len)) return NULL;
    memcpy(*buffer, src, len);
    return *buffer;
}

const char* hvm_value_to_cstring_a(HVM_VM *vm, HVM_Value v) {
    return hvm_value_to_cstring(vm, v, &vm->scratch_a, &vm->scratch_a_cap);
}

const char* hvm_value_to_cstring_b(HVM_VM *vm, HVM_Value v) {
    return hvm_value_to_cstring(vm, v, &vm->scratch_b, &vm->scratch_b_cap);
}

const char* hvm_concat_cstrings(HVM_VM *vm, const char *a, const char *b) {
    size_t len_a = 0;
    size_t len_b = 0;
    size_t total;
    char *out;

    if (!vm) return NULL;
    if (!a) a = "";
    if (!b) b = "";

    len_a = strlen(a);
    len_b = strlen(b);
    total = len_a + len_b + 1;

    out = hvm_ensure_buffer(&vm->scratch_concat, &vm->scratch_concat_cap, total);
    if (!out) return NULL;

    memcpy(out, a, len_a);
    memcpy(out + len_a, b, len_b);
    out[total - 1] = '\0';
    return out;
}
