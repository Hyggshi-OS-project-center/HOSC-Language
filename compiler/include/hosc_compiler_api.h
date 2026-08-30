#ifndef HOSC_COMPILER_API_H
#define HOSC_COMPILER_API_H

#include <stdbool.h>
#include <stddef.h>

#include "hosc_bytecode.h"
#include "hosc_diag.h"

typedef struct HoscCompileOptions {
    const char* module_name;
    const char* output_path;
    bool emit_debug_info;
    bool check_only;
} HoscCompileOptions;

typedef struct HoscCompileResult {
    bool success;
    HBytecode* bytecode;
    HDiagnosticBag* diagnostics;
} HoscCompileResult;

HoscCompileResult hosc_compile_file(const char* path, const HoscCompileOptions* options);
HoscCompileResult hosc_compile_memory(
    const char* display_path,
    const char* source,
    size_t length,
    const HoscCompileOptions* options);
bool hosc_write_bytecode_file(const char* path, const HBytecode* bytecode);
void hosc_compile_result_free(HoscCompileResult* result);

#endif
