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
        free(bag->items[i].message);
    }
    free(bag->items);
    bag->items = NULL;
    bag->count = 0;
    bag->capacity = 0;
}
