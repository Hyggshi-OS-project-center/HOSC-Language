#include "hosc_runtime_api.h"

#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fputs("usage: hvm_host <file.hbc>\n", stderr);
        return 1;
    }

    return hosc_runtime_run_file(argv[1], NULL);
}
