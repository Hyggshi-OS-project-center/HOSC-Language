/*
 * File: runtime\src\hvm_gc.c
 * Purpose: HOSC source file.
 */

static HVM_GCObject* hvm_gc_find_object(HVM_VM* vm, const char* ptr) {
    HVM_GCObject *it;
    if (!vm || !ptr) return NULL;
    it = vm->gc_objects;
    while (it) {
        if (it->ptr == ptr) return it;
        it = it->next;
    }
    return NULL;
}

static int hvm_gc_track_string(HVM_VM* vm, char* ptr, size_t size) {
    HVM_GCObject *obj;
    if (!vm || !ptr) return 0;

    obj = (HVM_GCObject *)malloc(sizeof(HVM_GCObject));
    if (!obj) return 0;

    obj->ptr = ptr;
    obj->size = size;
    obj->marked = 0;
    obj->next = vm->gc_objects;
    vm->gc_objects = obj;
    vm->gc_object_count++;
    vm->gc_bytes += size;

    if (vm->gc_enabled && vm->gc_bytes >= vm->gc_next_collection) {
        vm->gc_pending = 1;
    }

    return 1;
}

static char* hvm_gc_strdup(HVM_VM* vm, const char* value) {
    size_t len;
    char *ptr;

    if (!vm) return NULL;
    if (!value) value = "";

    len = strlen(value) + 1;
    ptr = strdup(value);
    if (!ptr) return NULL;

    if (!hvm_gc_track_string(vm, ptr, len)) {
        free(ptr);
        return NULL;
    }

    return ptr;
}

static void hvm_gc_mark_pointer(HVM_VM* vm, const char* ptr) {
    HVM_GCObject *obj;
    if (!vm || !ptr) return;
    obj = hvm_gc_find_object(vm, ptr);
    if (obj) obj->marked = 1;
}

static void hvm_gc_mark_roots(HVM_VM* vm) {
    size_t i;
    if (!vm) return;

    for (i = 0; i < vm->stack_top; i++) {
        if (vm->stack[i].type == HVM_TYPE_STRING && vm->stack[i].data.string_value) {
            hvm_gc_mark_pointer(vm, vm->stack[i].data.string_value);
        }
    }

    for (i = 0; i < vm->memory_used; i++) {
        if (vm->memory[i].type == HVM_TYPE_STRING && vm->memory[i].data.string_value) {
            hvm_gc_mark_pointer(vm, vm->memory[i].data.string_value);
        }
    }
}

static void hvm_gc_sweep(HVM_VM* vm) {
    HVM_GCObject *cur;
    HVM_GCObject *prev;

    if (!vm) return;

    prev = NULL;
    cur = vm->gc_objects;
    while (cur) {
        if (!cur->marked) {
            HVM_GCObject *dead = cur;
            if (prev) prev->next = cur->next;
            else vm->gc_objects = cur->next;

            cur = cur->next;
            vm->gc_object_count--;
            if (vm->gc_bytes >= dead->size) vm->gc_bytes -= dead->size;
            else vm->gc_bytes = 0;

            free(dead->ptr);
            free(dead);
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
}

static void hvm_gc_collect_internal(HVM_VM* vm) {
    HVM_GCObject *it;
    size_t next;

    if (!vm || !vm->gc_enabled) return;

    it = vm->gc_objects;
    while (it) {
        it->marked = 0;
        it = it->next;
    }

    hvm_gc_mark_roots(vm);
    hvm_gc_sweep(vm);

    vm->gc_pending = 0;

    next = vm->gc_bytes * 2;
    if (next < HVM_GC_INITIAL_THRESHOLD) next = HVM_GC_INITIAL_THRESHOLD;
    vm->gc_next_collection = next;
}

static void hvm_gc_destroy_all(HVM_VM* vm) {
    HVM_GCObject *it;
    if (!vm) return;

    it = vm->gc_objects;
    while (it) {
        HVM_GCObject *next = it->next;
        free(it->ptr);
        free(it);
        it = next;
    }

    vm->gc_objects = NULL;
    vm->gc_object_count = 0;
    vm->gc_bytes = 0;
    vm->gc_pending = 0;
}

