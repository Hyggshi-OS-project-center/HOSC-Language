#ifndef HVM_OBJECT_H
#define HVM_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

struct HVM;
struct HValue;

typedef bool (*HNativeFn)(struct HVM* vm, int argc, const struct HValue* argv, struct HValue* out_result);

typedef enum HObjectKind {
    HOBJ_STRING = 1,
    HOBJ_NATIVE = 2
} HObjectKind;

typedef struct HObject {
    HObjectKind kind;
    uint8_t marked;
    struct HObject* next;
} HObject;

typedef struct HStringObject {
    HObject base;
    uint32_t length;
    uint32_t hash;
    char* chars;
} HStringObject;

typedef struct HNativeObject {
    HObject base;
    char* name;
    int arity;
    HNativeFn fn;
} HNativeObject;

struct HVM;

void* hvm_allocate_object(struct HVM* vm, size_t size, HObjectKind kind);
void hvm_free_object(HObject* object);
HStringObject* hvm_string_new(struct HVM* vm, const char* chars, uint32_t length);
HNativeObject* hvm_native_new(struct HVM* vm, const char* name, int arity, HNativeFn fn);
void hvm_free_all_objects(struct HVM* vm);
void hvm_collect_garbage(struct HVM* vm);

#endif
