/* file_utils.h - File loading helpers shared across compiler and runtime */
#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reads a file as a null-terminated UTF-8/byte string.
 * Returns heap-allocated buffer that caller must free.
 * If out_size is non-NULL, receives the number of bytes read (excluding terminator).
 */
char* hosc_read_text_file(const char* path, size_t* out_size);

/*
 * Reads a file as raw bytes.
 * Returns heap-allocated buffer that caller must free.
 * If out_size is non-NULL, receives the number of bytes read.
 */
unsigned char* hosc_read_file_bytes(const char* path, size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif /* FILE_UTILS_H */
