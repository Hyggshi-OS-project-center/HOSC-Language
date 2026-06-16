#include "hosc_cli.h"

#include <stdio.h>

int hosc_cli_command_test(void) {
    fputs("hosc test: use CTest for the bootstrap build\n", stderr);
    return 2;
}
