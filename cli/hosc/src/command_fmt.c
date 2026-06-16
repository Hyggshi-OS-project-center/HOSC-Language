#include "hosc_cli.h"

#include <stdio.h>

int hosc_cli_command_fmt(const char* path) {
    (void)path;
    fputs("hosc fmt: formatter not implemented in bootstrap build\n", stderr);
    return 2;
}
