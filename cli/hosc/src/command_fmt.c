#include "hosc_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_text(const char* path, size_t* out_len) {
    FILE* file;
    long size;
    char* data;
    size_t got;
    if (out_len) *out_len = 0;
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) { if (file) fclose(file); return NULL; }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    data = (char*)malloc((size_t)size + 1);
    if (!data) { fclose(file); return NULL; }
    got = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) { free(data); return NULL; }
    data[got] = '\0';
    if (out_len) *out_len = got;
    return data;
}

static char* format_text(const char* input, size_t len, size_t* out_len) {
    char* output = (char*)malloc(len + 9);
    size_t i, used = 0, line_start = 0;
    if (!output) return NULL;
    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)input[i];
        if (ch == '\r' || ch == '\n') {
            while (used > line_start && output[used - 1] == ' ') --used;
            output[used++] = '\n';
            if (ch == '\r' && i + 1 < len && input[i + 1] == '\n') ++i;
            line_start = used;
        } else if (ch == '\t') {
            output[used++] = ' '; output[used++] = ' '; output[used++] = ' '; output[used++] = ' ';
        } else {
            output[used++] = (char)ch;
        }
    }
    while (used > line_start && output[used - 1] == ' ') --used;
    if (used == 0 || output[used - 1] != '\n') output[used++] = '\n';
    output[used] = '\0';
    if (out_len) *out_len = used;
    return output;
}

int hosc_cli_command_fmt(const char* path) {
    return hosc_cli_command_fmt_ex(path, NULL, 0);
}

int hosc_cli_command_fmt_ex(const char* path, const char* output_path, int check_only) {
    char* input = NULL;
    char* formatted = NULL;
    size_t input_len = 0, formatted_len = 0;
    const char* destination = output_path && output_path[0] ? output_path : path;
    int changed;
    FILE* file;
    input = read_text(path, &input_len);
    if (!input) { fprintf(stderr, "failed to read %s\n", path); return 1; }
    formatted = format_text(input, input_len, &formatted_len);
    if (!formatted) { free(input); fputs("failed to format source\n", stderr); return 1; }
    changed = input_len != formatted_len || memcmp(input, formatted, input_len < formatted_len ? input_len : formatted_len) != 0;
    if (check_only) {
        free(input); free(formatted);
        if (changed) { fprintf(stderr, "formatting required: %s\n", path); return 1; }
        return 0;
    }
    file = fopen(destination, "wb");
    if (!file || fwrite(formatted, 1, formatted_len, file) != formatted_len) {
        if (file) fclose(file);
        free(input); free(formatted);
        fprintf(stderr, "failed to write %s\n", destination);
        return 1;
    }
    fclose(file);
    free(input); free(formatted);
    printf("formatted: %s\n", destination);
    return 0;
}
