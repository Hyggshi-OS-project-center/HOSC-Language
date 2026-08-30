#include "hosc_diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* hosc_diag_strdup(const char* text) {
    size_t len;
    char* copy;

    if (!text) {
        return NULL;
    }

    len = strlen(text);
    copy = (char*)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

void hosc_diag_bag_init(HDiagnosticBag* bag) {
    bag->items = NULL;
    bag->count = 0;
    bag->capacity = 0;
}

void hosc_diag_bag_add(
    HDiagnosticBag* bag,
    HoscDiagnosticSeverity severity,
    const char* code,
    const char* file_path,
    HoscSourceSpan span,
    const char* message) {
    HoscDiagnostic* item;
    size_t new_capacity;

    if (bag->count == bag->capacity) {
        new_capacity = bag->capacity == 0 ? 4 : bag->capacity * 2;
        item = (HoscDiagnostic*)realloc(bag->items, new_capacity * sizeof(HoscDiagnostic));
        if (!item) {
            return;
        }
        bag->items = item;
        bag->capacity = new_capacity;
    }

    item = &bag->items[bag->count++];
    memset(item, 0, sizeof(*item));
    item->severity = severity;
    item->span = span;
    if (code) {
        snprintf(item->code, sizeof(item->code), "%s", code);
    }
    item->file_path = hosc_diag_strdup(file_path ? file_path : "");
    item->message = hosc_diag_strdup(message ? message : "unknown diagnostic");
}

bool hosc_diag_has_errors(const HDiagnosticBag* bag) {
    size_t i;
    for (i = 0; i < bag->count; ++i) {
        if (bag->items[i].severity == HOSC_DIAG_ERROR) {
            return true;
        }
    }
    return false;
}

void hosc_diag_bag_free(HDiagnosticBag* bag) {
    size_t i;
    if (!bag) {
        return;
    }
    for (i = 0; i < bag->count; ++i) {
        free(bag->items[i].file_path);
        free(bag->items[i].message);
    }
    free(bag->items);
    bag->items = NULL;
    bag->count = 0;
    bag->capacity = 0;
}

static void print_source_snippet(FILE* stream, const char* file_path, int line, int column, int end_column) {
    FILE* fp;
    char buf[1024];
    int current_line = 1;
    int found = 0;
    size_t len;
    int i;
    int spaces;

    if (!file_path || line <= 0) return;
    fp = fopen(file_path, "r");
    if (!fp) return;

    while (fgets(buf, sizeof(buf), fp)) {
        if (current_line == line) {
            found = 1;
            break;
        }
        current_line++;
    }
    fclose(fp);

    if (!found) return;

    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
        buf[--len] = '\0';
    }

    fprintf(stream, "\n%s\n", buf);

    if (column > 0) {
        spaces = column - 1;
        for (i = 0; i < spaces; i++) {
            if (i < (int)len && buf[i] == '\t') {
                fputc('\t', stream);
            } else {
                fputc(' ', stream);
            }
        }
        fputc('^', stream);
        if (end_column > column + 1) {
            int tildes = end_column - column - 1;
            for (i = 0; i < tildes; i++) {
                fputc('~', stream);
            }
        }
        fputc('\n', stream);
    }
}

void hosc_diag_bag_print(const HDiagnosticBag* bag, FILE* stream) {
    size_t i;
    if (!bag || !stream) return;

    for (i = 0; i < bag->count; ++i) {
        const HoscDiagnostic* item = &bag->items[i];
        const char* file_path = item->file_path ? item->file_path : "<unknown>";
        const char* severity = item->severity == HOSC_DIAG_ERROR ? "error" : "warning";
        const char* code = (item->code[0] != '\0') ? item->code : NULL;
        const char* message = item->message ? item->message : "diagnostic";

        if (code) {
            fprintf(stream, "%s:%d:%d: %s %s:\n", file_path, item->span.line, item->span.column, severity, code);
        } else {
            fprintf(stream, "%s:%d:%d: %s:\n", file_path, item->span.line, item->span.column, severity);
        }
        fprintf(stream, "%s\n", message);

        print_source_snippet(stream, file_path, item->span.line, item->span.column, item->span.end_column);
    }
}


