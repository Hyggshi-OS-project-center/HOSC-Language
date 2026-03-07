#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "parser.h"
#include "codegen.h"
#include "hvm.h"
#include "hvm_compiler.h"

#define MAGIC "HBC1"

typedef enum { OP_NONE=0, OP_INT=1, OP_FLOAT=2, OP_STRING=3 } OpKind;

static char *read_file(const char *path) {
    FILE *fp;
    long size;
    char *buffer;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size = ftell(fp);
    if (size < 0) { fclose(fp); return NULL; }
    rewind(fp);
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) { fclose(fp); return NULL; }
    if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) { free(buffer); fclose(fp); return NULL; }
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static int write_c_file(const char *path, const char *code) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    fputs(code, fp);
    fclose(fp);
    return 1;
}

static int run_c_compiler(const char *c_path, const char *exe_path) {
    char command[1024];
    int written = snprintf(command, sizeof(command), "gcc -O2 -std=c99 \"%s\" -o \"%s\"", c_path, exe_path);
    if (written < 0 || written >= (int)sizeof(command)) return 0;
    return system(command) == 0;
}

static int write_bytecode(HVM_VM *vm, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    uint32_t count = (uint32_t)vm->instruction_count;
    fwrite(MAGIC, 1, 4, fp);
    fwrite(&count, sizeof(uint32_t), 1, fp);
    for (size_t i = 0; i < vm->instruction_count; i++) {
        HVM_Instruction *ins = &vm->instructions[i];
        uint8_t op = (uint8_t)ins->opcode;
        uint8_t kind = OP_NONE;
        fwrite(&op, 1, 1, fp);
        switch (ins->opcode) {
            case HVM_PUSH_INT:
            case HVM_PUSH_BOOL:
            case HVM_JUMP:
            case HVM_JUMP_IF_FALSE:
            case HVM_JUMP_IF_TRUE:
            case HVM_CALL:
            case HVM_STORE:
            case HVM_LOAD:
                kind = OP_INT;
                fwrite(&kind, 1, 1, fp);
                fwrite(&ins->operand.int_operand, sizeof(int64_t), 1, fp);
                break;
            case HVM_PUSH_FLOAT:
                kind = OP_FLOAT;
                fwrite(&kind, 1, 1, fp);
                fwrite(&ins->operand.float_operand, sizeof(double), 1, fp);
                break;
            case HVM_PUSH_STRING:
            case HVM_STORE_GLOBAL:
            case HVM_LOAD_GLOBAL:
            case HVM_CREATE_WINDOW:
                kind = OP_STRING;
                fwrite(&kind, 1, 1, fp);
                if (ins->operand.string_operand) {
                    uint32_t len = (uint32_t)strlen(ins->operand.string_operand);
                    fwrite(&len, sizeof(uint32_t), 1, fp);
                    fwrite(ins->operand.string_operand, 1, len, fp);
                } else {
                    uint32_t len = 0;
                    fwrite(&len, sizeof(uint32_t), 1, fp);
                }
                break;
            default:
                kind = OP_NONE;
                fwrite(&kind, 1, 1, fp);
                break;
        }
    }
    fclose(fp);
    return 1;
}

