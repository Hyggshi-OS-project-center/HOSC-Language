#include "hosc_cli.h"

#include <stdlib.h>
#include <string.h>

char* hosc_cli_replace_extension(const char* path, const char* extension) {
    const char* dot;
    size_t base_len;
    size_t ext_len;
    char* result;

    dot = strrchr(path, '.');
    base_len = dot ? (size_t)(dot - path) : strlen(path);
    ext_len = strlen(extension);
    result = (char*)malloc(base_len + ext_len + 1);
    if (!result) {
        return NULL;
    }

    memcpy(result, path, base_len);
    memcpy(result + base_len, extension, ext_len);
    result[base_len + ext_len] = '\0';
    return result;
}
