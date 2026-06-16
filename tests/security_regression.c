#include "hvm_api.h"
#include "hvm_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_true(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static HBytecode make_base_bytecode(HBCString* strings, HBCFunction* functions, uint8_t* code, size_t code_size) {
    HBytecode bc;
    hbytecode_init(&bc);
    bc.entry_function_index = 0;
    bc.string_count = 1;
    bc.strings = strings;
    bc.function_count = 1;
    bc.functions = functions;
    bc.code_size = code_size;
    bc.code = code;
    return bc;
}

static int test_valid_nil_return(void) {
    HVM* vm;
    int rc;
    HBCString strings[] = {
        {4u, "main"},
    };
    uint8_t code[] = {
        OP_NIL,
        OP_RETURN,
    };
    HBCFunction functions[] = {
        {0u, 0u, 1u, 1u, 0u, 0u, (uint32_t)sizeof(code)},
    };
    HBytecode bc = make_base_bytecode(strings, functions, code, sizeof(code));

    vm = hvm_create(NULL);
    if (!vm) {
        fputs("FAIL: failed to create VM\n", stderr);
        return 1;
    }

    rc = hvm_execute(vm, &bc);
    hvm_destroy(vm);
    return expect_true(rc == 0, "valid bytecode should execute");
}

static int test_invalid_function_code_range(void) {
    HVM* vm;
    int rc;
    HBCString strings[] = {
        {4u, "main"},
    };
    uint8_t code[] = {
        OP_HALT,
    };
    HBCFunction functions[] = {
        {0u, 0u, 1u, 1u, 0u, 1u, 1u},
    };
    HBytecode bc = make_base_bytecode(strings, functions, code, sizeof(code));

    vm = hvm_create(NULL);
    if (!vm) {
        fputs("FAIL: failed to create VM\n", stderr);
        return 1;
    }

    rc = hvm_execute(vm, &bc);
    hvm_destroy(vm);
    return expect_true(rc != 0, "invalid function code range should be rejected");
}

static int test_stack_overflow_is_reported(void) {
    HVM* vm;
    int rc;
    size_t i;
    size_t code_size = HVM_STACK_MAX + 1u;
    uint8_t* code = (uint8_t*)calloc(code_size, sizeof(uint8_t));
    HBCString strings[] = {
        {4u, "main"},
    };
    HBCFunction functions[] = {
        {0u, 0u, 1u, HVM_STACK_MAX, 0u, 0u, (uint32_t)code_size},
    };
    HBytecode bc;

    if (!code) {
        fputs("FAIL: failed to allocate stack overflow test code\n", stderr);
        return 1;
    }
    for (i = 0; i < code_size; ++i) {
        code[i] = OP_TRUE;
    }
    bc = make_base_bytecode(strings, functions, code, code_size);

    vm = hvm_create(NULL);
    if (!vm) {
        free(code);
        fputs("FAIL: failed to create VM\n", stderr);
        return 1;
    }

    rc = hvm_execute(vm, &bc);
    hvm_destroy(vm);
    free(code);
    return expect_true(rc != 0, "stack overflow bytecode should fail safely");
}

static int test_loader_rejects_truncated_string(void) {
    const char* path = "security_regression_bad.hbc";
    HBCFileHeader header;
    HBytecode bytecode;
    char error_message[128];
    FILE* file;
    int ok;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, HBC_MAGIC, 4);
    header.version_major = HBC_VERSION_MAJOR;
    header.version_minor = HBC_VERSION_MINOR;
    header.string_count = 1u;
    header.function_count = 1u;
    header.entry_function_index = 0u;

    file = fopen(path, "wb");
    if (!file) {
        fputs("FAIL: failed to create malformed bytecode fixture\n", stderr);
        return 1;
    }
    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        remove(path);
        fputs("FAIL: failed to write malformed bytecode fixture\n", stderr);
        return 1;
    }
    fclose(file);

    ok = hvm_bytecode_load_file(path, &bytecode, error_message, sizeof(error_message));
    remove(path);
    if (ok) {
        hbytecode_free(&bytecode);
    }
    return expect_true(!ok, "truncated string table should be rejected by loader");
}

int main(void) {
    int failures = 0;
    failures += test_valid_nil_return();
    failures += test_invalid_function_code_range();
    failures += test_stack_overflow_is_reported();
    failures += test_loader_rejects_truncated_string();
    return failures == 0 ? 0 : 1;
}