static HVM_Instruction *read_bytecode(const char *path, size_t *out_count) {
    FILE *fp = fopen(path, "rb");
    char magic[5] = {0};
    uint32_t count;
    if (!fp) return NULL;
    if (fread(magic, 1, 4, fp) != 4 || strncmp(magic, MAGIC, 4) != 0) { fclose(fp); return NULL; }
    if (fread(&count, sizeof(uint32_t), 1, fp) != 1) { fclose(fp); return NULL; }
    HVM_Instruction *code = calloc(count, sizeof(HVM_Instruction));
    if (!code) { fclose(fp); return NULL; }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t op, kind;
        if (fread(&op, 1, 1, fp) != 1) { free(code); fclose(fp); return NULL; }
        if (fread(&kind, 1, 1, fp) != 1) { free(code); fclose(fp); return NULL; }
        code[i].opcode = (HVM_Opcode)op;
        switch (kind) {
            case OP_INT:
                fread(&code[i].operand.int_operand, sizeof(int64_t), 1, fp);
                break;
            case OP_FLOAT:
                fread(&code[i].operand.float_operand, sizeof(double), 1, fp);
                break;
            case OP_STRING: {
                uint32_t len = 0;
                fread(&len, sizeof(uint32_t), 1, fp);
                code[i].operand.string_operand = calloc(len + 1, 1);
                if (len > 0 && code[i].operand.string_operand) {
                    fread(code[i].operand.string_operand, 1, len, fp);
                    code[i].operand.string_operand[len] = '\0';
                }
                break;
            }
            case OP_NONE:
            default:
                break;
        }
    }
    fclose(fp);
    *out_count = count;
    return code;
}

static int compile_to_bytecode(ASTNode *ast, const char *bc_path, int run_after) {
    int ok = 0;
    HVM_VM *vm = hvm_create();
    HVM_Compiler *c = hvm_compiler_create(vm);
    if (!vm || !c) goto done;
    if (!hvm_compiler_compile_ast(c, ast)) goto done;
    if (bc_path) {
        if (!write_bytecode(vm, bc_path)) goto done;
        printf("Generated bytecode: %s\n", bc_path);
    }
    if (run_after) {
        if (!hvm_run(vm)) {
            fprintf(stderr, "VM execution failed\n");
            goto done;
        }
    }
    ok = 1;

done:
    hvm_compiler_destroy(c);
    hvm_destroy(vm);
    return ok;
}

static void print_usage(const char *exe) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <input.hosc> [-c out.c] [-o out.exe] [-b out.hbc] [-r]\n", exe);
    fprintf(stderr, "Options:\n  -c  emit C source\n  -o  build native exe via gcc (needs gcc)\n  -b  emit bytecode file (.hbc)\n  -r  run with VM (no gcc needed)\n");
}

int main(int argc, char **argv) {
    const char *input_path;
    const char *c_output_path = NULL;
    const char *exe_output_path = NULL;
    const char *bc_output_path = NULL;
    int run_vm = 0;
    int i;

    (void)read_bytecode; // keep loader referenced for optional future run-from-bytecode path

    if (argc < 2) { print_usage(argv[0]); return 1; }
    input_path = argv[1];

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            c_output_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            exe_output_path = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            bc_output_path = argv[++i];
        } else if (strcmp(argv[i], "-r") == 0) {
            run_vm = 1;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    char *source = read_file(input_path);
    if (!source) { fprintf(stderr, "Error: cannot read %s\n", input_path); return 1; }

    ASTNode *ast = parser_parse(source);
    free(source);
    if (!ast) { fprintf(stderr, "Error: parse failed\n"); return 1; }

    if (bc_output_path || run_vm) {
        int ok = compile_to_bytecode(ast, bc_output_path, run_vm);
        free_ast(ast);
        ast_release_arena();
        return ok ? 0 : 1;
    }

    if (!c_output_path) c_output_path = "output.c";

    char *code = codegen_generate(ast);
    free_ast(ast);
    ast_release_arena();
    if (!code) { fprintf(stderr, "Error: codegen failed\n"); return 1; }

    if (!write_c_file(c_output_path, code)) {
        free(code);
        fprintf(stderr, "Error: cannot write %s\n", c_output_path);
        return 1;
    }
    printf("Generated C file: %s\n", c_output_path);

    if (exe_output_path) {
        if (!run_c_compiler(c_output_path, exe_output_path)) {
            free(code);
            fprintf(stderr, "Error: native compilation failed\n");
            return 1;
        }
        printf("Generated executable: %s\n", exe_output_path);
    }

    free(code);
    return 0;
}






