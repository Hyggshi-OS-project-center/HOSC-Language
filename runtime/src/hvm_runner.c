/* hvm_runner.c - HOSC source file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "hvm.h"
#include "hvm_platform.h"
#include "bytecode_io.h"
#include "file_utils.h"


#define BUNDLE_MAGIC "HOSCEXE1"
#define BUNDLE_MAGIC_LEN 8

static int get_self_path(char *out, size_t out_size, const char *argv0) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    (void)argv0;

}

static HVM_Instruction *read_embedded_bytecode(const char *self_path, size_t *out_count) {
    unsigned char *exe_data;
    size_t exe_size;
    size_t footer_size;
    const unsigned char *footer;
    uint64_t payload_size_u64;
    size_t payload_size;
    size_t payload_offset;
    HVM_Instruction *code;

    if (!self_path || !out_count) return NULL;

    exe_data = hosc_read_file_bytes(self_path, &exe_size);
    if (!exe_data) return NULL;

    footer_size = BUNDLE_MAGIC_LEN + sizeof(uint64_t);
    if (exe_size < footer_size) {
        free(exe_data);
        return NULL;
    }

    footer = exe_data + (exe_size - footer_size);
    if (memcmp(footer, BUNDLE_MAGIC, BUNDLE_MAGIC_LEN) != 0) {
        free(exe_data);
        return NULL;
    }

    memcpy(&payload_size_u64, footer + BUNDLE_MAGIC_LEN, sizeof(uint64_t));
    if (payload_size_u64 > (uint64_t)(exe_size - footer_size)) {
        free(exe_data);
        return NULL;
    }

    payload_size = (size_t)payload_size_u64;
    payload_offset = exe_size - footer_size - payload_size;

    code = hvm_bytecode_parse_buffer(exe_data + payload_offset, payload_size, out_count);
    free(exe_data);
    return code;
}

int main(int argc, char **argv) {
    int rc = 1;
    size_t count = 0;
    HVM_Instruction *code = NULL;
    HVM_VM *vm = NULL;
    int from_embedded = 0;

    if (argc >= 2) {
        code = hvm_bytecode_read_file(argv[1], &count);
        if (!code) {
            fprintf(stderr, "Error: cannot read %s\n", argv[1]);
            goto done;
        }
    } else {
        char self_path[1024];
        if (!get_self_path(self_path, sizeof(self_path), argv[0])) {
            fprintf(stderr, "Usage: %s program.hbc\n", argv[0]);
            goto done;
        }

        code = read_embedded_bytecode(self_path, &count);
        if (!code) {
            fprintf(stderr, "Usage: %s program.hbc\n", argv[0]);
            fprintf(stderr, "Hint: this runtime can also execute bundled bytecode when embedded.\n");
            goto done;
        }

        from_embedded = 1;
    }

    if (from_embedded) {
        hvm_platform_hide_console_if_needed(code, count);
    }

    vm = hvm_create();
    if (!vm) {
        fprintf(stderr, "Error: cannot create VM\n");
        goto done;
    }

    if (!hvm_load_bytecode(vm, code, count)) {
        fprintf(stderr, "Error: load bytecode failed\n");
        goto done;
    }

    if (!hvm_run(vm)) {
        const char *msg = hvm_get_error(vm);
        fprintf(stderr, "VM execution failed%s%s\n", msg ? ": " : "", msg ? msg : "");
        goto done;
    }

    rc = 0;

done:
    hvm_bytecode_free(code, count);
    if (vm) hvm_destroy(vm);
    return rc;
}










