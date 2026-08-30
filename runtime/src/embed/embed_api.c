#include "hosc_embed.h"
#include "hosc_runtime_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool hosc_runtime_load_hbc_file(const char* path, HBytecode* out_bytecode, char* error_message, size_t error_message_size);

HoscRuntime* hosc_runtime_create(void) {
    HoscRuntime* runtime;

    runtime = (HoscRuntime*)calloc(1, sizeof(HoscRuntime));
    if (!runtime) {
        return NULL;
    }

    runtime->vm = hvm_create(NULL);
    if (!runtime->vm) {
        free(runtime);
        return NULL;
    }

    return runtime;
}

void hosc_runtime_destroy(HoscRuntime* runtime) {
    if (!runtime) {
        return;
    }
    hvm_destroy(runtime->vm);
    free(runtime);
}

bool hosc_runtime_register_native(HoscRuntime* runtime, const char* name, int arity, HNativeFn fn) {
    if (!runtime || !runtime->vm) {
        return false;
    }
    return hvm_register_native(runtime->vm, name, arity, fn);
}

int hosc_runtime_execute(HoscRuntime* runtime, const HBytecode* bytecode) {
    if (!runtime || !runtime->vm) {
        return 1;
    }
    return hvm_execute(runtime->vm, bytecode);
}

int hosc_runtime_run_bytecode(const HBytecode* bc, const HoscRuntimeOptions* options) {
    HVMConfig config;
    HVM* vm;
    int exit_code;

    config.enable_gc = options ? options->enable_gc : true;
    config.enable_trace = options ? options->trace : false;

    vm = hvm_create(&config);
    if (!vm) {
        fputs("failed to create HVM\n", stderr);
        return 1;
    }

    exit_code = hvm_execute(vm, bc);
    if (exit_code != 0) {
        fprintf(stderr, "runtime error: %s\n", hvm_last_error(vm));
    }

    hvm_destroy(vm);
    return exit_code;
}

int hosc_runtime_run_file(const char* path, const HoscRuntimeOptions* options) {
    HBytecode bytecode;
    char error_message[256];
    int exit_code;
    (void)options;

    if (!hosc_runtime_load_hbc_file(path, &bytecode, error_message, sizeof(error_message))) {
        fprintf(stderr, "failed to load bytecode: %s\n", error_message);
        return 1;
    }

    exit_code = hosc_runtime_run_bytecode(&bytecode, options);
    hbytecode_free(&bytecode);
    return exit_code;
}
