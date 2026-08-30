#include "hvm_object.h"

#include <stdlib.h>
#include <string.h>

#include "hvm_api.h"

static char* hvm_strdup_len(const char* chars, uint32_t length) {
    char* copy;

    copy = (char*)malloc(length + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, chars, length);
    copy[length] = '\0';
    return copy;
}

static uint32_t hvm_hash_string(const char* chars, uint32_t length) {
    uint32_t hash = 2166136261u;
    uint32_t i;

    for (i = 0; i < length; ++i) {
        hash ^= (uint8_t)chars[i];
        hash *= 16777619u;
    }
    return hash;
}

HStringObject* hvm_string_new(HVM* vm, const char* chars, uint32_t length) {
    HStringObject* string_object;

    string_object = (HStringObject*)hvm_allocate_object(vm, sizeof(HStringObject), HOBJ_STRING);
    if (!string_object) {
        return NULL;
    }

    string_object->length = length;
    string_object->chars = hvm_strdup_len(chars, length);
    string_object->hash = hvm_hash_string(chars, length);
    if (!string_object->chars) {
        return NULL;
    }

    return string_object;
}

HNativeObject* hvm_native_new(HVM* vm, const char* name, int arity, HNativeFn fn) {
    HNativeObject* native_object;
    size_t name_len;

    native_object = (HNativeObject*)hvm_allocate_object(vm, sizeof(HNativeObject), HOBJ_NATIVE);
    if (!native_object) {
        return NULL;
    }

    name_len = strlen(name);
    native_object->name = hvm_strdup_len(name, (uint32_t)name_len);
    if (!native_object->name) {
        return NULL;
    }
    native_object->arity = arity;
    native_object->fn = fn;
    return native_object;
}
