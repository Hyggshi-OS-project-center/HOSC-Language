/* arena.cpp - C++ arena allocator implementation for HOSC compiler */

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <string.h>

#include "arena.h"

struct Arena {
    std::unique_ptr<unsigned char[]> memory;
    size_t size = 0;
    size_t offset = 0;
};

extern "C" {

Arena* arena_create(size_t size) {
    if (size == 0) return nullptr;
    Arena* arena = new (std::nothrow) Arena();
    if (!arena) return nullptr;

    arena->memory.reset(new (std::nothrow) unsigned char[size]);
    if (!arena->memory) {
        delete arena;
        return nullptr;
    }

    arena->size = size;
    arena->offset = 0;
    return arena;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || size == 0) return nullptr;

    size_t aligned = (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
    if (arena->offset > arena->size) return nullptr;
    if (aligned > arena->size - arena->offset) return nullptr;

    unsigned char* ptr = arena->memory.get() + arena->offset;
    arena->offset += aligned;
    return ptr;
}

void arena_reset(Arena* arena) {
    if (arena) arena->offset = 0;
}

void arena_destroy(Arena* arena) {
    delete arena;
}

char* arena_strdup(Arena* arena, const char* str) {
    char* dst;
    size_t len;

    if (!arena || !str) return nullptr;
    len = strlen(str) + 1;
    dst = (char*)arena_alloc(arena, len);
    if (!dst) return nullptr;
    memcpy(dst, str, len);
    return dst;
}

} /* extern "C" */
