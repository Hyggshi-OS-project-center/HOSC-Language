/* file_utils.c - Shared file IO helpers for HOSC tooling */

#include "file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char* read_file_internal(const char* path, size_t* out_size, int null_terminate) {
    FILE* fp;
    long file_size;
    size_t read_size;
    unsigned char* buffer;
    size_t alloc_size;

    if (out_size) *out_size = 0;
    if (!path) return NULL;

    fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    alloc_size = (size_t)file_size + (null_terminate ? 1u : 0u);
    buffer = (unsigned char*)malloc(alloc_size > 0 ? alloc_size : (null_terminate ? 1u : 0u));
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read_size != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    if (null_terminate) {
        buffer[read_size] = '\0';
    }

    if (out_size) *out_size = read_size;
    return buffer;
}

char* hosc_read_text_file(const char* path, size_t* out_size) {
    return (char*)read_file_internal(path, out_size, 1);
}

unsigned char* hosc_read_file_bytes(const char* path, size_t* out_size) {
    return read_file_internal(path, out_size, 0);
}
