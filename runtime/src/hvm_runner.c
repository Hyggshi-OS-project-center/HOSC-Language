#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "hvm.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define MAGIC "HBC1"
#define BUNDLE_MAGIC "HOSCEXE1"
#define BUNDLE_MAGIC_LEN 8

typedef enum { OP_NONE=0, OP_INT=1, OP_FLOAT=2, OP_STRING=3 } OpKind;

static int opcode_uses_string_operand(HVM_Opcode opcode) {
    return opcode == HVM_PUSH_STRING ||
           opcode == HVM_STORE_GLOBAL ||
           opcode == HVM_LOAD_GLOBAL ||
           opcode == HVM_CREATE_WINDOW;
}

static void free_bytecode(HVM_Instruction *code, size_t count) {
    size_t i;
    if (!code) return;
    for (i = 0; i < count; i++) {
        if (opcode_uses_string_operand(code[i].opcode) && code[i].operand.string_operand) {
            free(code[i].operand.string_operand);
            code[i].operand.string_operand = NULL;
        }
    }
    free(code);
}

static unsigned char *read_file_bytes(const char *path, size_t *out_size) {
    FILE *fp;
    long file_size;
    size_t read_size;
    unsigned char *buffer;

    if (!path || !out_size) return NULL;
    *out_size = 0;

    fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = (unsigned char *)malloc((size_t)file_size);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read_size != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    *out_size = read_size;
    return buffer;
}

static HVM_Instruction *parse_bytecode_buffer(const unsigned char *buf, size_t size, size_t *out_count) {
    uint32_t count;
    size_t offset;
    HVM_Instruction *code;
    uint32_t i;

    if (!buf || !out_count) return NULL;
    *out_count = 0;

    if (size < 8) return NULL;
    if (memcmp(buf, MAGIC, 4) != 0) return NULL;

    memcpy(&count, buf + 4, sizeof(uint32_t));
    offset = 8;

    code = (HVM_Instruction *)calloc(count, sizeof(HVM_Instruction));
    if (!code) return NULL;

    for (i = 0; i < count; i++) {
        uint8_t op;
        uint8_t kind;

        if (offset + 2 > size) {
            free_bytecode(code, i);
            return NULL;
        }

        op = buf[offset++];
        kind = buf[offset++];

        code[i].opcode = (HVM_Opcode)op;
        switch (kind) {
            case OP_INT:
                if (offset + sizeof(int64_t) > size) {
                    free_bytecode(code, i + 1);
                    return NULL;
                }
                memcpy(&code[i].operand.int_operand, buf + offset, sizeof(int64_t));
                offset += sizeof(int64_t);
                break;

            case OP_FLOAT:
                if (offset + sizeof(double) > size) {
                    free_bytecode(code, i + 1);
                    return NULL;
                }
                memcpy(&code[i].operand.float_operand, buf + offset, sizeof(double));
                offset += sizeof(double);
                break;

            case OP_STRING: {
                uint32_t len = 0;
                if (offset + sizeof(uint32_t) > size) {
                    free_bytecode(code, i + 1);
                    return NULL;
                }
                memcpy(&len, buf + offset, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                code[i].operand.string_operand = (char *)calloc((size_t)len + 1, 1);
                if (!code[i].operand.string_operand) {
                    free_bytecode(code, i + 1);
                    return NULL;
                }

                if (offset + len > size) {
                    free_bytecode(code, i + 1);
                    return NULL;
                }

                if (len > 0) {
                    memcpy(code[i].operand.string_operand, buf + offset, len);
                }
                code[i].operand.string_operand[len] = '\0';
                offset += len;
                break;
            }

            case OP_NONE:
            default:
                break;
        }
    }

    *out_count = count;
    return code;
}

static HVM_Instruction *read_bytecode(const char *path, size_t *out_count) {
    unsigned char *data;
    size_t size;
    HVM_Instruction *code;

    data = read_file_bytes(path, &size);
    if (!data) return NULL;

    code = parse_bytecode_buffer(data, size, out_count);
    free(data);
    return code;
}

static int get_self_path(char *out, size_t out_size, const char *argv0) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    (void)argv0;

#ifdef _WIN32
    {
        DWORD len = GetModuleFileNameA(NULL, out, (DWORD)out_size);
        if (len == 0 || len >= out_size) return 0;
        out[len] = '\0';
        return 1;
    }
#else
    if (!argv0 || !argv0[0]) return 0;
    strncpy(out, argv0, out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
#endif
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

    exe_data = read_file_bytes(self_path, &exe_size);
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

    code = parse_bytecode_buffer(exe_data + payload_offset, payload_size, out_count);
    free(exe_data);
    return code;
}

static int str_eq_ci(const char *a, const char *b) {
    unsigned char ca;
    unsigned char cb;

    if (!a || !b) return 0;
    while (*a && *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (tolower(ca) != tolower(cb)) return 0;
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

#ifdef _WIN32
static int bytecode_has_gui_opcodes(const HVM_Instruction *code, size_t count) {
    size_t i;
    if (!code) return 0;

    for (i = 0; i < count; i++) {
        HVM_Opcode op = code[i].opcode;
        if (op == HVM_CREATE_WINDOW ||
            op == HVM_DRAW_TEXT ||
            op == HVM_DRAW_BUTTON ||
            op == HVM_DRAW_BUTTON_STATE ||
            op == HVM_DRAW_INPUT ||
            op == HVM_DRAW_INPUT_STATE ||
            op == HVM_DRAW_TEXTAREA ||
            op == HVM_DRAW_IMAGE ||
            op == HVM_MENU_SETUP_NOTEPAD ||
            op == HVM_LOOP) {
            return 1;
        }
    }

    return 0;
}

static int env_force_console(void) {
    const char *env = getenv("HVM_FORCE_CONSOLE");
    if (!env || !env[0]) return 0;
    return strcmp(env, "1") == 0 || str_eq_ci(env, "true") || str_eq_ci(env, "yes");
}

static void hide_console_window_if_needed(const HVM_Instruction *code, size_t count) {
    HWND console_hwnd;
    if (env_force_console()) return;
    if (!bytecode_has_gui_opcodes(code, count)) return;

    console_hwnd = GetConsoleWindow();
    if (console_hwnd) {
        ShowWindow(console_hwnd, SW_HIDE);
    }
}
#else
static void hide_console_window_if_needed(const HVM_Instruction *code, size_t count) {
    (void)code;
    (void)count;
}
#endif

int main(int argc, char **argv) {
    int rc = 1;
    size_t count = 0;
    HVM_Instruction *code = NULL;
    HVM_VM *vm = NULL;
    int from_embedded = 0;

    if (argc >= 2) {
        code = read_bytecode(argv[1], &count);
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
        hide_console_window_if_needed(code, count);
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
    free_bytecode(code, count);
    if (vm) hvm_destroy(vm);
    return rc;
}








