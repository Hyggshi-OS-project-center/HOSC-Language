/*
 * File: tools/hosc_cli.c
 * Purpose: HOSC compiler CLI entry point.
 */

#include "hosc_compiler.h"

int main(int argc, char **argv) {
    return hosc_compile_cli(argc, argv);
}
