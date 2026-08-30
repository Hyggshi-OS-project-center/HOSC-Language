#include "hvm_object.h"

#include "hvm_api.h"

void hvm_collect_garbage(HVM* vm) {
    (void)vm;
}

void hvm_free_all_objects(HVM* vm) {
    HObject* object;
    HObject* next;

    object = vm->objects_head;
    while (object) {
        next = object->next;
        hvm_free_object(object);
        object = next;
    }
    vm->objects_head = NULL;
}
