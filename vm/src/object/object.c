#include "hvm_object.h"

#include <stdlib.h>
#include <string.h>

#include "hvm_api.h"

void* hvm_allocate_object(HVM* vm, size_t size, HObjectKind kind) {
    HObject* object;

    object = (HObject*)calloc(1, size);
    if (!object) {
        return NULL;
    }

    object->kind = kind;
    object->marked = 0;
    object->next = vm->objects_head;
    vm->objects_head = object;
    vm->bytes_allocated += size;
    return object;
}

void hvm_free_object(HObject* object) {
    if (!object) {
        return;
    }

    switch (object->kind) {
        case HOBJ_STRING:
            free(((HStringObject*)object)->chars);
            break;
        case HOBJ_NATIVE:
            free(((HNativeObject*)object)->name);
            break;
        default:
            break;
    }

    free(object);
}
