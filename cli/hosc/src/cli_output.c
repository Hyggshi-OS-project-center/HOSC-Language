#include "hosc_cli.h"

#include <stdio.h>

void hosc_cli_print_usage(void) {
    puts("usage:");
    puts("  hosc run <file.hosc> [-o out.hbc] [--keep]");
    puts("  hosc build <file.hosc>");
    puts("  hosc check <file.hosc>");
    puts("  hosc fmt <file.hosc>");
    puts("  hosc test");
    puts("  hosc version");
}

void hosc_cli_print_diagnostics(const HDiagnosticBag* diagnostics) {
    size_t i;

    if (!diagnostics) {
        return;
    }

    for (i = 0; i < diagnostics->count; ++i) {
        const HoscDiagnostic* item = &diagnostics->items[i];
        fprintf(
            stderr,
            "%s %s at %d:%d: %s\n",
            item->severity == HOSC_DIAG_ERROR ? "error" : "warning",
            item->code,
            item->span.line,
            item->span.column,
            item->message ? item->message : "diagnostic");
    }
}
