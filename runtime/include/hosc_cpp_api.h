/*
 * File: runtime\include\hosc_cpp_api.h
 * Purpose: HOSC source file.
 */

#ifndef HOSC_CPP_API_H
#define HOSC_CPP_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HoscApiContext HoscApiContext;

HoscApiContext* hosc_api_create(size_t arena_size);
void hosc_api_destroy(HoscApiContext* ctx);

void* hosc_api_alloc(HoscApiContext* ctx, size_t size, size_t alignment);

int hosc_api_set_int(HoscApiContext* ctx, const char* key, int64_t value);
int hosc_api_get_int(HoscApiContext* ctx, const char* key, int64_t* out_value);

int hosc_api_set_string(HoscApiContext* ctx, const char* key, const char* value);
int hosc_api_get_string(HoscApiContext* ctx, const char* key, const char** out_value);

void hosc_api_clear(HoscApiContext* ctx);
size_t hosc_api_used_bytes(const HoscApiContext* ctx);
size_t hosc_api_capacity_bytes(const HoscApiContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // HOSC_CPP_API_H
