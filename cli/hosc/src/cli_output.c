#include "hosc_cli.h"

#include <stdio.h>

void hosc_cli_print_usage(void) {
    puts("usage:");
    puts("  hosc run <file.hosc> [-o out.hbc] [--keep]");
    puts("  hosc build <file.hosc> [-o output.hbc]");
    puts("  hosc check <file.hosc>");
    puts("  hosc fmt <file.hosc> [-o output.hosc] [--check]");
    puts("  hosc test");
    puts("  hosc version");
}

void hosc_cli_print_diagnostics(const HDiagnosticBag* diagnostics) {
    hosc_diag_bag_print(diagnostics, stderr);
}

