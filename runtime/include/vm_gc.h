/* vm_gc.h - GC and VM object helpers */
#ifndef VM_GC_H
#define VM_GC_H

#include <stddef.h>
#include "hvm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HVM_GcHeap HVM_GcHeap;

HVM_GcHeap* hvm_gc_create(void);
void hvm_gc_destroy(HVM_GcHeap* heap);
char* hvm_gc_strdup(HVM_GcHeap* heap, HVM_VM* vm, const char* value);
void hvm_gc_collect_heap(HVM_GcHeap* heap, HVM_VM* vm);
size_t hvm_gc_live_objects_heap(const HVM_GcHeap* heap);
size_t hvm_gc_live_bytes_heap(const HVM_GcHeap* heap);

#ifdef __cplusplus
}
#endif

#endif /* VM_GC_H */
