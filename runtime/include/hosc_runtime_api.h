#ifndef HOSC_RUNTIME_API_H
#define HOSC_RUNTIME_API_H

#include <stdbool.h>

#include "hosc_bytecode.h"

typedef struct HoscRuntimeOptions {
    bool trace;
    bool enable_gc;
} HoscRuntimeOptions;

int hosc_runtime_run_file(const char* path, const HoscRuntimeOptions* options);
int hosc_runtime_run_bytecode(const HBytecode* bc, const HoscRuntimeOptions* options);

#endif
