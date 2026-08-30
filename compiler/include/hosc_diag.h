#ifndef HOSC_DIAG_H
#define HOSC_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum HoscDiagnosticSeverity {
    HOSC_DIAG_ERROR = 1,
    HOSC_DIAG_WARNING = 2
} HoscDiagnosticSeverity;

typedef struct HoscSourceSpan {
    int line;
    int column;
    int end_line;
    int end_column;
} HoscSourceSpan;

typedef struct HoscDiagnostic {
    HoscDiagnosticSeverity severity;
    char code[8];
    char* message;
    char* file_path;
    HoscSourceSpan span;
} HoscDiagnostic;

typedef struct HDiagnosticBag {
    HoscDiagnostic* items;
    size_t count;
    size_t capacity;
} HDiagnosticBag;

void hosc_diag_bag_init(HDiagnosticBag* bag);
void hosc_diag_bag_add(
    HDiagnosticBag* bag,
    HoscDiagnosticSeverity severity,
    const char* code,
    const char* file_path,
    HoscSourceSpan span,
    const char* message);
bool hosc_diag_has_errors(const HDiagnosticBag* bag);
void hosc_diag_bag_free(HDiagnosticBag* bag);
void hosc_diag_bag_print(const HDiagnosticBag* bag, FILE* stream);

#endif

