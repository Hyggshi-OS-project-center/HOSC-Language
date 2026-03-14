/* gc.cpp - C++ garbage collector and VM object storage for HOSC */

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "vm_gc.h"

class VMObject {
public:
    virtual ~VMObject() = default;
    virtual const char* type_name() const = 0;
};

class VMString : public VMObject {
public:
    explicit VMString(std::string v) : value(std::move(v)) {}
    const char* type_name() const override { return "string"; }
    std::string value;
};

struct GCObject {
    std::unique_ptr<VMObject> object;
    const char *ptr = nullptr;
    size_t size = 0;
    bool marked = false;
};

struct HVM_GcHeap {
    std::vector<std::unique_ptr<GCObject>> objects;
};

static const size_t kGcInitialThreshold = 64u * 1024u;

static GCObject* find_object(HVM_GcHeap* heap, const char* ptr) {
    if (!heap || !ptr) return nullptr;
    for (const auto& obj : heap->objects) {
        if (obj && obj->ptr == ptr) return obj.get();
    }
    return nullptr;
}

static void mark_ptr(HVM_GcHeap* heap, const char* ptr) {
    GCObject* obj = find_object(heap, ptr);
    if (obj) obj->marked = true;
}

static void mark_roots(HVM_GcHeap* heap, HVM_VM* vm) {
    size_t i;
    if (!heap || !vm) return;

    for (i = 0; i < vm->stack_top; i++) {
        if (vm->stack[i].type == HVM_TYPE_STRING && vm->stack[i].data.string_value) {
            mark_ptr(heap, vm->stack[i].data.string_value);
        }
    }

    for (i = 0; i < vm->memory_used; i++) {
        if (vm->memory[i].type == HVM_TYPE_STRING && vm->memory[i].data.string_value) {
            mark_ptr(heap, vm->memory[i].data.string_value);
        }
    }
}

extern "C" {

HVM_GcHeap* hvm_gc_create(void) {
    return new (std::nothrow) HVM_GcHeap();
}

void hvm_gc_destroy(HVM_GcHeap* heap) {
    delete heap;
}

char* hvm_gc_strdup(HVM_GcHeap* heap, HVM_VM* vm, const char* value) {
    if (!heap || !vm) return nullptr;
    std::string src = value ? value : "";
    auto obj = std::make_unique<GCObject>();
    auto str_obj = std::make_unique<VMString>(src);
    obj->ptr = str_obj->value.c_str();
    obj->size = str_obj->value.size() + 1;
    obj->object = std::move(str_obj);
    obj->marked = false;

    vm->gc_object_count++;
    vm->gc_bytes += obj->size;

    if (vm->gc_enabled && vm->gc_bytes >= vm->gc_next_collection) {
        vm->gc_pending = 1;
    }

    heap->objects.push_back(std::move(obj));
    return const_cast<char*>(heap->objects.back()->ptr);
}

void hvm_gc_collect_heap(HVM_GcHeap* heap, HVM_VM* vm) {
    size_t i;
    size_t bytes = 0;
    size_t count = 0;
    if (!heap || !vm || !vm->gc_enabled) return;

    for (auto& obj : heap->objects) {
        if (obj) obj->marked = false;
    }

    mark_roots(heap, vm);

    heap->objects.erase(
        std::remove_if(heap->objects.begin(), heap->objects.end(),
            [&](const std::unique_ptr<GCObject>& obj) {
                if (!obj || !obj->marked) return true;
                return false;
            }),
        heap->objects.end());

    for (i = 0; i < heap->objects.size(); i++) {
        if (heap->objects[i]) {
            count++;
            bytes += heap->objects[i]->size;
        }
    }

    vm->gc_object_count = count;
    vm->gc_bytes = bytes;
    vm->gc_pending = 0;

    vm->gc_next_collection = bytes * 2;
    if (vm->gc_next_collection < kGcInitialThreshold) {
        vm->gc_next_collection = kGcInitialThreshold;
    }
}

size_t hvm_gc_live_objects_heap(const HVM_GcHeap* heap) {
    if (!heap) return 0;
    return heap->objects.size();
}

size_t hvm_gc_live_bytes_heap(const HVM_GcHeap* heap) {
    size_t bytes = 0;
    if (!heap) return 0;
    for (const auto& obj : heap->objects) {
        if (obj) bytes += obj->size;
    }
    return bytes;
}

} /* extern "C" */
