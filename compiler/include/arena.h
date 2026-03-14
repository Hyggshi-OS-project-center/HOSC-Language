/* arena.h - Arena allocator interface for HOSC compiler */

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Arena Arena;

Arena* arena_create(size_t size);
void* arena_alloc(Arena* arena, size_t size);
void arena_reset(Arena* arena);
void arena_destroy(Arena* arena);
char* arena_strdup(Arena* arena, const char* str);

#ifdef __cplusplus
}
#endif

#endif // ARENA_H

