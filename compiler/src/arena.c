/*
 * File: compiler\src\arena.c
 * Purpose: HOSC source file.
 */

#include "arena.h"
#include <stdlib.h>
#include <string.h>

Arena* arena_create(size_t size) {
    Arena* arena;
    if (size == 0) return NULL;
    arena = (Arena*)malloc(sizeof(Arena));
    if (!arena) return NULL;
    arena->memory = (unsigned char*)malloc(size);
    if (!arena->memory) {
        free(arena);
        return NULL;
    }
    arena->size = size;
    arena->offset = 0;
    return arena;
}

void* arena_alloc(Arena* arena, size_t size) {
    size_t aligned;
    if (!arena || size == 0) return NULL;

    aligned = (size + 7u) & ~7u; // align to 8 bytes
    if (aligned < size) return NULL; // overflow
    if (arena->offset > arena->size) return NULL;
    if (aligned > arena->size - arena->offset) return NULL;

    void* ptr = arena->memory + arena->offset;
    arena->offset += aligned;
    return ptr;
}

void arena_reset(Arena* arena) {
    if (arena) arena->offset = 0;
}

void arena_destroy(Arena* arena) {
    if (!arena) return;
    free(arena->memory);
    free(arena);
}

char* arena_strdup(Arena* arena, const char* str) {
    size_t len;
    char* dst;
    if (!arena || !str) return NULL;
    len = strlen(str) + 1;
    dst = (char*)arena_alloc(arena, len);
    if (!dst) return NULL;
    memcpy(dst, str, len);
    return dst;
}
